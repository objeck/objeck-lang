# Verifying the ARM64 JIT float-move fix on Apple Silicon

The fix in `core/vm/arch/jit/arm64/jit_arm_a64.cpp` (`move_freg_freg`) is
verified by disassembly and compiles on every CI leg, but the **crash it is
believed to cause has never been reproduced in CI**. Closing the
`investigate/jit-malloc-corruption` investigation needs one run on ARM64
hardware.

CI cannot do this, for three specific reasons:

1. the repro requires a **source edit** (`YOUNG_REGION_SIZE`), which CI never makes;
2. `programs/deploy/2d_game_13.obs` is **never run** by any CI job;
3. it is **probabilistic** — a few failures per ~10 runs — so a single green
   CI run proves nothing.

## Step 1 — the cheap check first (2 minutes, no source edits)

Before the stress repro, confirm the new deterministic guard passes natively:

```sh
cd programs/regression
./run_regression.sh arm64          # or just: the suite runs jit_float_mem_ops
```

`jit_float_mem_ops.obs` exercises float arithmetic and comparison against
**memory operands** — the exact shape that routes through `move_freg_freg` —
in a loop hot enough to be JIT-compiled, asserting exact values.

**This is the interesting one.** Under the bug the float move silently did not
happen, so this test should have produced a **wrong value**, not a crash. If it
passes on ARM64 before the fix, my analysis is incomplete and the real trigger
is narrower than I think — that is worth knowing either way.

## Step 2 — the original stress repro

From the investigation handoff:

1. `core/vm/arch/memory.h`: set `#define YOUNG_REGION_SIZE (32 * 1024)`
   (normally 128 MB). This makes GC and allocation churn constant, which is
   what makes the corruption detectable.
2. Build the ARM64 VM (macOS uses the Xcode project,
   `core/vm/xcode/VM.xcodeproj` — **not** `Makefile.arm64`).
3. Run, forcing every method through the JIT:

```sh
cd programs/deploy
for i in $(seq 1 30); do
  OBJECK_JIT_THRESHOLD=1 ../release/deploy/bin/obr 2d_game_13.obe > /dev/null 2>&1
  echo "run $i -> exit $?"
done
```

4. **Restore `YOUNG_REGION_SIZE` to 128 MB afterwards.**

### Reading the result

- **Before the fix:** several runs abort with exit 133/134/138/139 —
  `BUG IN CLIENT OF LIBMALLOC: memory corruption of free block`, or
  `EXC_BAD_ACCESS`/SIGBUS with `PC = 0x3F947AE147AE147B` (the IEEE-754 bits of
  the double `0.02`, i.e. a float being followed as a code pointer).
- **After the fix:** all 30 runs should exit 0.
- `OBJECK_JIT_DISABLE=1` was always clean and still should be — that is the
  control, and it is what established the bug was JIT codegen rather than the
  GC.

30 runs matters. The original was "a few of ~10", so a clean 10 is weak
evidence and a clean 30 is reasonable.

## If it still crashes

The fix is correct on its own merits — the old encoding provably addressed the
wrong register file (capstone: `fmov d10, x3` / `fmov x5, d10` for a requested
`D3 → D5` move) — so keep it regardless. But a remaining crash means there is
a second source, and the handoff doc lists the other leads: any path where an
`IMM_FLOAT`/`REG_FLOAT`/`MEM_FLOAT` working-stack entry is consumed by an
integer or reference store without a conversion.

The doc's instrumentation gotchas are worth re-reading before writing another
validator — several traps there have already been hit once.

## Related

- Fix: `core/vm/arch/jit/arm64/jit_arm_a64.cpp`, `move_freg_freg`
- Investigation: `docs/jit-malloc-corruption-investigation.md` on
  `investigate/jit-malloc-corruption`
- Guard: `programs/regression/jit_float_mem_ops.obs` (gating, runs on
  linux-arm64 and macos-arm64)
