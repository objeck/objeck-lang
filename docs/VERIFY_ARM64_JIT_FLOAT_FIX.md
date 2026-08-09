# ARM64 JIT float register bug — resolution record

> **RESOLVED and on master** (`a60371477`). This was written as a verification
> procedure; it is kept as the record of what the bug actually was, because the
> answer is more interesting than the fix and the same shape can recur.

Very likely the cause of the long-open *"BUG IN CLIENT OF LIBMALLOC: memory
corruption of free block"* investigation
(`docs/jit-malloc-corruption-investigation.md` on
`investigate/jit-malloc-corruption`).

## One line, two different bugs

`cmp_mem_freg` ended with a `move_freg_freg(holder, dest)` that should never
have been there — a compare produces flags, not a value, and `dest` still holds
the live left-hand operand. AMD64 always agreed: `cmp_mem_xreg` is a bare
`ucomisd` that never writes `dest`.

That one line presented completely differently before and after the encoding
was corrected:

| | `move_freg_freg` encoding | effect of the trailing move |
| --- | --- | --- |
| **before** | `fmov d10, x{src}` + `fmov x{dest}, d10` — addressed the **general** register file, because the `Register` enum restarts float numbering at `D0 = 0` | inert on the FP file, but **stray-clobbered general register `X{dest}`** on every call. When that register held a live pointer, the next use followed a float bit pattern as an address → heap corruption |
| **after** | `fmov d{dest}, d{src}` (correct) | the copy became **real** — and since this path is reached from *every JIT float division* via the divide-by-zero guard (`div_freg_freg` → `CheckFloatDivideByZero` → `cmp_imm_freg` → `cmp_mem_freg`), it copied the `0.0` constant back into the **divisor**. Every division computed `x / 0.0` |

So the corruption and the regression were the same line seen from two sides.
Removing it fixed both. `move_freg_freg` now has no live callers.

The fix also corrected the compare's **operand order** to
`cmp_freg_freg(holder, dest)` — the `dest ? mem` sense used by `cond_jmp`/`cmov`
and by AMD64 — and removed the dead, divergent `math_imm_freg` duplicate that
had been hiding the divergence.

## Why it hid for so long

The investigation had already, correctly, ruled out the GC, the write barrier,
moving collection, stack overflow, and wild store bases, converging on *"a valid
base and a valid in-bounds offset — what is wrong is the **VALUE**"*, with a
crash PC of `0x3F947AE147AE147B` (the IEEE-754 bits of the double `0.02`). That
description fits a clobbered general register exactly.

It stayed hidden because the damage was **collateral**: the wrong register was
usually dead, so nothing visibly misbehaved. It only surfaced as corruption when
`X{dest}` happened to hold a live pointer — which is why the repro needed a
32 KB young generation and `OBJECK_JIT_THRESHOLD=1` to raise allocation density
until the odds caught up.

## The lesson worth keeping

**A wrong instruction that is also inert produces no symptom until something
else makes it live.** Correcting the encoding was right, and it immediately
broke `ml_phase1_test` on both ARM64 legs — which looked like a regression but
was the latent bug finally becoming observable. The CI gate refusing to merge
on that failure is what forced the second, real fix.

Also: AMD64 was correct throughout (`movsd xmm,xmm`, bare `ucomisd`). When one
backend diverges from the other on something this basic, the divergence itself
is the signal.

## Guard

`programs/regression/jit_float_mem_ops.obs` — gating, runs on `linux-arm64` and
`macos-arm64`. Division is the primary trigger, so it asserts `DivLit`,
`DivVar` (divisor in a register, must survive the guard) and non-commutative
`SubLit`/`DivVar` forms that would catch an operand-order reversal. All values
are binary-exact so equality comparison is reliable.

The first version of this guard was **not** sufficient: it exercised
compare-against-memory but never re-read the register afterwards, so it passed
while the bug was live. A guard that cannot fail is decoration — the division
cases are what give it teeth.
