# ARM64 float-JIT investigation (macOS / Apple Silicon)

Companion to PR #548 (`fix/jit-floor-ceil-atan`). The AMD64 fixes there are
complete and correct; this plan is for the **ARM64-only** failure that the new
regression test surfaced in CI.

## What happened

PR #548 fixed two AMD64 float-codegen bugs (swapped Floor/Ceil `ROUNDSD`
immediates; missing `ATAN_FLOAT` case) and added
`programs/regression/jit_float_round_trig.obs`. That test:

- **passes on AMD64** (default threshold and `OBJECK_JIT_THRESHOLD=1`), but
- **fails on ARM64** in CI — `macos-arm64` / `linux-arm64` build jobs:
  `jit_float_round_trig — runtime error (exit 1)` at the **default** threshold.

`exit 1` is the test's own `Runtime->Exit(1)` on a failed assertion (a wrong
*value*, not a crash). So ARM64 **miscompiles** one of: Floor, Ceil, ArcTan, or
the chained `ArcSin+ArcCos+ArcTan` expression, once the helper auto-JITs (the
helpers are each called 25× > the threshold of 10).

## What is already ruled out (by code inspection on the Windows box)

These ARM64 encodings/handlers were verified correct, so the bug is *not* a
simple swap/missing-case like AMD64 had:

- Floor → `frintm` `0x1E654000`, Ceil → `frintp` `0x1E64C000`
  (`core/vm/arch/jit/arm64/jit_arm_a64.cpp` `floor_freg_freg` ~3396,
  `ceil_freg_freg` ~3413) — round-down vs round-up are **not** swapped.
- `ATAN_FLOAT` is handled: `ProcessFloatOperation` maps `func_ptr = atan`
  (`jit_arm_a64.cpp` ~4261).

## Prime suspect: FP register clobber across chained libc calls

`JitArm64::ProcessFloatOperation` (`jit_arm_a64.cpp:4223`) is the unary
transcendental handler (sin/cos/tan/asin/acos/atan/...). Before the libc call it
saves **only `D0`**:

```
move_freg_mem(D0, TMP_D0, SP);   // save D0 only (if holder != D0)
move_mem_freg(left, SP, D0);     // arg -> D0
move_imm_reg(func_ptr, X9); call_reg(X9);   // <-- AAPCS64: clobbers D0-D7, D16-D31
move_mem_reg(TMP_X0, SP, X9);
move_freg_freg(D0, holder);      // result
```

It does **not** spill the other live working-stack FP values. In
`ArcSin(0.5) + ArcCos(0.5) + ArcTan(1.0)` the first results sit in
caller-saved `D` registers while the next `ProcessFloatOperation` issues another
`call`, which per AAPCS64 may clobber `D0–D7` and `D16–D31` → the earlier partial
sums are corrupted → wrong total. This matches "single arc call is fine, chain is
wrong," which is what we expect the FAIL line to show.

(AMD64 doesn't hit this because its `call_xfunc` path spills appropriately; the
AMD64 chain test passes.)

If confirmed, the fix is to **spill all live working-stack FP registers (and any
caller-saved GP regs) before the call and reload after** — i.e. treat
`ProcessFloatOperation`'s libc call like the other callbacks do
(`FlushLocalCache` / full caller-saved spill), not just `D0`. Compare with how
`ProcessStackCallback` / `call_xfunc`-equivalent paths preserve state on ARM64.

## Step-by-step on macOS

Build the VM (xcode or `make -f make/Makefile.arm64`), deploy, then:

1. **Get the exact FAIL line** (tells you which op is wrong):
   ```bash
   cd programs/regression
   OBJECK_LIB_PATH=<deploy>/lib <deploy>/bin/obc -src jit_float_round_trig.obs \
       -lib cipher,collect,xml,json -opt s3 -dest /tmp/jfrt.obe
   OBJECK_LIB_PATH=<deploy>/lib <deploy>/bin/obr /tmp/jfrt.obe                       # default
   OBJECK_LIB_PATH=<deploy>/lib OBJECK_JIT_THRESHOLD=1 <deploy>/bin/obr /tmp/jfrt.obe # force JIT
   ```
   - `FAIL: arc chain = ...`  → confirms the register-clobber theory (fix
     `ProcessFloatOperation` spilling).
   - `FAIL: ArcTan(1.0) = ...` (single) → atan call/marshalling bug.
   - `FAIL: Floor(3.14) = 4` / `Ceil...` → frintm/frintp or `As(Int)` (FCVTZS)
     F2I bug; check `ProcessFloatRound` (~4343) and the F2I codegen.

2. **Minimal repros** (each a method called 25× so it JITs at default; also try
   `OBJECK_JIT_THRESHOLD=1`):
   ```objeck
   # single
   class T { function : Main(a:String[])~Nil { C()->PrintLine(); }
     function : C()~Float { return Float->ArcTan(1.0); } }
   # chain (the suspect)
   class T { function : Main(a:String[])~Nil { C()->PrintLine(); }
     function : C()~Float { return Float->ArcSin(0.5)+Float->ArcCos(0.5)+Float->ArcTan(1.0); } }
   # two-call (does clobber start at 2?)
   class T { function : Main(a:String[])~Nil { C()->PrintLine(); }
     function : C()~Float { return Float->ArcSin(0.5)+Float->ArcCos(0.5); } }
   ```
   Compare each `OBJECK_JIT_THRESHOLD=1` vs `OBJECK_JIT_DISABLE=1` (= expected).
   The smallest N that breaks pinpoints the clobber.

3. **Debugger (lldb)** — for a miscompile (wrong value, not a crash) the FAIL
   line + repros are usually enough. If you do need to inspect codegen, the
   Windows workflow was `gdb` + `llvm-symbolizer` on the ASLR'd PDB; the macOS
   equivalent is `lldb -- <deploy>/bin/obr /tmp/jfrt.obe`, `run`, and on stop
   `disassemble`/`register read` — but a value bug is better chased via the
   repros above than single-stepping JIT'd code.

4. **Fix** (if register clobber): in `ProcessFloatOperation` (and check the
   binary `ProcessFloatOperation2` for pow/atan2/mod, and `ProcessFloatRound`),
   spill every live working-stack FP register to its stack slot before
   `call_reg`, and reload after — mirror whatever the MTHD_CALL/callback path
   already does on ARM64. Keep `D0` handling as-is.

5. **Verify**: `jit_float_round_trig.obs` passes at default **and**
   `OBJECK_JIT_THRESHOLD=1`; then the full suite
   (`programs/regression/run_regression.sh`) stays green (expect the usual
   `core_opencv` only). Pay attention to `math_trig_funcs`, `math_log_exp`,
   `jit_native_math` — same code path.

## If it is NOT the chain (Floor/Ceil/As(Int))

Then ARM64 has its own Floor/Ceil/F2I issue despite the correct frintm/frintp
encodings — check `ProcessFloatRound` (~4343) operand handling and the
`F2I`/`FCVTZS` codegen for the floored value, and whether the `->As(Int)` of a
negative floored value truncates vs rounds.

## Scope note

The AMD64 fixes in #548 are independent and correct. If the ARM64 fix is small,
fold it into #548; otherwise land #548 for AMD64 (temporarily marking the test
`# JIT_DISABLE` or AMD64-gating the arc-chain check) and fix ARM64 in a
follow-up. The test itself is good — it caught a real ARM64 discrepancy.
