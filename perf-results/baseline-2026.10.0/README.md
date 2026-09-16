# macOS arm64 measurements, v2026.10.0 hardening

Taken 2026-09-15 on Apple M4 Max (14 cores, macOS 26.6.2 arm64, AC power, sleep held off)
against master `ad2c610bb0` and release `v2026.9.4`, each built from its own clean worktree
with its own dependencies. Report-only: nothing here was merged or pushed to master.

## 1. Perf baselines (plan Phase 0.3) — GATE PASS

`baseline_raw.csv`, `baseline_summary.json`, `addendum_raw.csv`, `addendum_summary.json`

18 benchmarks, ABAB-interleaved between builds, 2 warm-ups discarded, 15 runs each (60 for the
re-measure noted below). No benchmark is >5% slower on master at p<0.01.

| benchmark | master | v2026.9.4 | delta | p |
|---|---|---|---|---|
| binarytrees | 2.0160s | 2.0766s | -2.92% | 3e-06 |
| bench_gc_large_heap | 0.0261s | 0.0268s | -2.30% | 3e-06 |
| bench_gc_churn | 0.1699s | 0.1734s | -2.02% | 4.8e-05 |
| bench_array_intensive | 0.0845s | 0.0819s | +3.11% | 3e-06 |
| bench_matrix_multiply | 0.1979s | 0.2011s | -1.58% | 0.22 (n=60) |

Negative = master faster. Everything else is within ±1%.

**A withdrawn result.** At n=15 bench_matrix_multiply read +8.95% (p=0.042); at n=60 it reads
-1.58% (p=0.22), and master's own median moved 0.2199 -> 0.1979 between samples. Sub-second
benchmarks are under-powered at 15 runs. The driver now sets
`n = clamp(ceil(10s / per-run cost), 15, 101)` from the slower build's warm-up.

**Peak RSS** (compared on `/usr/bin/time -l`, since v2026.9.4 predates the VM's own counter):
bench_gc_large_heap -26.6% (38.6 vs 52.6 MB, matching the ObjectBlockSize fix), binarytrees
-3.2%, and a constant +320 KB floor on every small benchmark (the obr binary is only +67 KB
bigger; the rest is the verifier and stats infrastructure master compiles in).

## 2. Nursery sweep (#841) — recommends 64m

`nursery_sweep.csv` — master only, 4 sizes x 3 benchmarks x 15 runs, 0 failures.

| size | binarytrees | bench_gc_churn | bench_gc_large_heap |
|---|---|---|---|
| 16m | +35.3% time, -48.8% RSS | -2.0% time, -84.1% RSS | +318% time, +136% RSS |
| 32m | +19.5% time, -48.1% RSS | -1.4% time, -72.0% RSS | +317% time, +192% RSS |
| 64m | +6.5% time, -26.2% RSS | -0.7% time, -48.0% RSS | +0.7% time, same RSS |
| 128m | baseline | baseline | baseline |

64m is the only size that is never bad. 16m and 32m are disqualified by a mechanism, not a
preference: on a large live set they are 4.2x slower AND use more memory, because the live set
stops fitting the nursery and survivors get promoted (28 MB promoted against zero at 64m).

Pause p95 on churn scales with nursery size: 665us at 16m to 4759us at 128m — the
throughput-versus-latency trade, for a future release that targets pause.

One machine, one architecture; Windows and Docker sweeps should agree before this ships.

## 3. #861 threaded GC crash — reproduced on arm64, fix verified

`gc_861_arm64_summary.csv`, `gc_861_failures.txt`, `gc_861_crash_reports.txt`

macOS has no per-process affinity, so the pinned Linux configuration is unreachable and the
sequential loop there had no power. Oversubscription reproduces the same starvation, using the
fixture's own constants: `workers = ceil(cpus * 3 / 8)` = 6 copies on 14 cores (~48 threads).

| arm | mode | runs | crashes | rate |
|---|---|---|---|---|
| pre-fix (`ad2c610bb0`) | jit=off | 5000 | 4 | 0.08% |
| pre-fix | jit=1 | 5000 | 7 | 0.14% |
| post-fix (`e864d335fd`) | jit=off | 5000 | 0 | — |
| post-fix | jit=1 | 5000 | 0 | — |

All 11 crashes share one signature: `SIGSEGV KERN_INVALID_ADDRESS at 0x20`, faulting thread
`MemoryManager::CheckPdaRoots+460`, with an exiting thread in `UnregisterMutator` and a
collection in `CollectMinor <- AllocateObject`. `StackFrame::jit_mem` is at offset 0x20, so
every one is `cur_frame->jit_mem` with `cur_frame == nullptr`.

If both arms shared a rate, all 11 landing in one arm has probability 2 * 0.5^11 = 0.00098.
Identical bytecode and identical `lang.obl` (95a0c64e...) on both arms; only the VM differs.

**Scope.** This shows the fix holds on arm64. It does NOT demonstrate the RegisterMutator
synchronisation race or the `*stack_frame = nullptr` hunks — every arm64 crash is the
exiting-thread window that the CheckPdaRoots null guard closes.

The oversubscribed path's detection rate, recorded as unmeasured in 94ab3e25f9, is now
0.11% on 14-core arm64 against 0.26% pinned on Linux x64.

## 4. jit_closure_call_in_creating_frame on arm64 — 12/12 PASS

s0 and s3 x {--jit=off, default threshold, OBJECK_JIT_THRESHOLD=1} x {verifier off, on}.
Not a hollow pass: `OBJECK_GC_STATS=1` shows minor=12 major=12 pauses=24 promoted_objects=484,
one collection per GcBetween iteration. The verifier doubles pause p95 (6294 -> 12162us) with
identical collection counts. So 42ac1763ec's "ARM64 mirrored, untested" is tested.

## Caveat on absolute numbers

Xcode auto-updated to 27.0 at 19:10:19, two minutes before the baseline started at 19:12:29;
load averages were 1.84 / 3.80 / 4.98. ABAB interleaving protects the master-vs-9.4 comparison,
but absolute figures from early in that pass may be slightly inflated.

## Driver

These numbers were produced by the driver that is now `perf-results/objeck_bench.py`
(md5 of the version used here: 5cc32ffc429d0396c7719144c2b8d931). `run_benchmarks.sh` does not
run on macOS: it times with GNU `/usr/bin/time -f`, which BSD time has no flag for. The driver
wraps each run in the platform's own RSS instrument and records the VM's `[gc-stats]` number
alongside it, so the two are never confused for one another.

The version of the driver that produced these numbers is NOT the version in the tree, and the
differences matter if you reproduce this work:

- Its `stress` mode carried the superseded sequential #861 design, which has no power without
  CPU affinity -- the very thing the oversubscription result above establishes. The current
  driver refuses to run that mode rather than emit a misleading clean number.
- Peak RSS was measured only on macOS. On Linux and Windows it fell back to the VM's own
  counter, which does not exist before commit 0a5e7ba008 -- so a v2026.9.4 arm would have had
  no RSS at all. The current driver measures it on all three platforms and records which
  instrument produced each value.
- It had no `.exe` handling, so it could not locate `obc`/`obr` on Windows at all.

The macOS numbers here are unaffected by any of that: the macOS RSS path is the one that
worked, and none of the four items used `stress` mode.
