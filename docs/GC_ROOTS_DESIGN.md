# GC roots: a precise root model for the interpreter and both JITs

Written 2026-10-29 against master `c816f3f9ae`, before any implementation. It was prompted by two
suspected JIT root bugs (`jit_stale_spill_slot`, `jit_inline_local_root`) and a run of collector fixes
that each patched one consequence of the same assumption. This is a design for review, not a record of
built work: nothing below has landed.

**Status, 2026-10-30.** The user chose the full redesign over a targeted fix.
- **Phase 0 is merged** (#1455): `--jit=off` now compiles nothing.
- **Phase 1 is built**, on branch `gc-roots/phase1`. What changed against this document:
  - **The knob is `OBJECK_GC_ROOTS=audit`**, not `OBJECK_GC_VERIFY=roots`. It reports, at every
    collection, where the maps and the live stacks disagree. Its per-method dump is
    `OBJECK_GC_ROOTS_DUMP=<name>`.
  - **The interpreter is not free** (§6.2). `frame->ip` is written only at calls, and error traces read
    it. The instruction in progress now goes to `StackFrame::cur_ip`: a union with `jit_offset`, so both
    JITs' frame records keep their layout. It is written on the dispatch loop's default path, which
    every instruction that can collect takes, and at the safepoint poll, encoded as "between
    instructions". The measured cost is within noise: -0.5% on a call- and allocation-heavy
    interpreted benchmark.
  - **The operand stack is one array per thread**, the frames' segments end to end. The audit splits it
    with each frame's height at its pc. A caller's segment is its height less what its call popped; the
    top frame's segment is anywhere between that and its height.
  - **A trap's stack effect:** `TRAP_RTRN` pops `args` and pushes one word, as the JIT models it. A plain
    `TRAP` pushes nothing, except right before the `RTRN` of a value-returning method, whose value it
    pushes (`Deserializer->ReadInt`).
  - **`GetParamCount()` counts stack words**: a func-ref parameter is two.
  - **The and/or slot starts as the zero the frame is entered with**, on every path, so what two paths
    store meets where they join.
  - **#1457 is a prerequisite**, found by the audit. Three trap handlers left their declared arguments on
    the operand stack for good, interpreted and compiled, keeping objects alive and shifting every frame
    above them. Fixed in #1458.
- **Suite-wide audit** (665 tests, interpreted, 256k nursery, on the #1457 fix):
  - 2,568 maps, 0 failed;
  - 0 threads whose segments do not add up;
  - 42 words left unknown, of about 417,000;
  - 0 reference-typed words at nothing;
  - every Int-typed word at an object is `gc_conservative_bad_class`, which keeps an address in an Int
    on purpose.

## 1. Summary

The collector does not know which words in a frame are references. It finds roots in three ways:

- it walks the declared locals by their declared types;
- it scans untyped words conservatively, marking anything that `IsAllocated` accepts;
- it relocates anything whose target has been forwarded (`FixupSlot`).

The untyped words are:

- the interpreter's operand stacks;
- the and/or slot;
- a fixed window of `JIT_TMP_LOOK_BACK` (16) words beside every JIT frame, whether or not the frame
  wrote them.

Neither JIT backend tells the collector which of those words hold a live reference at a safepoint.

That guess has three consequences. Each is demonstrated below on a fresh Windows x64 build of master:

1. **Dead objects stay reachable.** A spill slot that is never cleared, or never zeroed at entry, keeps
   whatever it last held. `jit_stale_spill_slot` keeps a dead 32 MB array alive (§3.1).
2. **Integers are rewritten.** The nursery moves objects, and fixup "relocates" any untyped word equal to
   a forwarded object's address. That includes an `Int` that holds an address, such as
   `GetInstanceID()`. In the reproducer, 242 of 2,000 such integers changed value across a minor
   collection, interpreted and compiled alike (§3.3). This is a correctness bug, not a leak.
3. **Every new slot kind became a separate fix.** The and/or slot, pending registers at a loop poll,
   self's frame copy, frame layout and closure captures were each fixed one at a time (§4).

The proposal has three parts:

- one shared bytecode analysis that says, for every instruction, which operand-stack depths and which
  untyped slots hold references;
- interpreter frames read that analysis as a stack map;
- JIT frames spill references only to reference-only slots that the collector scans by type, and clear
  them after reload.

Conservative scanning of frames and stacks is then removed, along with the window and the symptom
patches that exist only because of it (§7, phase 5).

**Verdicts on the two suspected bugs:**

- **`jit_stale_spill_slot` is real, JIT-only, and has two paths.** First, AMD64 entry zeroing skips
  `TMP_REG_0` on both ABIs. Second, spill slots are never cleared, so a frame keeps what it spilled for
  as long as it runs. ARM64 has only the second path.
- **`jit_inline_local_root` is refuted.** The AMD64 JIT inliner has been switched off since f871a89252
  (2026-04-01): `ProcessInlineMethod` has no caller. ARM64 has no JIT inliner. The test never reached
  either. The dead inliner should be deleted (phase 5), because enabling it again would introduce
  exactly the defect the test describes.

## 2. Evidence: method and environment

- **Build:** Windows 11, VS 2022, MSBuild of `core/release/objeck.sln` at `c816f3f9ae`, deployed to a
  scratch `deploy-x64`.
- **Configurations:** each test was compiled at `-opt s0` and `-opt s3`, and run with `--jit=off`,
  `--jit=1` and the default threshold.

### 2.1 `--jit=off` does not mean "interpreter only"

`--jit=off` (and `OBJECK_JIT_DISABLE=1`) only raises the auto-JIT threshold. A `native` method is still
compiled. Under `--jit=off`, `OBJECK_JIT_REPORT=1` prints `[jit] Probe2:Leave:: compiled` for a
`native` method, and `System.String:Get` is compiled too, because library methods are `native` as well.

The usage text says "'off' runs the interpreter only". The earlier finding that `jit_stale_spill_slot`
"fails with the JIT off" came from this: the test's methods are all `native`. With `native` removed:

| `jit_stale_spill_slot`, no `native` | `--jit=off` | `--jit=1` | default (10) |
|---|---|---|---|
| `-opt s0` | PASS (1,049,378 B) | **FAIL** (34,603,835 B) | PASS |
| `-opt s3` | PASS | **FAIL** | PASS |

Two consequences follow:

- The nightly differential's `s0/off` configuration (#1443's table) is not interpreter-only either.
- `--jit=off` needs its own fix, or its help text needs to change. That is reported separately and is
  not part of this design.

## 3. What the two tests actually show

### 3.1 Stale spill slots (real)

**Path 1: entry zeroing misses `TMP_REG_0` (AMD64, both ABIs).**

`JitAmd64::RegisterRoot` zeroes `((offset - 8) >> 3) + 10` words upward from the lowest local. On Win64
the locals plus the `RED_ZONE` word take `offset / 8` words, so the count stops one word short of
`TMP_REG_0` (`RBP-32`). POSIX misses `TMP_REG_0` (`RBP-88`) the same way.

The `- 8` dates from the six-slot layout (`+ 6`). 5555f2960b (2026-09-09) widened the slots to ten
without changing it.

In the test, `Leave()` spills the 32 MB array to `TMP_REG_0` across the `StaleSmall` constructor and
returns. `Churn()` is then entered at the same frame pointer and never writes that slot, so every
collection under `Churn()` finds the array there.

Changing the count to `(offset >> 3) + 10` makes s0 pass. That one-line change is kept only as
evidence, on the local branch `scratch/amd64-tmp-reg0-entry-zeroing-evidence`; it is not the fix. The
main checkout's deploy build from 2026-10-22, which has the same count, passes the same test, so frame placement decides whether this path
triggers. The 10-24 call-stack change (13daaacfb2) is the likely trigger; this was not bisected.

**Path 2: spills are never cleared (AMD64 and ARM64).**

At `-opt s3`, the compiler inlines `Leave` into `Run` as bytecode. `MakeBig`'s result is pending in
`Run`'s own frame across `StaleSmall:New`, so `ProcessStackCallback` spills it to `Run`'s `TMP_REG_0`,
reloads it for `Keep`, and leaves the slot as it is. `Run` stays live while `Garbage()` runs.

A trace build that prints the window words the collector finds allocated shows the array in every
collection:

```
[gc] jit frame 'JitStaleSpillSlot:Run:' mem=...EB60 dclrs=2
[gc]   window 10 @...EBC0 -> 0000027E5B6D0060 size=33554457
```

Window word 10 on Win64 is `RBP-32`, which is `TMP_REG_0`. ARM64 zeroes all six `TMP_X` slots at entry
(`RegisterRoot`), so it has path 2 only.

Severity: the effect is retention, at most one object per stale slot per live frame. Nothing is freed
while still in use. It becomes serious when the object is large, as with the 32 MB array here.

### 3.2 Inlined callee locals (refuted)

The claim is that the AMD64 JIT pastes small callees into the caller and puts their locals below the
walked region. The layout part is true of the code: `inline_local_offset` starts below the caller's
locals, and the collector walks only `org_local_space`. But that code never runs:

- **The JIT inliner never runs.** `ProcessInlineMethod` has no caller. f871a89252 replaced the call
  with the callback path ("disable ProcessInlineMethod for MTHD_CALL"). `CanInlineMethod` is still
  called, but only to size `extra_inline_space`, which is dead frame space today.
- **At `-opt s0`, `Fill` is compiled as a separate method.** Its `s->Size()` is a `MTHD_CALL`, so
  `CanInlineMethod` would refuse it anyway.
- **At `-opt s3`, the compiler inlines `Fill` as bytecode.** Its locals become `Run`'s declared locals,
  which the collector walks.
- **A rewritten test that the JIT inliner would accept still passes.** It has no calls and no branches,
  at most 20 instructions, and returns `s[0]`. It passes at s0, s1 and s3 under `--jit=off` and
  `--jit=1`, because nothing inlines it.
- **ARM64 has no JIT inliner.**

The latent hazard is real: re-enabling `ProcessInlineMethod` would reintroduce exactly this defect.
Phase 5 deletes it. Any future JIT inliner must give inlined locals declared, walked slots.

### 3.3 Integers rewritten by fixup (new; interpreter and JIT)

`FixupRoots` runs `FixupSlot` over every operand-stack word and every window word. The comment there
says it "validates the forwarding target so a plain integer that merely aliases the young address
range is not misread". But the check cannot tell an integer from a reference: an integer equal to a
young object's start address passes it.

`GetInstanceID()` returns exactly such an integer (`LOAD_INST_UID` compiles to `LOAD_INST_MEM`). The
reproducer:

```objeck
o := Box->New(i);
before := o->GetInstanceID();
# o->GetInstanceID() is pending (operand stack / spill slot) while Churn allocates
if(Same(o->GetInstanceID(), Churn(i), before) = false) { bad += 1; };
```

Run with `--nursery=64k`, 2,000 iterations, no `native`:

| | `--jit=off` | `--jit=1` |
|---|---|---|
| `-opt s0` | **FAIL**, 242 changed | **FAIL**, 242 |
| `-opt s3` | **FAIL**, 242 | **FAIL**, 242 |

With the default 128 MB nursery it passes, because no minor collection lands in the window. The
2026-10-22 binary fails the same way, so this is not a regression of this cycle.

A related but separate problem: an instance ID is an address, so it changes when the object is
promoted. That is an identity-stability bug, out of scope here.

## 4. The nine patches, traced to the assumption each worked around

| commit | what it fixed | the root-discovery assumption that failed |
|---|---|---|
| 9cdc656566 (#1429-#1431) | the and/or slot was neither marked nor relocated in interpreted and ARM64 JIT frames | "declarations describe every slot". The and/or slot has no declaration; its type depends on the pc. The fix marks it conservatively. |
| 613e68a63f / 8617fda009 | pending values in registers at a loop's safepoint park were lost | "nothing is live in registers at a safepoint". The fix borrows the conservative window by spilling there. |
| 0f0c6b8bfe (#746) | the frame's `INSTANCE_MEM` copy of self went stale after a park | "self has one home". The JIT keeps a second copy that the collector cannot see. |
| 55504a6fcd | ARM64 read every declared slot one word off when slot 0 was missing | "the collector can infer the frame layout from declarations". The JIT laid the frame out from references. |
| 4b67aa39ba | an unreferenced declared local shifted every slot below it | the same assumption, in the other direction |
| 4542ec7e41 (#820) | self "relocated" through a word that was not a forwarding address | "any word can be relocated if it looks forwarded". This is hardening of the guess. |
| 181d478f70 (G5) | a conservative nursery word was read as a class pointer and dereferenced | "a word that looks allocated is an object". The fix validates the guess. |
| 5d92132687 (#880, #881) | a stale window word marked a closure block first, so the typed walk skipped its captures | "conservative and typed marking are interchangeable". A stale window word won the race. |
| 193f076fdd | (a) zero-field objects at the nursery end; (b) a capture block typed `BYTE_ARY_TYPE` holding references | (a) is an allocator bound, unrelated to roots. (b) is the heap-side twin of this design: a reference container without a reference type. |

The framing is confirmed for eight of the nine. Each fix either told the collector about one more slot
kind, or made the conservative guess safer. None gave the collector a precise statement of what is
live. 193f076fdd(a) is a separate allocator fault.

## 5. Root model

### 5.1 Slot kinds today

| slot kind | interpreter | AMD64 JIT | ARM64 JIT | how found today |
|---|---|---|---|---|
| declared locals | `frame->mem[1..]` | `jit_mem` upward, typed | `jit_mem` upward, typed | precise, by declaration |
| and/or slot (id 0) | `frame->mem[1]` | lowest local; also window word 0 | `jit_mem[-1]` | conservative (9cdc656566) |
| self | `frame->mem[0]` | `frame->mem[0]`, plus `INSTANCE_MEM` copy | same | precise for `mem[0]`; the copy is reloaded after every park or callback |
| operand stack | `op_stack[0..pos]`, per thread | values handed to callbacks | same | conservative, and relocated |
| pending values (working stack) | not applicable | registers; spilled to `TMP_REG_0..9` / `TMP_XMM_0..2` at callbacks, loop parks and libm calls | `TMP_X0..5` / `TMP_D0..3` | conservative window |
| scratch saves (div, shift, `RCX` save) | not applicable | `TMP_REG_0/1` | not applicable | conservative window, never cleared |
| saved frame words | not applicable | Win64 window reaches saved `RBP` and the return address; POSIX reaches `OP_STACK`/`STACK_POS` | window covers the caller's `D8-D15` save area | conservative window (harmless today, never meaningful) |
| func-ref locals | two words | two words | two words | precise (`FUNC_PARM`) plus closure declarations |
| closure capture block | old `BYTE_ARY_TYPE` | same | same | typed via `GetClosureDeclarations`, conservative when dirty (193f076fdd) |
| native / trap C++ locals | on the operand stack by convention | bridge into C++ | same | operand stack, conservative |

### 5.2 Target rules

A word is a root if and only if the collector is told it is a reference at the safepoint where the
collection happens. No word is ever relocated on a guess.

1. **Declared locals.** Precise by declaration, as today. Both backends already lay frames out from
   declarations (4b67aa39ba), and entry zeroing makes a not-yet-written slot hold 0. Liveness is not
   required: a dead local that holds a reference is retention, not corruption, and the language makes it
   visible.
2. **The and/or slot.** Typed per pc by the shared analysis (§6.1): a reference where the join merges
   references, an integer otherwise. It is no longer conservative.
3. **Self.** `frame->mem[0]` is the only root. `INSTANCE_MEM` is a derived cache that is never scanned
   and is reloaded after every GC-reachable call. That rule exists today; the design states it as an
   invariant and the verifier checks it.
4. **Operand stacks.** Typed per (method, pc) by the shared analysis. A depth is marked and relocated
   only if it is a reference. Integers are never touched, which fixes §3.3.
5. **Pending values in JIT code.** A reference goes to a reference-only spill slot, which is typed,
   scanned precisely, zeroed at entry and cleared after reload. An integer or float goes to slots the
   collector never scans. This fixes §3.1 and §3.3 in compiled code.
6. **Scratch saves.** These go to non-scanned slots. A scratch value is never a reference the collector
   needs: no collection can happen between a save and its restore inside one instruction.
7. **Saved frame words and float temps.** Never scanned.
8. **Closure captures.** Unchanged for roots. The capture block gets a type of its own in phase 6 (the
   heap side), which retires 193f076fdd's conservative dirty-block path.
9. **Native and trap C++ code.** The convention stays: a reference held across an allocation lives on
   the operand stack. The analysis types an instruction's inputs as of the instruction's start, and a
   trap that pushes intermediate references must declare them. Phase 2 audits the traps; the verifier
   runs report-only until that audit is done.

### 5.3 Conservative scanning afterwards

There is none in frame or stack root discovery. The old scan survives in one place: the audit mode
`OBJECK_GC_ROOTS=audit`, which runs the conservative scan beside the precise one and reports any
allocated word the conservative scan would mark that the precise scan did not. That is the migration
safety net. During phases 1-4 it reports, and it never changes what is marked.

## 6. How live references are reported

### 6.1 One shared analysis: `RefMap` (in `jit_common`)

The bytecode does not carry reference types on the stack. `LOAD_INT_VAR` loads objects and integers
alike, and `Int[]` and object arrays share `INT_TYPE`. So the first piece is an abstract interpretation
over a method's bytecode that tracks, for each operand-stack depth and the and/or slot, one of
{`int`, `ref`, `float`, `unknown`}:

| source | result |
|---|---|
| `LOAD_INT_VAR` | the slot's declared kind |
| `NEW_*` | `ref` |
| `LOAD_INST_MEM` | `ref` |
| `MTHD_CALL` | the callee's return type |
| arithmetic | `int` |
| array element loads | the array's declared element kind, where the declaration knows it |
| joins | the meet of the incoming kinds |

The analysis runs once per method on first need and is cached. It has three consumers: the interpreter
(read lazily at collection time), AMD64 (at compile time) and ARM64 (at compile time).

`unknown` is the open question. Phase 1 measures how often it occurs across the regression suite and the
library before anything depends on it. Any residue gets a compiler-emitted annotation instead of a
guess.

### 6.2 Interpreter: stack maps per (method, pc)

The map is read only when a collection walks the frame, and indexed by the instruction in progress,
`StackFrame::cur_ip` (see the status note at the top). Recording it is one store on the dispatch loop's
default path, which only instructions that can collect take, and one at the safepoint poll.

### 6.3 JIT: three options compared

| | A. stack maps per safepoint | B. clear-after-reload, keep the window | C. typed spill areas (recommended) |
|---|---|---|---|
| mutator cost | one store per GC-reachable call (the site id into a frame word) | one store per spilled value | one store per spilled reference (the clear) |
| precision | exact, including liveness | retention fixed; integers still rewritten (§3.3) | exact for spills; locals by declaration |
| failure mode of a bug | wrong map, so a live object is missed: corruption | missed clear: retention | missed clear: retention; wrong slot class: caught by `roots` verify |
| new machinery | site-id word, per-method tables, lookup in `CheckJitRoots`/`FixupRoots` | none | split `TMP_*` into `TMP_REF_*` and `TMP_VAL_*`; a `RegInstr` ref bit from `RefMap` |
| backends | both, plus table encoding | both | both; frame layout constants change in both `.h` files |

B is the minimum patch. It is rejected as a root model because it keeps the conservative window and
does not fix §3.3.

A is the most general, but a wrong map entry fails toward corruption, and it adds a table format to two
hand-written backends.

C gets the same safety from a smaller change, and every bug it can have fails toward retention.
Recommendation: **C**. A can come later if liveness inside long frames ever measures as a problem.

Under C, the collector walks each JIT frame as: the declared slots (typed), the and/or slot (typed by
`RefMap` at the frame's current pc), then the `TMP_REF_*` slots (typed `ref`, 0 when empty). Nothing
else. The and/or slot needs the pc, and this design has not yet confirmed that a JIT frame records it at
every GC-reachable call. Phase 1 checks this. If it does not, phase 3 adds one store of the instruction
index per such call, which is option A's cost for this one slot only.

## 7. Phases

Each phase is one PR against master. The full regression suite, the GC verifier legs and the stress
tests (`gc_*_stress`, `jit_gc_stress`, `gc_mt_small_nursery_stress`, the nightly `verify-major` and
`--nursery` configurations) stay green. Before/after numbers are recorded in each PR (§8).

| phase | content | fixes | risk |
|---|---|---|---|
| **0** | Fix `--jit=off` so it disables `native` compilation too, or rename its meaning; the differential's `s0/off` depends on it. Add the measurement baseline (§8). | the evidence gap in §2.1 | low |
| **1** | `RefMap` analysis (`core/vm/arch/ref_map.h`, header-only so no build file changes) plus the `OBJECK_GC_ROOTS=audit` report-only mode. Report how often `unknown` occurs per suite. No behaviour change. | nothing yet; measures the design | low |
| **2** | Interpreter operand stacks and the interpreted and/or slot marked and relocated by `RefMap`; audit traps that hold references across allocations. Lands `gc_int_not_relocated` (interpreter configurations). | §3.3 in interpreted code; retires 9cdc656566's interpreter half | medium |
| **3** | AMD64: typed spill areas (`TMP_REF_*` / `TMP_VAL_*`), `RegInstr` ref bit, clear after reload, entry zeroing of the reference slots; `CheckJitRoots`/`FixupRoots` walk them by type on x64. Lands `jit_stale_spill_slot` and the JIT configurations of `gc_int_not_relocated`. | §3.1 paths 1 and 2, §3.3 compiled, on AMD64 | medium-high (frame constants) |
| **4** | ARM64: the same. Compile-checked locally with `cl /Zs` and the cross toolchain; proven on CI's arm64 legs. | the same, on ARM64 | medium-high |
| **5** | Remove `JIT_TMP_LOOK_BACK` and every `CheckUntypedSlot`/`FixupSlot` call on frame and stack words. Delete the dead JIT inliner (`ProcessInlineMethod`, `CanInlineMethod`, `extra_inline_space`). Remove the symptom code made redundant: the conservative and/or marking (9cdc656566) and the window spill comment paths in the loop safepoints (613e68a63f/8617fda009, now `TMP_REF_*`). Demote the conservative-word class validation (181d478f70) and the `FixupSelf` checks (4542ec7e41) to verifier assertions. | the whole class | medium |
| **6** | Heap side: a reference type for closure capture blocks, retiring 193f076fdd's conservative dirty-block path. Separately, stable instance IDs. | 193f076fdd(b); ID identity | medium |

5d92132687's `GC_TRACED_BIT` is kept either way. It makes typed tracing of a closure happen exactly once,
which stays useful with precise roots.

### What v2026.10.2 needs

**Recommendation: none of phases 1-6.** Ship 10.2 without them.

- The spill-slot defect is retention: at most one object per slot per live frame, nothing freed while
  in use.
- The integer rewrite needs an address-valued `Int` to be pending across a minor collection. It is in
  v2026.10.1 and the 10-22 build alike, so it is not a regression of this cycle.
- Phases 3-4 change frame layout constants in both hand-written backends, and phase 4 can only be proven
  on CI's arm64 legs. That is the wrong risk to take the day before a tag.

If one item must go in, take phase 0's `--jit=off` fix. It is small and VM-only, and it makes the
nightly differential's "interpreter" column mean what it says.

The one-line entry-zeroing change would also be safe, but it fixes one path of one symptom, which is the
pattern this design replaces. It stays on the evidence branch unless the user decides otherwise.

## 8. Measurement

- **GC pause.** `OBJECK_GC_STATS=1` (minor/major counts, pause p50/p95/max, promoted bytes, peak RSS) on:
  - `gc_mt_small_nursery_stress`, `jit_gc_stress`, `gc_closure_ids_stress`;
  - `query_expr_concurrent`;
  - CLBG `binarytrees` and `fannkuch` at default and `--nursery=256k`.

  Recorded before and after phases 2, 3 and 5. Precise scanning should lower mark work (no `IsAllocated`
  set lookups per untyped word); the map read is per frame.
- **Codegen.** Per method: code bytes and compile time, added to the `OBJECK_JIT_REPORT` line, summed
  over the regression suite under `OBJECK_JIT_THRESHOLD=1`. Run time: the CLBG suite through the
  `benchmark` skill (median of runs, flagged against `docs/performance.md`). The only new instruction on
  a hot path is the clear after a reference reload at a call site, which is next to a call.
- **Interpreter.** Dispatch is unchanged. The analysis cost is measured once per method and cached, and
  reported as a total per suite run.

## 9. Tests

| test | catches | lands in |
|---|---|---|
| `jit_stale_spill_slot` (rewritten without `native`, so `--jit=off` is a true control; checks both the s0 and s3 paths) | §3.1 | phase 3 (AMD64), extended in 4 |
| `gc_int_not_relocated`, the §3.3 reproducer, all four configurations at `--nursery=64k` | §3.3 | phase 2 (interpreter), phase 3 (JIT) |
| `jit_inline_local_root` | nothing today (§3.2) | not landed; phase 5 deletes the code it would test |
| **`gc_root_coverage`, randomized** (below) | the whole class | phase 1 runs it report-only; it gates from phase 2 |

`gc_root_coverage` generates, from a seed (printed on failure, reproducible with `--seed`), methods
that:

- keep a random mix of `Int` values (including `GetInstanceID()`), objects, arrays and closures pending
  on the stack;
- put them in and/or joins, conditional constructor arguments and loop safepoints;
- allocate between every pair of uses.

It checks every value afterwards: integers unchanged, objects with their fields intact. It runs under
`OBJECK_GC_VERIFY=checkmark` with `--nursery=64k`, at s0 and s3, with `--jit=off` and with
`OBJECK_JIT_THRESHOLD=1`, plus a two-thread variant that parks at loop polls. The fixed seeds run in the
regression suite, and the nightly runs fresh seeds. During phase 1 `OBJECK_GC_ROOTS=audit` must
report zero differences across the suite before phase 2 starts.

## 10. Not established

- **#1443** (`query_url_cache_processes`, Windows only, parent at `s3/jit1`, children get "no response
  from the server"). Neither mechanism here is shown to cause it:
  - retention cannot produce a wrong or missing response;
  - the integer rewrite is not JIT-only and not Windows-only.

  One link to the harness is real: that differential's `s0/off` column is not interpreter-only (§2.1),
  so "passes with the JIT off" has meant less than it appeared. #1443 was not reproduced here.
- **ARM64 behaviour of `jit_stale_spill_slot`.** Inferred from the code (path 2 only), not run. CI's
  arm64 legs will run it when the test lands.
- **When the s0 trigger appeared.** It is absent on 10-22 and present on master, and it was not bisected.
