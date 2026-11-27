# JIT: operand-stack values in registers, and the inline allocator's result

**Status:** assessment and one change, 2026-11-26, for v2026.11.4. The question was whether
the JIT should cache the top entries of the VM operand stack in registers. Sections 1 to 3
are the answer: it already does, for every entry, and they write down the rules that make
that safe, which until now lived only in the code. Section 4 is the one place where compiled
code still took a value through operand-stack memory on a fast path, the AMD64 inline
allocator, and the change that removed it. Section 5 has the measurements and section 6
what was looked at and left alone.

**Limitations that exist today.** The change is AMD64 only: ARM64 has no inline allocator
(every `NEW_OBJ_INST` is a callback there), so it had no such round trip to remove. A
`NEW_OBJ_INST` reached with fewer than two free pool registers keeps the old sequence.
`OBJECK_JIT_ALLOC_REG=0` restores the old sequence everywhere.

## 1. What "operand-stack caching" would be, and why there is nothing to add

A template JIT that translates one bytecode at a time pushes and pops the VM's operand stack
in memory between consecutive instructions, and the standard fix is to hold the top one to
three entries in registers. Neither Objeck backend is that kind of JIT. Each walks the
bytecode once with a **working stack** of `RegInstr` values that exists only at compile time
(`working_stack` in `jit_amd_lp64.h` and `jit_arm_a64.h`). A `LOAD_INT_LIT` pushes an
immediate and emits nothing; a `LOAD_LOCL_INT_VAR` pushes a reference to the local's frame
slot (`MEM_INT`) and emits nothing; an `ADD_INT` pops two entries, emits the add into a
register and pushes that register (`REG_INT`). No instruction between two bytecodes writes or
reads the operand stack's memory, however deep the expression: the cache is the whole
expression stack, not its top three entries.

The disassembly of `TreeNode:BottomUpTree` in `binarytrees` (section 5) shows it. Its only
references to the frame's `OP_STACK` and `STACK_POS` words are the ones listed in section 3.

## 2. The rules that keep the working stack safe

These are the invariants the existing code keeps. Any change to either backend's handling of
pending values has to keep them too.

1. **A collection sees no register.** The collector scans memory: the operand stack up to
   its count, each interpreter frame, and each compiled frame's declared locals and typed
   spill slots (`jit_frame_layout.h`). So before every point where a collection can start or
   this thread can park, each pending register is stored to a frame slot and reloaded after
   (`SpillPending` / `ReloadPending`). The points are: a bridge callback (`ProcessStackCallback`:
   allocation, traps, conversions, threading, calls that are not native sites), a native call
   site (`EmitNativeCallSite`), and the loop-header poll's slow path (`EmitJitSafePoint`).
   The method's reference map (`arch/ref_map.h`) says which pending values are `Int` at that
   instruction; those go to `TMP_VAL_*` slots, which are never scanned, and everything else
   goes to `TMP_REF_*` slots, which are scanned and relocated. An imprecise map therefore
   costs retention, never a missed root.
2. **A libm call spills but is not a GC point** (`SpillPending(..., false)`): the registers are
   caller-saved, so they are stored and reloaded, without the and/or temp's typed copy.
3. **A block boundary sees one state.** At a label, a jump and a jump table the local
   register cache is dropped (`FlushLocalCache`; it writes through, so nothing is stored) and
   every deferred load still on the working stack is loaded into a register
   (`MaterializeWorkingStack`, #1256), so a value pending across the boundary is in the same
   register on every path into the join. Statements end with an empty working stack, so in
   practice only a conditional expression carries a value across a label. With
   `OBJECK_JIT_REPORT=1` both backends audit the register pool at every label.
4. **A guard stub needs nothing flushed.** The nil, bounds and divide-by-zero stubs and a
   callee's error status branch to the method's teardown with a status; no pending value is
   read again, and the teardown pops the frame record.
5. **Scratch saves are not roots.** `TMP_SCR_*` slots hold registers saved inside one
   instruction (around a division, a shift, the inline allocator). Nothing between the save
   and the restore can collect, so the collector never reads them.

## 3. Where compiled code does touch operand-stack memory

| Where | Why | Can it go? |
|---|---|---|
| Arguments of a bridge callback (`ProcessReturn(params)`) and its result (`ProcessIntCallParameter` and its Float and func-ref siblings) | `JitStackCallback` and `JitDirectCall` are C++: they take their operands from the operand stack and leave the result there | Only by giving the C++ bridge another protocol for each of its thirty opcodes. Measured bound in section 5: not worth it |
| The bridge entry and exit of a compiled method (`ProcessParameters`, `ProcessReturn`) | the interpreter calls a compiled method with its arguments on the operand stack | No: that is the interpreter's calling convention. A compiled caller uses the native entry, which does not touch the operand stack |
| The slow path of a native call site | the callee is not compiled yet, the call stack is full, or the receiver is `Nil`: the outgoing area is copied to the operand stack and the bridge runs | Cold |
| `RTRN` of a native entry when a callback left the value on the operand stack (`CPY_CHAR_ARY` + `RTRN`) | the value was never on the working stack | Rare |
| **The inline allocator's result (AMD64, until v2026.11.4)** | the inline path pushed the pointer so it could share the callback's pop | **Yes: section 4** |

## 4. The inline allocator's result (AMD64)

`NEW_OBJ_INST` for a small object (up to `ZERO_UNROLL_MAX` words to clear) is a bump
allocation emitted inline (`EmitNewObjectInline`), with the bridge callback as the path taken when the nursery is full.
The two paths joined *before* the result was read, so the inline path had to leave the
pointer where the callback leaves it:

```
; inline path, after the object header is written (RDX = the new pointer)
mov  rax, [rbp+OP_STACK]
mov  rbx, [rbp+STACK_POS]
mov  ecx, [rbx]
shl  rcx, 3
add  rax, rcx
mov  [rax], rdx               ; op_stack[count] = pointer
inc  qword [rbx]              ; count += 1
...
; join (both paths)
mov  r, [rbp+STACK_POS]
dec  qword [r]                ; count -= 1
mov  r32, [r]
shl  r, 3
add  r, [rbp+OP_STACK]
mov  r, [r]                   ; r = op_stack[count]
```

Thirteen instructions on the path every small allocation takes, in a dependency chain through
two read-modify-writes of the count and a store-to-load forward of the pointer itself. Around
the allocation, `RAX`-`RDX` (its scratch) were saved to `TMP_SCR_0..3` and restored on both
exits whether or not they held anything: eight more memory operations.

Now:

- `NEW_OBJ_INST` flushes the local cache and takes the result's register `R` from the free
  pool **before** emitting either path, when the pool has at least two free registers.
- The inline path ends `mov R, rdx` (nothing when `R` is `RDX`) and jumps past the callback
  *and* its pop. Only the callback path pops, into the same `R`
  (`ProcessIntCallParameter(result)`).
- Of `RAX`-`RDX`, only a register that holds a pending value is saved and restored. A
  register in the free pool holds nothing: the working stack does not name it, and the local
  cache was just flushed (and writes through in any case). `R` is not saved either; it is
  about to be overwritten on both paths.

Why this keeps section 2's rules:

- *Rule 1.* The pointer is in `RDX`, then `R`, with no call and no poll in between, exactly as
  it was after the old pop; from the join on it is an ordinary `REG_INT` working-stack entry,
  spilled to a reference slot at the next GC point like any other. On the callback path `R`
  is not on the working stack while the callback runs, so it is not spilled; it holds nothing
  then, and the pop sets it after `ReloadPending`.
- *Rule 3.* No label is involved. The two paths meet with the same registers holding the same
  values: the pending ones restored (inline path) or reloaded (callback path), and `R` set on
  both.
- *Rule 5.* The scratch saves still bracket a sequence with no call in it.
- The operand stack's count is the same on both paths at the join (the callback's push is
  popped; the inline path never pushed). The old inline path left a stale copy of the pointer
  above the count; nothing read it.

Two cases keep the old sequence, byte for byte:

- **Fewer than two free pool registers.** `R` is held across the callback, and the callback
  takes a scratch register of its own to reload `self`. Requiring one to spare means a method
  that compiled before still compiles: a `String` array literal of seven elements reaches a
  `NEW_OBJ_INST` with seven of the pool's eight registers pending, the shape this release's
  pool-pressure fixes were made for (the comment in `ProcessIntCallParameter`).
- **`OBJECK_JIT_ALLOC_REG=0`**, read once per process (`JitAllocResultInRegister` in
  `jit_common.h`), for telling a problem in this sequence from anything else.

ARM64: nothing to do. Its `NEW_OBJ_INST` is `ProcessStackCallback` followed by the pop; there
is no inline path whose result could skip the operand stack. An inline allocator for ARM64
is a separate piece of work.

## 5. Measurements

All numbers compare three arms run interleaved: the VM built from the parent commit
(`68637fb7d3`, "before"), this change ("after"), and this change with
`OBJECK_JIT_ALLOC_REG=0` ("knob off", the same binary emitting the old sequence).

### 5a. Emitted code

`--jit=1`, every method compiled, bytes summed from `OBJECK_JIT_DUMP`'s index (Windows x64):

| program | methods | allocation sites | before | after | |
|---|---|---|---|---|---|
| binarytrees | 23 | 9 | 40,448 | 40,010 | -1.1% |
| nbody | 20 | 6 | 35,830 | 35,398 | -1.2% |
| fannkuchredux | 20 | 3 | 31,444 | 31,285 | -0.5% |
| spectralnorm | 12 | 0 | 18,220 | 18,220 | 0 |
| mandelbrot | 26 | 6 | 39,687 | 39,303 | -1.0% |
| fasta | 22 | 30 | 64,281 | 62,265 | -3.1% |
| `bench_gc_churn` | 5 | 2 | 6,729 | 6,609 | -1.8% |
| `vm_jit_equiv` (the equivalence fixture) | 90 | 86 | 252,468 | 247,104 | -2.1% |
| `jit_alloc_result_reg` | 46 | 77 | 127,195 | 123,625 | -2.8% |
| all twelve programs measured | | | 703,875 | 689,652 | **-2.0%** |

A site's code can only shrink: the new inline path drops seven instructions and adds at most
one register move, and the saves go from four to at most four. With the knob off the
emitted code is the parent VM's: the 275 methods of eight programs (the six CLBG programs
but spectralnorm, `vm_jit_equiv`, `jit_alloc_result_reg` and `jit_call_probe`) were compared
byte for byte against the parent VM's dump and differ only inside the 64-bit immediates that
hold a run's addresses.

Compile time did not move: a whole run of `vm_jit_equiv` with every method compiled (90
methods) takes 28 ms before and 27 ms after, best of fifteen. The change adds one pool lookup
per scratch register at each `NEW_OBJ_INST` and emits seven instructions fewer.

### 5b. Run time, Windows x64 (smoke)

Ryzen 9 7950X3D, Windows 11, release build of `obr` from each commit, the same `.obe` for
every arm, best of seven interleaved runs. **The machine was shared with other work, so read
these as a smoke test**, not as the acceptance measurement; 5c is the Linux counterpart.

| program | before | after | knob off | after / before |
|---|---|---|---|---|
| binarytrees 17 | 1.858 s | 1.814 s | 1.877 s | **0.976** |
| `bench_gc_churn` | 0.163 s | 0.142 s | 0.159 s | **0.871** |
| `bench_gc_large_heap` 18 | 0.035 s | 0.033 s | 0.035 s | 0.95 (35 ms: noise) |
| 30M `Holder->New(i)` in a loop | 0.377 s | 0.346 s | 0.379 s | **0.920** |
| `jit_alloc_result_reg` | 0.164 s | 0.152 s | 0.160 s | **0.927** |
| `bench_method_dispatch` | 0.286 s | 0.285 s | 0.287 s | 0.997 |
| nbody 50M | 5.537 s | 5.613 s | 5.521 s | 1.014 (see 5d) |
| fannkuchredux 11 | 2.352 s | 2.329 s | 2.338 s | 0.990 |
| spectralnorm 5500 | 2.275 s | 2.285 s | 2.254 s | 1.004 |
| mandelbrot 4000 | 4.057 s | 4.054 s | 4.053 s | 1.000 |
| `bench_array_intensive` | 0.090 s | 0.090 s | 0.090 s | 0.996 |
| `bench_matrix_multiply` 500 | 0.173 s | 0.171 s | 0.176 s | 0.988 |
| `bench_tco` | 0.127 s | 0.127 s | 0.129 s | 0.999 |
| `bench_spectralnorm_native` 2000 | 0.254 s | 0.252 s | 0.251 s | 0.995 |

The five other `perf/` programs run for 20 to 60 ms, mostly process start-up, and moved by
up to 3% in both directions with the knob on and off alike; they say nothing either way.
fasta was not timed on Windows (its 250 MB of output takes minutes through the Windows
console path).

The loop of 30 million allocations puts a number on the round trip: 12.6 ns per object
before, 11.5 ns after, so about **1 ns per allocation**. The same loop allocating a
one-element `Int` array, which is a bridge callback, costs about 100 ns per iteration on
either VM.

### 5c. Run time, Linux x64

The same machine under WSL2 (Ubuntu 24.04, `deploy_posix.sh x64` builds of both commits), best
of seven interleaved runs. Not a dedicated box either; 5d is what settles the rows that moved.

| program | before | after | knob off | after / before |
|---|---|---|---|---|
| binarytrees 17 | 1.701 s | 1.604 s | 1.669 s | **0.943** |
| `bench_gc_churn` | 0.126 s | 0.108 s | 0.126 s | **0.862** |
| `bench_gc_large_heap` 18 | 0.020 s | 0.019 s | 0.020 s | 0.96 (20 ms) |
| `bench_method_dispatch` | 0.259 s | 0.267 s | 0.260 s | 1.030 (see 5d) |
| nbody 50M | 4.938 s | 4.865 s | 4.935 s | 0.985 |
| fannkuchredux 11 | 2.097 s | 2.082 s | 2.108 s | 0.993 |
| spectralnorm 5500 | 2.023 s | 2.008 s | 2.026 s | 0.992 |
| mandelbrot 4000 | 0.434 s | 0.436 s | 0.438 s | 1.005 |
| fasta 25M | 6.311 s | 6.293 s | 6.282 s | 0.997 |
| `bench_array_intensive` | 0.071 s | 0.071 s | 0.071 s | 1.001 |
| `bench_matrix_multiply` 500 | 0.143 s | 0.143 s | 0.142 s | 0.995 |
| `bench_tco` | 0.136 s | 0.139 s | 0.136 s | 1.020 (see 5d) |
| `bench_spectralnorm_native` 2000 | 0.220 s | 0.219 s | 0.217 s | 0.996 |

`bench_loop_invariant`, `bench_cse`, `bench_strength_ext`, `bench_copy_prop` and
`bench_dead_code` (10 to 40 ms each) were within 1.5% in both directions.

### 5d. Code placement, and the rows that moved without a reason

Three rows moved by 1.4% to 3% although the code they spend their time in has no allocation
and is byte-identical before and after: nbody on Windows (slower), nbody on Linux (faster),
and `bench_method_dispatch` and `bench_tco` on Linux (slower; `bench_method_dispatch`
allocates once, outside its loop, and `bench_tco` never). What changed for them is where
that code sits. Compiled methods are placed back to back with no alignment, so a method
compiled earlier that is now 72 bytes shorter moves every later one.

To measure the change apart from placement, a scratch build added a knob that puts *n* bytes
before every compiled method, and each program was run at eight paddings with the old
sequence and the new one (Linux, pinned to one core, best of five, times relative to the old
sequence at padding 0):

| program | sequence | 0 | 8 | 16 | 24 | 32 | 40 | 48 | 56 | mean |
|---|---|---|---|---|---|---|---|---|---|---|
| `bench_method_dispatch` | old | 1.000 | 1.006 | 1.048 | 1.011 | 1.003 | 1.001 | 1.023 | 1.040 | 1.016 |
| | new | 1.032 | 1.013 | 1.026 | 1.036 | 1.022 | 1.015 | 1.019 | 1.016 | 1.022 |
| `bench_tco` | old | 1.000 | 1.002 | 0.997 | 0.762 | 0.997 | 0.997 | 1.000 | 0.761 | 0.940 |
| | new | 0.995 | 1.001 | 1.013 | 0.756 | 1.007 | 1.010 | 0.992 | 0.761 | 0.942 |
| nbody 20M | old | 1.000 | 0.973 | 0.998 | 1.003 | 0.994 | 1.021 | 0.996 | 0.984 | 0.996 |
| | new | 0.977 | 0.993 | 0.987 | 1.013 | 0.994 | 0.995 | 0.992 | 0.999 | 0.994 |
| binarytrees 16 | old | 1.000 | 0.989 | 0.995 | 0.981 | 0.979 | 1.003 | 1.003 | 0.993 | 0.993 |
| | new | 0.962 | 0.963 | 0.942 | 0.957 | 0.945 | 0.971 | 0.975 | 0.950 | 0.958 |
| `bench_gc_churn` | old | 1.000 | 0.987 | 1.041 | 0.995 | 0.985 | 1.001 | 1.011 | 1.003 | 1.003 |
| | new | 0.848 | 0.846 | 0.871 | 0.859 | 0.847 | 0.874 | 0.863 | 0.866 | 0.859 |

Averaged over the eight placements, new against old: `bench_method_dispatch` 1.006,
`bench_tco` 1.003, nbody 0.998, binarytrees **0.965**, `bench_gc_churn` **0.857**. The three
programs that do not allocate in their hot code are unchanged, and each swings more between
placements of the *same* sequence (4.8%, 24% and 4.8%) than it moved in 5b or 5c. The two that
allocate are faster at every placement. The same sweep on Windows, nbody only and the old
sequence only, gave 1.000 to 1.030 across the eight paddings, with the parent's layout the
fastest of them, which is the whole of nbody's 1.4% in 5b.

This is a property of the JIT as it stands, and any change that alters the length of an
earlier method re-rolls it in either direction; `bench_tco` runs 24% faster at two of the
eight paddings. It is listed in section 6.

## 6. Looked at and left alone

- **A register protocol for bridge callbacks.** Passing a callback's operands and result in
  registers would remove the remaining operand-stack traffic of section 3's first row, at
  the cost of touching every opcode `JitStackCallback` handles. Each such callback already
  pays for an eleven-argument C++ call, an opcode switch and the operation itself: about
  100 ns for an array allocation, against the 1 ns a round trip measured at in section 5b.
- **Caching `&op_stack[count]` in a register.** It would shorten each marshalling sequence
  by three instructions and need a register reserved in every method and refreshed after
  every call. Same bound: a fraction of a nanosecond on a 100 ns path.
- **An inline allocator on ARM64.** Out of scope here; it is the larger gain on that backend
  and needs hardware in the loop.
- **Aligning method entries and loop heads.** Section 5d: the same bytes run up to 5% apart
  depending on where they land, and 24% apart in `bench_tco`. Aligning each method's start and each loop head would take
  that lottery out of every later measurement. It changes the placement of all compiled
  code at once, so it needs its own before-and-after over the whole benchmark set, on a quiet
  machine, and is not part of this change.
