# JIT batch 4: loop-carried locals in registers (F3) and short-circuit branches (F6)

Design for the two findings of `JIT_CODEGEN_ASSESSMENT_2026_09.md` still open after batches 1-3 that
touch the most code: **F3**, locals that live in memory across every basic block, and **F6**, boolean
connectives that round-trip through a temporary local. Written 2026-09-09 before implementation;
section 6 records what was built and measured.

## 1. Where the time goes today

Measured on this machine (Windows x64, `-opt s3`, `--jit=1`, master at `9b0014b0`), the two kernels
the earlier batches did not move:

| kernel | interpreter | JIT | what the loop does |
|---|---|---|---|
| `Locals` -- 8 loop-carried scalars, 8 adds | 1.399 s | 0.032 s | 56 memory operands in 92 instructions: every local is loaded from its frame slot when read and stored back when written, on every iteration |
| `Branchy` -- `if(i % 3 = 0 & i % 5 <> 0)` | 1.274 s | 0.035 s | the `&` materializes one side with `cmov`, stores it to a temp slot, reloads it, tests it, then branches |

Both are artefacts of the code shape, not of the work the loop does.

## 2. F6: boolean connectives

### 2.1 What the compiler emits now

`IntermediateEmitter::EmitAndOr` (`core/compiler/intermediate.cpp`) is the only emitter for
`AND_EXPR`/`OR_EXPR`, and the same code runs whether the connective sits in an `if` condition or on
the right of an assignment. One naming trap first: the parser stores the **source-left** operand in
the node's `right` field and the source-right operand in `left` (`ParseLogic` ends with
`SetLeft(right); SetRight(left)`), so the emitter's "emit right" is the source-left operand. For
`a & b` it emits:

```
<a>                        ; source-left, evaluated first, always
JMP  L1, 1                 ; pop; if a = 1 goto L1
LOAD_INT_LIT 0
STOR_INT_VAR 0 LOCL        ; temp := 0
JMP  L2, -1
L1:
<b>                        ; source-right, only when a was true
STOR_INT_VAR 0 LOCL        ; temp := b
L2:
LOAD_INT_VAR 0 LOCL        ; the expression's value
```

The semantics are right: left to right, short-circuit. (The first draft of this document read the
emitter without the parser and claimed the order was reversed. The regression test written to pin
the order, `programs/regression/core_bool_short_circuit.obs`, passed on the unmodified compiler and
failed on the "fix", which is how the field naming came to light. No earlier test observed
evaluation order at all, so the test stays as the guard it turned out to be.)

What is wrong is the cost. **The value crosses a label through local slot 0.** The JIT requires an
empty working stack at every label, so a value produced on two paths must be joined through memory:
hence the temp, the store, the reload, and -- because the second operand's compare is followed by a
`STOR` rather than a `JMP` -- the failed compare/branch fusion that leaves a four-instruction `cmov`
sequence. Slot 0 is reserved method-wide (`Method::HasAndOr`, set in `context.cpp`), shifting every
local id by one, and the bytecode inliner refuses to inline any method that has the flag
(`optimization.cpp:2209`).

### 2.2 The change

**Branch context.** A new `IntermediateEmitter::EmitBranch(Expression* e, long target, bool if_true)`
emits "jump to `target` when `e` is `if_true`" without producing a value. With `first` the
source-left operand (the node's `right`) and `second` the source-right one:

```
AND, jump-if-false:   EmitBranch(first, target, false); EmitBranch(second, target, false)
AND, jump-if-true:    skip = new label; EmitBranch(first, skip, false); EmitBranch(second, target, true); LBL skip
OR,  jump-if-true:    EmitBranch(first, target, true);  EmitBranch(second, target, true)
OR,  jump-if-false:   skip = new label; EmitBranch(first, skip, true);  EmitBranch(second, target, false); LBL skip
NOT (NEQL_EXPR whose right is the literal true, which is how the parser spells `<>x`):
                      EmitBranch(operand, target, !if_true)
anything else:        EmitExpression(e); JMP target, if_true ? 1 : 0
```

Every statement condition routes through it: `EmitIf`, `EmitWhile`, `EmitDoWhile` (when it has no
post statement -- with one, the value has to wait on the stack while the post statement runs),
the classic `EmitFor` shape and `EmitConditional` (`?:`) currently do `EmitExpression(cond); JMP
label, false` and become `EmitBranch(cond, label, false)`. The order and the skipping are exactly
what `EmitAndOr` did; what disappears is the temp, the store, the reload and the `cmov`: each
comparison is immediately followed by the `JMP` that consumes it, the shape both JIT backends fuse
into `cmp`/`jcc`. The interpreter executes fewer instructions too.

**Value context** (`x := a & b`, an argument, a return value) keeps `EmitAndOr` as it is -- a value
that reaches a join must go through memory under the JIT's stack model. `HasAndOr` and the slot-0
reservation are unchanged in this batch, so no `.obe`/`.obl` layout, the debugger's id mapping
(`dap.cpp`) and the bytecode inliner's accounting all stay as they are. A later refinement can set
the flag only for value-context connectives, which would also let the inliner accept methods whose
only `&`/`|` sit in conditions.

### 2.3 Semantics

Unchanged: left to right, short-circuit, and now pinned by a test that records which operands ran
and in what order, in `if`, `while`, `for`, `?:`, negated, nested, and in value context.

## 3. F3: loop-carried locals in registers

### 3.1 The model today

The AMD64 backend keeps every local in its frame slot at `[RBP + offset]`. `ProcessLoad` defers a
local read as a `MEM_INT` working-stack entry that consumers materialize with a load; `ProcessStore`
writes through to the slot and keeps the stored register in `local_reg_cache` for the *next* load
only, and `FlushLocalCache` drops the cache at every `LBL` and every `JMP`. So inside a loop body a
local costs a load per read and a store per write, and the loop-carried dependency `i := i + 1` runs
through a store-to-load forward every iteration. ARM64 is the same model with the ISA swapped.

### 3.2 Design: pin the hottest scalar locals of each loop to callee-saved registers

**Regions.** `Compile()` already finds loops: every `JMP` whose target index is lower than its own is
a back-edge, recorded in `detected_loops` as `(header, back_edge)`. Overlapping and nested pairs are
merged into maximal *regions* `[header, end]` (outer loops absorb inner ones). A region is *eligible*
only if no jump from outside it targets an interior label other than the header -- structured code
never does this; if it happens the region is skipped and the method keeps today's code.

**Candidates.** A local is pinnable if it is accessed only through `LOAD/STOR/COPY_*_INT_VAR` with
`LOCL` context and its declared type is `INT_PARM` or `CHAR_PARM`. The type comes from the method's
`StackDclr` list, the same list the collector walks (`MemoryManager::CheckJitRoots`): declaration `j`
occupies slot ids from `base + sum(width of dclrs 0..j-1)`, width 2 for `FUNC_PARM` and 1 otherwise,
`base = 1` when `HasAndOr` reserves slot 0. Object, array and function locals are never pinned: the
collector reads and *rewrites* their slots (young-generation objects move), so their memory must be
current at every safepoint. Integer slots are skipped by the collector, so a stale integer in memory
is invisible to it. Floats are phase 2 (section 5).

**Selection.** Per region, count the loads and stores of each candidate, weighting an access inside
a nested loop by its depth; take the top `K`, ties to the lower slot. `K` is the number of pinning
registers:

| platform | pinning registers | why |
|---|---|---|
| Windows x64 | `R13`, `R14`, `R15` | callee-saved, unused by the backend today; pushed in the prologue and popped in the epilogue only when the method pins (two pushes keep the 16-byte alignment, the third pairs with the existing 8-byte filler) |
| Linux/macOS x64 | `R13`, `R14`, `R15` | already saved by the prologue; removed from `aux_regs`, which keeps `R11`, `R10`, `R8` |
| ARM64 | `X20`-`X27` | callee-saved, unused (`X19` holds `&stw_active`); saved to frame slots beside the `X19` save |

Callee-saved is the whole safety argument: every path that leaves JIT code -- the interpreter
callback, native and math calls, the write barrier's slow path, the safepoint's `SafePoint` call, a
JIT-to-JIT call whose callee pins and therefore saves/restores in its own prologue -- preserves these
registers by ABI, so no call site needs auditing and no spill code is added. The price is `K` = 3 on
AMD64 in this batch; the allocator's own pool is untouched.

**Entry.** A region's header is the back-edge *target*: the instruction after the loop's label, because
the compiler resolves a label to the index of the next instruction (nothing keyed on the `LBL`'s own
index ever matches -- see section 6). That instruction gets two native offsets: `entry`, where the
pinned locals are loaded from their slots, and `loop`, just after those loads and before the safepoint
poll, so every iteration polls. The jump
fixup pass (`Compile()`'s walk over `jump_table`) sends a jump to the header from *inside* the region
to `loop` and one from outside (or the fall-through) to `entry`. That handles a `continue`, the
back-edge, and an `if`/`else` join that the compiler folded onto the loop header.

**Exit.** A jump from inside the region to a label outside it goes through a stub emitted after the
method body: the stub stores every pinned register to its slot and jumps to the real target. The
fixup pass retargets the exit jump's displacement to the stub (conditional exits therefore keep the
fall-through hot path clean). The instruction after the region's last one also gets the stores, for
the fall-through exit of a conditional back-edge (`do`/`while`). A `RTRN` inside the region needs
nothing: the frame is gone and the epilogue restores the registers.

**Inside the region.**

- `LOAD_INT_VAR` of a pinned slot: `mov r, Rpin` into a fresh pool register, pushed as `REG_INT`.
  A copy rather than the pinned register itself, because consumers mutate `REG_INT` operands in
  place (`add_reg_reg` and friends write their destination).
- `STOR_INT_VAR` of a pinned slot: `mov Rpin, imm` / `mov Rpin, r` / `mov r, [slot']; mov Rpin, r`
  for `IMM_INT` / `REG_INT` / `MEM_INT` operands. No memory store. Any `local_reg_cache` entry for
  the slot is impossible (pinned slots never enter the cache) and asserted so.
- `COPY_INT_VAR` (store that keeps the value on the working stack): the same, leaving the value.
- Everything else -- the cache, the working stack, spills to `TMP_REG_n`, inlined callees whose
  locals live in the inline area at other offsets, `ProcessParameters` at method entry, the
  self-recursive tail-call back-edge (which stores the new argument values before jumping to the
  header, an interior jump, so the registers already hold them) -- is unchanged.

**Verification hooks.** `OBJECK_JIT_REPORT=1` gains a line per pinned region (`method: pinned N
locals in [header,end]`), and `_DEBUG_JIT` listings show the entry loads and exit stubs.

### 3.3 What is not done here

- Floats in `XMM6`-`XMM9` on Windows (callee-saved there, unused by the allocator) and in
  `D8`-`D15` on ARM64 (already saved by the prologue). On Linux/macOS x64 every XMM register is
  caller-saved, so floats there would need spill code around calls -- the reason the integer design
  insists on callee-saved registers.
- `RSI`/`RDI` as two more pinning registers on Windows (callee-saved there, caller-saved on SysV).
- Keeping a local pinned across a region boundary, or pinning in straight-line code.

## 4. Verification

1. **Equivalence fixture** (`programs/regression/vm_jit_equiv.obs`, run with `--jit=off`, default,
   and `--jit=1`; the output must be byte-identical): new probes for eight loop-carried scalars, a
   loop with `break` and `continue`, nested loops that share locals, a `do`/`while`, a loop with a
   method call and one with an allocation (an object local beside the integers, through a minor
   collection), an early `return` from inside a loop, a self-recursive tail call, a loop whose header
   follows an `if`/`else`, and floats beside pinned integers.
2. **Order test** for F6 (`programs/regression/core_bool_short_circuit.obs`): a class variable
   records which operands ran, in what order; `&` and `|` in `if`, `while`, `for`, `?:`, negated,
   nested three deep, and in value context.
3. The regression suite, normal and with `OBJECK_JIT_THRESHOLD=1`; the VM flag tests; every library
   rebuilt with the new compiler and the suite run again.
4. `programs/tests/jit_probe.obs` before/after, `Locals` and `Branchy` in particular; the five CI legs
   for ARM64 and Linux, plus the maintainer's macOS run.

## 5. Phasing

| phase | content | backends |
|---|---|---|
| 4a | F6: `EmitBranch`, left-first `EmitAndOr`, the order test | compiler only |
| 4b | F3 on AMD64: regions, selection, entry/exit, pinned load/store, report line, fixture probes | AMD64 |
| 4c | F3 on ARM64, same design with `X20`-`X27` | ARM64 |
| 4d | floats where the registers are callee-saved; `RSI`/`RDI` on Windows; `HasAndOr` only for value-context connectives | both |

## 6. Outcome

**4a (F6)** landed as `a8ad2c0d27`: conditions branch directly, `Branchy` 0.035 s -> 0.026 s, and the
evaluation-order test showed the order had always been right (section 2.1).

**4b (F3, AMD64)**: up to three INT/CHAR locals per loop in `R13`-`R15`. Windows x64, `-opt s3`,
`--jit=1`, median of three:

| kernel | before 4b | after 4b |
|---|---|---|
| `Locals` (8 loop-carried scalars) | 0.027 s | 0.020 s |
| `ArraySum` | 0.017 s | 0.014 s |
| `IntLoop`, `FloatDot`, `Branchy`, `CallLoop` | -- | within noise |

Three registers cover three of `Locals`' eight scalars; the rest is phase 4d's. Verified as
section 4 lists: fixture byte-identical between interpreter and JIT with the new probes, flag tests,
the regression suite normal and with every method JIT-compiled.

**Found on the way, all on master before this batch:**

1. *No JIT loop was ever polled for a GC safepoint*, on either backend. The poll was emitted in `case
   LBL` and keyed on the label's index while jump targets are the instruction after the label. A
   collection on another thread waited for the whole loop; `programs/regression/jit_gc_safepoint.obs`
   fails on the old code and passes now. The fix (the poll at the target instruction) is why the
   kernels above did not all get faster: they gained a compare-and-branch per iteration they should
   always have had.
2. *Magic division with the dividend in `RDX`* multiplied the constant by itself (batch 2). The
   fixture's `JoinAndFloats` probe found it.
3. *The 16-bit movers had no REX prefix*, so a char element addressed through `R8`-`R15` was read
   from `RAX`-`RDI`. Dormant until batch 3 put `R8`-`R11` in the Windows pool; pinning changed the
   JSON scanner's allocation and crashed it. The fixture's `Narrow` probe crashes on the old code.
   Any register added to a pool needs every encoder checked for a computed REX.

4. *A method flagged `HasAndOr` lost its slot 0 under F6.* The compiler reserves local slot 0 in any
   method with `&`/`|`, a ternary, a `select` or a lambda, and the ARM64 collector skips that slot when
   it walks a JIT frame. Both JITs laid the frame out from the ids the bytecode references, so once
   conditions branched directly, methods whose connectives are all in conditions had no slot 0 and the
   ARM64 walk read every declared slot one off (`MarkMemory(0xb)` from `String:Append`'s `Char[]`
   parameter; five tests on the Linux and macOS ARM64 legs, invisible on x64 whose walk runs from the
   other end). The frame layout is a contract with the collector, not a function of which ids the
   bytecode happens to use: both backends now reserve the slot whenever the flag is set. Found by
   running CI with `OBJECK_GC_TRACE=1`, which names each scanned JIT frame and slot into the failure
   artifact -- a validation gap worth remembering: the standard library must be *rebuilt* with a changed
   compiler before a local run means anything, since CI's POSIX legs rebuild it and Windows does not.
5. *ARM64 leaked one floating-point register per fused float compare* (the D register stayed on the
   working stack as `REG_FLOAT`; only `REG_INT` was released after a fused jump), so a method with more
   fused float compares than the pool holds fell back to the interpreter.

6. *A `native` method could be inlined into a caller that never runs natively.* The `-opt s3`
   inliner pastes small calls into their caller, and the copy runs in whatever engine the caller
   does. `Main` was exempt (its comment says why: it is never compiled), but a thread's `Run` was
   not, and `Run` is called once per thread so the auto-JIT never compiles it either: a worker's
   native loop was pasted into `Run` and interpreted, ~50x slower than the same code on the main
   thread. First seen as "a compiled method called from a spawned thread runs at interpreter
   speed", and chased as a thread problem until the bytecode listing showed no call at all -- the
   proof was an out-of-bounds index inside the loop, which the worker reported with the
   interpreter's message and the main thread with the JIT's. The compiler now refuses to inline a
   `native` method into a non-native caller; a native caller is compiled whole, so inlining into
   it changes nothing (`jit_native_inline.obs`).
