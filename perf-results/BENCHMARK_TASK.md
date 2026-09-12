# Benchmark Task: re-measure `docs/performance.md` for v2026.9.1

**For:** the session on the AMD Ryzen 9 7950X3D box.
**Written:** 2026-09-12, from the Zephyrus (x64 laptop), which cannot do this job —
wrong CPU for the baseline, and it cannot even build the tree (the repo targets
toolset v145, that box has VS 2022 v143).

---

## Why this exists

`docs/performance.md` carries measured tables from **2026-06-21, against v2026.6.2**.
Four releases have landed since. The largest of them changed how compiled code calls
compiled code:

| | before | after |
|---|---|---|
| bound call (AMD64) | 26.5 ns | **5.5 ns** |
| `virtual` call (AMD64) | 125 ns | **6.0 ns** |
| `Fib(32)` (AMD64) | 0.208 s | **0.043 s** |

So every call-dominated row on that page is wrong, and wrong in the *pessimistic*
direction. A banner now says so, and this task is what removes the banner.

**The numbers were deliberately left unchanged** rather than guessed at or
substituted from other hardware. Do not treat "update the page" as licence to
estimate anything. Either a number is measured on that box in a unified run, or it
does not go on the page.

---

## Read first

- `docs/performance.md` — especially the banner and the P3 roadmap table.
- `perf-results/docker/Dockerfile` — what the unified run actually builds.
- The traps section below. Three of them have already cost someone a day.

---

## Step 0 — the trap that would silently corrupt the whole run

**`Dockerfile:39` copies the committed `.obl` files instead of rebuilding them:**

```dockerfile
COPY core/lib/*.obl core/lib/
```

The image builds a fresh `obc` and `obr` from current source, then runs them against
**standard libraries compiled on 2026-09-04** — before the recent compiler work. That
is a v2026.9.1 VM measured against v2026.9.0-era libraries, and nothing in the output
would look wrong.

This is confirmed, not theoretical: on 2026-09-12, rebuilding `diags.obl` from
identical source with identical flags produced a file **48 bytes smaller** than the
committed one.

**Rebuilding restores comparability rather than breaking it.** The 2026-06-21 baseline
used committed `.obl` that were current *for v2026.6.2*. The like-for-like condition
for v2026.9.1 is libraries current for v2026.9.1.

So, in WSL, before building the image:

```bash
cd core/compiler && sh update_version.sh amd64
git status --porcelain core/lib/
```

**Record whether any `core/lib/*.obl` changed.** That answer belongs in the results —
it tells us how stale the committed set had drifted. If they changed, the rebuilt ones
are what should be measured, and they should probably be committed separately (their
own PR, not mixed into the numbers PR).

---

## Step 1 — the run

Same host as the baseline, or the cross-language rows stop being comparable to their
own history. That is the entire reason this task is pinned to that box.

```bash
git checkout master && git pull
docker build -t objeck-bench -f perf-results/docker/Dockerfile .
docker run --rm -v "$(pwd)/perf-results/docker-results:/results" objeck-bench
```

Record the **commit SHA** you measured. It goes on the page.

---

## Step 2 — what to look at hardest

`spectralnorm` is the interesting one, not just the stale one.

The page currently argues its 44.86 s "is a JIT-warmup artifact, not raw capability,"
and that the remaining lever is *threshold tuning, not coverage*. **That entire reading
was written when a closure call cost 125 ns.** It now costs ~6 ns, and func-ref sites
carry an inline cache. The warmup argument may simply have evaporated.

Do not copy that paragraph forward. Re-derive it from the new numbers:

- If default-threshold `spectralnorm` is now close to its `OBJECK_JIT_THRESHOLD=1`
  time, the warmup story is dead and should be deleted, not softened.
- If the gap persists, it is a genuinely different finding than it was in June and
  needs its own explanation.

Also expect movement in `binarytrees` and `fannkuchredux` (both call-bound). If
`binarytrees` is *still* the weak spot, that strengthens the P1 allocation-throughput
case rather than weakening it — say so plainly.

---

## Step 3 — updating the page

- Replace the measured tables; keep the structure.
- Delete the staleness banner at the top — it exists only until this run lands.
- Stamp the **Objeck version and commit SHA beside every table**, not just in the
  footer. The absence of that is precisely how the page went four releases without
  anyone noticing it was stale (it is P3's second item).
- Rewrite every *reading* that the new numbers no longer support. The prose asserts
  specific ratios ("7.5x faster than Python", "~16x", "3.8x faster than LuaJIT") —
  each one needs recomputing, not adjusting by feel.
- Update both the footer dates: prose *and* tables.

Then open a PR. **Merges happen only on the maintainer's explicit word** — the gate is
the five build legs, `Tools (formatter, LSP, VS Code extension)` and `CI Status` all
green, then `gh pr merge N --merge --delete-branch`. Never `gh pr merge --auto`: this
repo has no auto-merge, so `--auto` merges *immediately*.

---

## Traps, all previously paid for

1. **Check AC vs battery before trusting any timing.** Windows throttles CPU on DC and
   this workload is exactly the CPU-bound shape that suffers most. A run once took an
   hour on battery and read convincingly as a livelock.
2. **Do not estimate run counts from an assumed per-run time.** A "~130 runs" figure
   was once derived from an assumed 15 s/run; one run actually took an hour, so every
   progress number was inflated.
3. **A hung-looking process is not a hang until the threads say so.** Use
   `analyze_hang.py`, which buckets threads by module+offset and distinguishes blocked
   (deadlock), shared-RIP (spin) and dispersed (real work). `analyze_dump.py` reads an
   exception record and is useless on a hang dump.
4. **Identical numbers across two configurations are indistinguishable from a
   configuration that never applied.** This bit two sessions in one night. If two runs
   come back the same, prove the knob reached the process — time a workload known to be
   sensitive to it — before reporting them as two results. On 2026-09-12,
   `OBJECK_JIT_THRESHOLD=1` vs `--jit=off` on `jit_call_overhead.obe` gave 9065 ms vs
   1012 ms; that is what a knob that works looks like.
5. **Verify a measurement harness can detect a failure before trusting a clean run.**
   A loop using `start /affinity F /b /wait` reported a perfect score while running
   nothing, because that form does not propagate the child's exit code.

---

## Do not

- Put any number on the page that was not measured in this run on this box.
- Mix the `.obl` rebuild (if it changes files) into the numbers PR.
- Carry forward a *reading* without re-deriving it from the new numbers. The prose is
  the part most likely to be quietly wrong, because it survives a table replacement
  unchanged.

---

## Send back

- `perf-results/docker-results/` output.
- The commit SHA measured.
- Whether any `core/lib/*.obl` changed in step 0.
- Whether the machine was on AC.
