# JIT code-generation efficiency: AMD64 measured, ARM64 by inspection (2026-09-08)

> **Status:** assessment only; nothing here changes code. Measurements are from
> the v2026.9.1 tree (master `8e18e45ee4`) on Windows x64 with an
> instruction-tracing build of `obr` (`_DEBUG_JIT`, built to a scratch
> directory) and the shipped `obr`. ARM64 has no hardware in this loop, so its
> findings come from reading `jit_arm_a64.cpp` against the same code paths; the
> probe should be run on an ARM64 leg before any of the ARM64 claims are acted on.

## 1. Method

Six kernels in `programs/tests/jit_probe.obs`, each a method with one hot loop,
timed once under `--jit=1` (compile on first call) and once under `--jit=off`
with 20M iterations; then compiled with the tracing build so the emitted
instruction stream of each method could be read. Two follow-up kernels isolate
single costs (`Size()` inside a loop condition; division by a constant). The
`native` keyword was deliberately not used: it forces compilation even under
`--jit=off`, which made the first attempt at this comparison measure the JIT
against itself. The committed fixture nevertheless declared every kernel `native` until
2026-09-10, and section 5b records what that did on ARM64; it no longer does, and its
header says why.

## 2. Headline numbers

| kernel | interpreter | JIT | speedup | JIT per iteration | emitted instructions in the loop |
|---|---|---|---|---|---|
| `IntLoop` — `sum += (i*7) % 13 - i/3` | 0.493 s | 0.069 s | 7.1× | 3.5 ns | ~33, two `idiv` |
| `ArraySum` — `for i < a->Size(): total += a[i]` | 0.528 s | 0.133 s | 4.0× | 6.7 ns | ~22 + an interpreter callback |
| `ArraySum` with `Size()` hoisted out of the loop | 0.431 s | **0.014 s** | 31× | **0.7 ns** | ~22 |
| `FloatDot` | 0.841 s | 0.117 s | 7.2× | 5.8 ns | callback + 2 bounds-checked loads |
| `CallLoop` — `acc := Small(acc, i)` | 0.515 s | 0.012 s | 43× | 0.6 ns | no call: `obc -opt s3` inlined `Small` at the bytecode level |
| `Branchy` — three `%` tests per iteration | 1.209 s | 0.058 s | 21× | 2.9 ns | 3 `idiv`, boolean temp through memory |
| `Locals` — 8 loop-carried scalars, 8 adds | 2.248 s | 0.027 s | 83× | 1.35 ns | 56 memory operands in 92 instructions |
| `IntLoop` without division (`and 15`, `>> 1`) | 0.441 s | **0.009 s** | 49× | **0.45 ns** | |

Two numbers carry the assessment: the array loop is **10× faster when
`Size()` leaves the loop condition**, and the integer loop is **6.4× faster
without its two constant divisions**. Neither is a change a user should have to
make; both are the JIT's to fix.

### 2b. AMD64 re-measured at `26784fe1ab` (2026-09-10)

The same box as section 2 (Ryzen 9 7950X3D, Windows x64), the same fixture as section 5a
(no `native`), medians of three runs each. Three columns because the older VM was rebuilt
and run on the same probe: the interpreter as it is today, the JIT as it was at section 2's
commit `8e18e45ee4` (compile on first call), and the JIT after batches 1-6.

| kernel | interpreter | JIT at `8e18e45ee4` | JIT now | batches 1-6 | JIT now vs interpreter |
|---|---|---|---|---|---|
| `IntLoop` | 0.805 s | 0.053 s | 0.025 s | 2.1x | **32x** |
| `ArraySum` | 0.773 s | 0.105 s | 0.019 s | 5.5x | **41x** |
| `FloatDot` | 1.055 s | 0.111 s | 0.022 s | 5.0x | **48x** |
| `CallLoop` | 0.698 s | 0.018 s | 0.015 s | 1.2x | **47x** |
| `Branchy` | 1.133 s | 0.055 s | 0.034 s | 1.6x | **33x** |
| `Locals` | 2.220 s | 0.036 s | 0.027 s | 1.3x | **82x** |

Every kernel returned an identical result in all three configurations, so, like 5a, this
doubles as an equivalence check across two VM vintages and the interpreter.

Two things this table corrects in section 2. Its interpreter column is not reproducible:
the VM at `8e18e45ee4`, rebuilt today, interprets `IntLoop` in 0.66 s, not 0.49 s, and today's
VM in 0.81 s, so the interpreter has not regressed by the amount a naive comparison of the
two sections suggests -- the spread is build-to-build, and this table is the AMD64 reference
from here on. And at `8e18e45ee4` the *default* threshold never compiled these kernels at all:
each is called once, so the interpreter ran them, 0.68 s for `IntLoop` under "JIT default".
That is the case batch 6a fixed (`JIT_ENTRY_COMPILE_DESIGN.md`); today the default and
compile-on-first-call columns are the same numbers.

Read this beside 5a with the usual caveat: different hardware, so the two tables are each a
baseline against itself, at the same commit.

## 3. What the emitted code looks like

`IntLoop`'s loop, as emitted (AMD64, Windows), annotated:

```
26 mov  -104(%rbp),%rcx        ; i            <- loaded from the frame
27 cmp  -88(%rbp),%rcx         ; i < n        (compare fused with the branch: good)
28 jge  exit
29 mov  -104(%rbp),%rcx        ; i            <- loaded AGAIN (cache dropped by the compare)
30 mov  $3,%rbx
31 test %rbx,%rbx              ; divide-by-zero check on the CONSTANT 3
32 je   <err>
33 mov  %rax,-32(%rbp)         ; save rax/rdx to spill slots (neither is live)
34 mov  %rdx,-40(%rbp)
35 mov  %rcx,%rax
36 idiv %rbx                   ; i / 3
37 mov  %rax,%rcx
38 mov  -32(%rbp),%rax         ; restore
39 mov  -40(%rbp),%rdx
40 mov  -104(%rbp),%rbx        ; i            <- loaded a THIRD time
41 imul $7,%rbx
42 mov  $13,%rax
43 test %rax,%rax              ; zero check on the constant 13
44 je   <err>
45..51                         ; save, idiv (mod), restore: 7 more instructions
52 mov  -96(%rbp),%rax         ; sum          <- from memory
53 add  %rbx,%rax
54 sub  %rcx,%rax
55 mov  %rax,-96(%rbp)         ; sum          -> to memory
56 mov  -104(%rbp),%rax        ; i            <- from memory
57 inc  %rax
58 mov  %rax,-104(%rbp)        ; i            -> to memory
   (label; safepoint poll: cmp byte [r12],0 / je -- 2 instructions, fine)
```

Thirty-three instructions and twelve memory operands per iteration for work an
optimizing compiler emits in about eight with `i` and `sum` in registers and no
`idiv`. The interpreter/JIT ratio of 7× is real, but the code is running at
roughly a quarter of what the same instruction selection with sane register
residency would give.

`ArraySum`'s inner loop adds the interpreter callback for `a->Size()`:

```
38..50  push the array onto the VM operand stack (9 instructions)
51..64  mov $28,%rcx ; movabs instr,%rdx ; mov 16(%rbp),%r8 ; mov 24(%rbp),%r9
        push $19 ; push 72(%rbp) ; push 64(%rbp) ; push 56(%rbp) ; push 48(%rbp) ; push 40(%rbp)
        sub $32,%rsp ; movabs JitStackCallback,%r10 ; call %r10 ; add $80,%rsp
65..74  read the result back off the VM operand stack (10 instructions)
76      cmp %rdx,%rax ; jge                  ; i < size
79      test %rax,%rax ; je <err>            ; nil check on the array (needed)
82..91  mov (%rax),%rcx ; shl $3,%rdx ; shl $3,%rcx ; test %rdx,%rdx ; jl <err>
        cmp %rcx,%rdx ; jge <err> ; add $24,%rdx ; add %rdx,%rax ; mov (%rax),%rax
92..94  total += via memory (3)     95..100  i++ via memory (3), r++ via memory (3)
```

`LOAD_ARY_SIZE` is a plain bytecode — the count is word `[0]` of the array,
one load — and both backends hand it to the interpreter through
`ProcessStackCallback`, which marshals the operand stack, calls C++, and
marshals back: ~35 instructions plus the dispatch, per loop test.

## 4. Findings, ranked by expected gain per unit of work

| # | Finding | Backends | Evidence | Fix | Size |
|---|---|---|---|---|---|
| F1 | **`LOAD_ARY_SIZE` goes through the interpreter callback.** Every `for(i := 0; i < a->Size(); ...)` pays ~35 instructions and a C++ call per iteration. | both (`jit_amd_lp64.cpp:987`, `jit_arm_a64.cpp:940`) | 10× on `ArraySum` when hoisted | nil-check + `mov (%reg),%reg` / `ldr`; the same shape as `ProcessLoadIntElement` | ~15 lines each |
| F2a | **Divide-by-zero check on immediate divisors.** `test`/`je` (AMD64) and `CheckIntDivideByZero` (ARM64) run at runtime for a constant the compiler already knows is non-zero. | both | every constant `/` and `%` | skip when the immediate ≠ 0 | trivial |
| F2b | **`idiv` for constant divisors, with unconditional RAX/RDX save/restore.** Power-of-two is strength-reduced; anything else is `idiv` plus four `mov`s to spill slots whether or not the registers are live. ARM64 materializes the constant and issues `sdiv` (no clobber, but ~12–20 cycles). | both | 85% of `IntLoop`, 3 of them in `Branchy` | magic-number multiply (`mul` + shifts; the Granlund–Montgomery / libdivide recipe) for constant divisors; on AMD64 save RAX/RDX only when live | ~80 lines each |
| F3 | **Locals live in memory across every basic block.** The register cache is dropped at each label and jump (write-through), so a loop-carried scalar is reloaded up to three times per iteration and stored after every assignment; `i += 1` is load/inc/store. | both (same `FlushLocalCache` policy) | 56 memory operands in `Locals`' 92 instructions; `i` loaded 3× in `IntLoop` | pin hot loop-carried scalars in callee-saved registers for the loop's extent, spill only around callbacks; needs liveness over the loop, which the pre-scan already walks | the big one: 1–2 weeks; likely 2–3× across ordinary code |
| F4 | **AMD64 allocates from four GPRs** (`RAX RBX RCX RDX`; `jit_amd_lp64.cpp:6429`). `RSI RDI R8–R11 R13 R14` are unused (`R12` holds the STW flag, `R10`/`R15` are call scratch). ARM64 has twelve (`X0–X7`, `X12–X15`). | AMD64 | any expression with more than four live temporaries spills; F3 cannot be done without it | add the callee-saved set to the pool, push/pop them in the prologue/epilogue | 1 day |
| F5 | **Array addressing: nine instructions where four do.** Index and bound are both shifted, then a negative check, then the bound compare, then `add $24; add; mov (%rax)`. | both | every element access | `cmp idx,[arr]` (unsigned compare handles negatives) then `mov 24(%arr,%idx,8)` / `ldr x, [x, x, lsl #3]` with the header offset folded | ~40 lines each |
| F6 | **Boolean connectives round-trip through a stack temp.** `a & b` in a condition materializes `b` with `cmov` into a temp local, stores it, reloads it, tests it, branches (`Branchy` 54–61). The front end emits the temp; the JIT executes it literally. | front end + both | ~8 instructions per `&`/`\|` in conditions | short-circuit branches in the bytecode emitter, or a JIT peephole for `STOR temp; LOAD temp; JMP` | 1 day |
| F7 | **Every method entry/exit rebuilds the VM operand-stack view** (~25 instructions of prologue glue, ~10 of epilogue), and JIT-to-JIT calls (`MTHD_CALL_JIT`) still marshal arguments through the VM stack. | both | fixed cost per call; invisible in `CallLoop` only because `obc` inlined the callee | a native calling convention between JIT'd methods (arguments in registers, VM stack synced only at callbacks) | weeks; do after F3 |
| F8 | **`JMP_TABLE` (`select`) is not compiled**, so a method containing a `select` runs entirely in the interpreter (noted in the JIT README this week). | both | any parser/state machine/dispatch loop written with `select` | emit a jump table (`lea`/indirect `jmp` on AMD64, `adr`+`br` on ARM64) | 2 days |
| F9 | **Whole-method fallback is silent.** One unsupported opcode in a method (`CanJitInstruction`: 118 of 146 opcodes accepted on ARM64; AMD64 equivalent) returns the whole method to the interpreter and nothing reports it. | both | unknown in real programs — that is the point | `OBJECK_JIT_REPORT=1`: print each rejected method and the opcode that rejected it | 0.5 day |
| — | Compare-and-branch fusion (`cmp`/`jge`), the loop-header safepoint poll (`cmp byte [r12],0`/`je`; `ldarb`/`cbz`), constant folding, power-of-two division, `imul`-by-constant, `inc`/`dec` selection, and the Windows-ABI shadow space are all correct and tight. | both | | none | |

The callbacks other than F1 are allocation (`NEW_*`, `ZERO_*`, `CPY_*`), calls,
traps, threads and casts — all reasonable to leave with the interpreter for now.

## 5. ARM64 specifically

Structurally the ARM64 backend is the AMD64 backend with the ISA swapped: the
same working-stack model, the same local cache and flush points, the same
callback set (identical opcode list), the same array-index shape. So F1, F2a,
F3, F5, F6, F8 and F9 apply as written. Two differences matter:

- Its **register pool is twelve wide**, so F4 is AMD64-only and ARM64 will
  benefit more from F3 sooner.
- `move_imm_reg` synthesizes with `movz`/`movk` (good) and falls back to a
  literal load for wide constants; division has no register clobber (`sdiv`),
  so F2b is smaller there but the ~15-cycle latency still argues for magic
  numbers.

### 5a. The ARM64 baseline, measured (2026-09-09)

Measured on macOS ARM64 at `f9d777fc41` (batches 1-5 merged): Apple M4 Max,
10P+4E, macOS 26.6.2, `obc`/`obr` 2026.9.1 from `deploy_macos_arm64.sh`.

| kernel | interpreter | JIT | speedup |
|---|---|---|---|
| `IntLoop` — `sum += (i*7) % 13 - i/3` | 0.560 s | 0.018 s | **30.6×** |
| `ArraySum` — `for i < a->Size(): total += a[i]` | 0.569 s | 0.019 s | **30.0×** |
| `FloatDot` | 0.798 s | 0.026 s | **31.1×** |
| `CallLoop` — `acc := Small(acc, i)` | 0.626 s | 0.020 s | **31.9×** |
| `Branchy` — three `%` tests per iteration | 0.850 s | 0.024 s | **35.0×** |
| `Locals` — 8 loop-carried scalars, 8 adds | 1.150 s | 0.046 s | **24.9×** |

Every kernel returned an identical result in both modes, so this doubles as an
equivalence check.

**Do not read these against section 2's AMD64 column.** That table was taken on
a different machine, and before batches 1-5 landed; the two differ in hardware
and in commit, so a ratio between them measures neither. This table is a
baseline for ARM64 against itself — the thing later ARM64 work is compared to.

### 5b. `jit_probe.obs` cannot be timed interpreted as written

Its kernels are declared `function : native :`, and **`native` bypasses every
JIT-disable switch**: `--jit=off` and `OBJECK_JIT_DISABLE=1` turn off *auto*-JIT
(`jit_common.h`), while a `native` method is compiled through a path neither
reaches. Timing the fixture as committed therefore compares compiled against
compiled, and reports it as an interpreter number:

| | `--jit=off` | default | apparent "speedup" |
|---|---|---|---|
| `IntLoop`, as committed (`native`) | 0.0209 s | 0.0177 s | 1.2× |
| `IntLoop`, `native` removed | 0.5595 s | 0.0183 s | 30.6× |

The 1.2× is not a slow JIT, it is two JIT runs. The table in 5a was produced
from a copy with `function : native :` rewritten to `function :`. Since 2026-09-10
the committed fixture is that copy: no kernel is `native`, and a header on the file
says what `native` does to a timing so nobody puts it back. Section 2b was measured
from it as committed. Since section 5 asks for a baseline that "every later number
needs", getting this wrong silently invalidates everything downstream.

Adding `jit_probe.obs` to the perf gate (currently linux-x64 only) would keep
this measured rather than sampled — with the same caveat baked into whatever
runs it.

## 6. What this means for v2026.9.1

> **Superseded on 2026-09-09.** The maintainer chose to land the ranked items before the tag, with the verification this section asks for; see section 8 for what shipped and how it was checked. The paragraph below is left as written.

Nothing here goes into 9.1. Every item changes emitted code and needs the
interpreter/JIT equivalence fixture green on all five legs, which is a release
cycle's worth of verification, not a pre-tag change. The JIT in 9.1 is
*correct* — this week's fixes (IMUL operand order, native-call spills, 64-bit
immediates, `PatchCallSites`) were about that — and it is 4–80× faster than the
interpreter on these kernels. It is also leaving roughly 3–10× on the table on
the most common loop shapes.

## 7. Suggested order for the next release's VM track

Items 1-3 below are done (section 8); the open ones are 4-6.

1. **F1 + F2a** — an afternoon; the array-loop 10× and free removal of dead checks.
2. **F9** — the report mode, so the next items are chosen from real programs' fallbacks rather than from kernels.
3. **F4 then F2b then F5** — each a day; independent of each other.
4. **F6** — in the front end, where it also helps the interpreter.
5. **F3** — the structural change, with F4 done first; expect it to be the release's headline VM number.
6. **F8**, then **F7**.

Keep `programs/tests/jit_probe.obs` (and its `_plain`/`2` variants) as the
before/after harness: run it under `--jit=1` and `--jit=off`, and check the
emitted streams with a `_DEBUG_JIT` build (`CL=/D_DEBUG_JIT`, built to a
scratch `OutDir`; it prints per call at runtime, so use a small iteration count
for listings — the 20M-iteration run wrote a 3.6 GB file).

## 8. Status, 2026-09-09

Landed on `master` before the v2026.9.1 tag, in three PRs:

| PR | Findings | Also |
|---|---|---|
| [#731](https://github.com/objeck/objeck-lang/pull/731) | F1 (`Size()` inlined, both backends), F2a (no zero check on constant divisors), F9 (`OBJECK_JIT_REPORT=1`) | Windows saves `XMM10`-`XMM15` in the prologue; AMD64 `>>` emitted `SHR` (logical) where the interpreter and ARM64 shift arithmetically -- wrong for every negative operand, in every release; fixed to `SAR` at all three sites |
| [#732](https://github.com/objeck/objeck-lang/pull/732) | F2b (magic-number division, both backends; the recipe checked against exact division for 480,000 cases first), F5 (one-instruction array addressing, both backends) | |
| [#733](https://github.com/objeck/objeck-lang/pull/733) | F4 (`R8`-`R11` join the AMD64 pool; ten spill slots) | AMD64 treated `TRY_START`/`TRY_END` (what `?->` desugars to) as no-ops on the assumption that calls were never compiled, so a nil receiver under `?->` in a JIT-compiled caller exited the process; such methods now run in the interpreter, as on ARM64 |

Measured on the kernels of section 2 (Windows x64, `--jit=1`, the pre-batch `obr` against batch 2; batch 3 changes these little, its gain is the expressions that no longer spill or fall back):

| kernel | before | after | |
|---|---|---|---|
| IntLoop (two constant divisions per iteration) | 0.063 s | 0.025 s | 2.5x |
| ArraySum | 0.191 s | 0.023 s | 8x |
| FloatDot | 0.219 s | 0.064 s | 3.4x |
| Branchy (three `%` per iteration) | 0.073 s | 0.043 s | 1.7x |

Verification, per batch: `vm_jit_equiv.obs` byte-identical between interpreter and JIT (with new probes for `Size()`, multi-dimensional `Size()`, constant divisors of every shape, `>>` on negatives, and an expression with more than four live values), the VM flag tests, the regression suite, and the regression suite with `OBJECK_JIT_THRESHOLD=1` (every method compiled on first call -- the run that exposed the try-region bug, which every earlier binary fails). All five CI legs were green on the combined tree; the macOS and Linux ARM64 legs were the first execution of the ARM64 halves of F2b and F5.

Done since, as batches 4 and 5 (2026-09-09): **F6** (boolean temporaries, front end), **F3** on AMD64 (loop-carried locals in `R13`-`R15`; ARM64 still open), **F8** (`select` jump tables, both backends; `docs/JIT_SELECT_TABLES_DESIGN.md`). Since then: floats pin in `XMM6`-`XMM9` on Windows (batch 6). Open: **F3** on ARM64 (with `D8`-`D15` for floats), **F7** (calling convention). The ARM64 kernel timings of section 5 are still unmeasured. Two things learned on the way: a stacked PR does not get the build legs (`ci-build.yml` runs only for PRs that target master), and `OBJECK_JIT_REPORT=1` should be run on the fixture itself -- its own division probe had been falling back to the interpreter.

Found later the same day, by the first compiled run of `core_thread_gc_stress` in CI (#745 made the runners match the opt-out marker as a whole line; the test's own comment had matched as a substring since June): the loop-header poll's one omission. A park there did not refresh the frame's `self` the way a callback's return does, so a young `self` that another thread's collection promoted went stale for the rest of the loop -- 35-45 corrupted values on every CI leg, none on a 32-thread box until pinned to four cores. Fixed on both backends ([#746](https://github.com/objeck/objeck-lang/issues/746)); the test runs compiled again.

2026-09-10: the ARM64 baseline is in (5a), the AMD64 side is re-measured at the same commit
(2b), and the probe fixture is no longer `native` (5b). What the two tables agree on: the JIT
is 25-48x the interpreter on every kernel but `Locals` (82x on AMD64), results identical in
every mode. Open on ARM64: F3, `D8`-`D15`, and the section 5 findings by inspection.

Later the same day, F7 was measured for the first time (`JIT_CALLING_CONVENTION_DESIGN.md`,
fixture `programs/tests/jit_call_probe.obs`): a compiled-to-compiled call cost 26 ns against
51 ns interpreted, so a call-bound loop gained 2.8x from the JIT where the same loop without
the call gained 43x, and a compiled caller of a `virtual` method was *slower* than the
interpreter (125 ns against 70) because the bridge re-entered a fresh interpreter per call.
Phase 1 of that design -- the C++ bridge: virtual callees resolved there, a per-thread frame
pool zeroed on acquire, a bound callee passed by pointer -- brought a compiled call to 16.5 ns
and a virtual one to 20 ns. Open: phases 2 (callee prologue/epilogue) and 3 (register
arguments, a second native entry per method).
