# v2026.10.0 hardening measurements

Sections 1, 3 and 4 are macOS arm64 only. Section 2, the #841 nursery sweep, covers three
platforms: macOS arm64, Linux x64 in Docker, and Windows x64 native.

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

All 18 benchmarks, from `baseline_summary.json` and `addendum_summary.json` (positive delta = master slower; `bench_matrix_multiply` is the 15-run sample that was later withdrawn, see below):

| benchmark | master median (s) | v2026.9.4 median (s) | delta | p | runs |
|---|---:|---:|---:|---:|---:|
| nbody | 5.6610 | 5.6585 | +0.05% | 0.533829 | 15 |
| binarytrees | 2.0160 | 2.0766 | -2.92% | 3e-06 | 15 |
| spectralnorm | 2.8918 | 2.8893 | +0.09% | 0.678302 | 15 |
| fannkuchredux | 37.3564 | 37.3008 | +0.15% | 0.229029 | 15 |
| fasta | 11.4846 | 11.4754 | +0.08% | 0.648204 | 15 |
| mandelbrot | 2.2122 | 2.2174 | -0.23% | 0.678302 | 15 |
| bench_loop_invariant | 0.0139 | 0.0138 | +0.56% | 0.198506 | 15 |
| bench_cse | 0.0176 | 0.0175 | +0.72% | 0.171069 | 15 |
| bench_strength_ext | 0.0491 | 0.0487 | +0.90% | 0.051239 | 15 |
| bench_gc_churn | 0.1699 | 0.1734 | -2.02% | 4.8e-05 | 15 |
| bench_gc_large_heap | 0.0261 | 0.0268 | -2.30% | 3e-06 | 15 |
| bench_array_intensive | 0.0845 | 0.0819 | +3.11% | 3e-06 | 15 |
| bench_matrix_multiply | 0.2199 | 0.2018 | +8.95% | 0.042111 | 15 |
| bench_method_dispatch | 0.3722 | 0.3722 | -0.01% | 0.340086 | 15 |
| bench_copy_prop | 0.0580 | 0.0581 | -0.08% | 0.771551 | 15 |
| bench_dead_code | 0.0512 | 0.0514 | -0.38% | 0.383733 | 15 |
| bench_spectralnorm_native | 0.4881 | 0.4845 | +0.74% | 0.506915 | 15 |
| bench_tco | 0.1450 | 0.1452 | -0.10% | 0.319506 | 15 |

Inputs are the ones in `perf-results/run_benchmarks.sh` (nbody 50,000,000; binarytrees 17; spectralnorm 5500; fannkuchredux 12; fasta 25,000,000; mandelbrot 4000; `bench_gc_large_heap` 18; `bench_matrix_multiply` 500), copied into the driver's `CLBG` and `PERF` tables. The `p` column is `mann_whitney_p` in `perf-results/objeck_bench.py` (two-sided Mann-Whitney U, normal approximation with tie correction, on the two builds' per-run wall times); the delta is the difference of the medians over the v2026.9.4 median. Two warm-up runs per build are discarded and the two builds' runs alternate (ABAB).

**A withdrawn result.** At n=15 bench_matrix_multiply read +8.95% (p=0.042); at n=60 it reads
-1.58% (p=0.22), and master's own median moved 0.2199 -> 0.1979 between samples. Sub-second
benchmarks are under-powered at 15 runs. The driver now sets
`n = clamp(ceil(10s / per-run cost), 15, 101)` from the slower build's warm-up.

**Peak RSS** (compared on `/usr/bin/time -l`, since v2026.9.4 predates the VM's own counter):
bench_gc_large_heap -26.6% (38.6 vs 52.6 MB, matching the ObjectBlockSize fix), binarytrees
-3.2%, and a constant +320 KB floor on every small benchmark (the obr binary is only +67 KB
bigger; the rest is the verifier and stats infrastructure master compiles in).

## 2. Nursery sweep (#841) — three platforms; default stays 128m

All three legs use the same method: tree `ad2c610bb0`, 4 sizes x 3 benchmarks x 15 runs after
2 warm-ups, 0 failures on each.

| file | platform | driver |
|---|---|---|
| `nursery_sweep.csv` | macOS arm64, M4 Max | Mac driver (md5 5cc32ffc…) |
| `nursery_sweep_linux_x64_docker.csv` | Linux x64, Docker on the 7950X3D | `objeck_bench.py` at commit `5710c77800` (branch `hardening/bench-driver`) |
| `nursery_sweep_windows_x64.csv` | Windows x64 native, same 7950X3D | `objeck_bench.py` at commit `5710c77800` (branch `hardening/bench-driver`) |

Machine state for the x64 legs: `machine_state_nursery_linux_x64_docker.txt` and
`machine_state_nursery_windows_x64.txt`. The x64 CSVs carry a `peak_rss_source` column: `gnu_time`
for Docker, `win_psapi` for Windows.

Change vs the 128m default (time / peak RSS):

| | macOS arm64 | Linux x64 (Docker) | Windows x64 |
|---|---|---|---|
| binarytrees 16m | +35.3% / -48.8% | +81.2% / -54.3% | +163.3% / -55.5% |
| binarytrees 32m | +19.5% / -48.1% | +35.2% / -45.2% | +69.8% / -49.0% |
| **binarytrees 64m** | **+6.5% / -26.2%** | **+10.5% / -31.0%** | **+25.8% / -27.8%** |
| bench_gc_churn 64m | -0.7% / -48.0% | -9.2% / -46.6% | -2.4% / -47.0% |
| bench_gc_large_heap 16m | +319% / +136% | +355% / +87% | +1040% / +117% |
| bench_gc_large_heap 32m | +318% / +192% | +279% / +142% | +991% / +160% |
| bench_gc_large_heap 64m | +0.9% / 0% | +2.2% / +0.7% | +2.3% / 0% |

**16m and 32m are ruled out on every platform.** The live set stops fitting the nursery, so
28 MB gets promoted where 64m and 128m promote nothing. bench_gc_large_heap is then several times
slower *and* uses more memory.

**64m is not "never bad" on Windows.** It costs +6.5% on macOS and +10.5% on Linux, but +25.8% on
Windows. The cause is promotion, not collection count. bench_gc_churn runs 25 extra minor
collections that promote nothing and pays nothing anywhere, while the cost per promoted MB is
11–34 ms on Windows, 3–10 ms on Linux x64 and 2.5–5 ms on macOS. **On the same hardware,
Windows promotion is ~3.5x slower than Linux**, and the binarytrees p95 pause is ~87 ms natively
vs ~31 ms in Docker. Tracked as #871.

**Decision: keep the 128m default for now.** Fix Windows promotion cost first (#871), then
re-measure 64m. A default change that slows binarytrees by a quarter on Windows does not
belong in a hardening release.

Pause p95 on churn still scales with nursery size on every platform (e.g. Windows 1376 us at 16m
to 7378 us at 128m). That is the throughput-versus-latency evidence for a future release that
targets pause time.

Absolute binarytrees at 128m: macOS 2.03 s, Linux x64 Docker 1.96 s (docs/performance.md
documents 2.14 s for x64 Docker, so the container was not core-capped: nproc=32), Windows native 2.59 s.

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

## Limits of this evidence (threats to validity)

- **One Apple M4 Max for sections 1, 3 and 4**, one 7950X3D for the x64 legs of section 2: no Linux x64 native, no x64 comparison of master against v2026.9.4, no ARM64 Linux or Windows.
- **Section 1 compares two Objeck builds, not languages**, and says nothing about absolute speed relative to other runtimes. It can only show that master is not slower than v2026.9.4 on these 18 programs; two of them (`bench_spectralnorm_native`, `bench_tco`) and `bench_matrix_multiply` at n=60 come from the addendum files.
- **Sub-second benchmarks are under-powered at 15 runs** (the withdrawn `bench_matrix_multiply` result is the example); the 0.014-0.05 s rows (`bench_loop_invariant`, `bench_cse`, `bench_strength_ext`) are close to the timer's resolution and their deltas of 0.5-0.9% should be read as ties, which is also how the table's p-values (0.05 to 0.2) read.
- **Section 2's x64 legs ran 15 runs after 2 warm-ups with adaptive run counts off** (`machine_state_nursery_*.txt`), on machines recorded as idle apart from the interactive sessions named there (the Windows file records `adaptive off`, the Docker file does not say); the Docker leg's engine was upgraded during the setup and its image build restarted on the new engine (that file).
- **Section 3 shows a fix holds on arm64; it is not a statement about x64**, and the README's own "Scope" paragraph says which hunks it does not exercise.
- **Report-only**: nothing was committed to master with this data, and no CI job re-runs it. The benchmark inputs are not enforced against this driver by `check_benchmark_inputs.py` (that check reads `run_benchmarks.sh`).

## Reproduce

```
python perf-results/objeck_bench.py baseline --master <deploy dir> --base <v2026.9.4 deploy dir> --repo <checkout> --out <dir>
python perf-results/objeck_bench.py nursery  --master <deploy dir> --repo <checkout> --out <dir>
python perf-results/objeck_bench.py stress   --master <deploy dir> --repo <checkout> --out <dir>
```

The driver in the tree is a later version than the one that produced these files (see Driver below), so a rerun is a comparable measurement, not a byte-identical one. Do not run it beside other work: `machine_state_*.txt` records the load average and running processes before and after each pass for that reason.

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
