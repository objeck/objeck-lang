# Perf Gate — native cross-language regression check

Catches **relative** performance regressions (the GC-safepoint one shipped
unnoticed because nothing compared releases head-to-head). It runs Objeck against
the fast peer JITs — **Java (HotSpot)** and **LuaJIT** — natively on one machine
and gates on the **Objeck-vs-peer time ratios**. Ratios on the same run cancel
machine noise, so the check is meaningful even on a shared/variable CI runner
where absolute times wander 2–3×.

## Pieces

- `cross-lang/run_native.sh <objeck_bin_dir> <out.csv> [runs] [--with-scripting]`
  — compiles + times Objeck CLBG benchmarks and the Java/LuaJIT equivalents
  (sources in `cross-lang/benchmarks/`), CI-scaled inputs. `--with-scripting`
  adds Python/Ruby (slow; off by default).
- `check_perf_gate.py RESULTS.csv` — median per (lang, benchmark) → Objeck/Java
  and Objeck/LuaJIT ratios → compared to `perf_baseline.json`. Warns at
  >+15%, fails at >+30% (ratio worsening). `--update-baseline` regenerates the
  baseline; `--no-fail` reports without failing; `--summary FILE` appends a
  Markdown table (used for the GitHub job summary).
- `perf_baseline.json` — committed baseline ratios (lower = Objeck faster).
- `.github/workflows/perf-gate.yml` — runs on pushes to `master` and PRs that
  touch `core/vm` / `core/compiler` / `core/shared` (pushes also on
  `perf-results/` or the workflow itself), plus manual dispatch.

## What is measured, and how

- **Inputs (CI-scaled, from `run_native.sh`):** nbody 50,000,000; fannkuchredux 11; spectralnorm 2000; binarytrees 15; mandelbrot 4000. These are smaller than the inputs on [docs/performance.md](../docs/performance.md) (fannkuchredux 12, spectralnorm 5500, binarytrees 17), so the ratios here are not comparable with that page's ratios. mandelbrot is compiled and timed for Objeck only: there is no Java or LuaJIT mandelbrot source in `cross-lang/benchmarks/`, so it produces no ratio and is not gated.
- **Timing:** wall-clock between two `date +%s%N` reads around the command, three runs by default (`runs` input of the manual dispatch), median taken by `check_perf_gate.py`. Objeck is timed as `obr x.obe <input>` after a separate `obc -opt s3` compile (binarytrees also links `gen_collect`); Java is timed after `javac`; LuaJIT runs the `.lua` source. Process start-up (VM, JVM) is inside every time. No warm-up run is discarded.
- **Machine:** the GitHub-hosted `ubuntu-24.04` runner (`perf-gate.yml`), which varies from run to run; peers come from the runner's `PATH` after `apt-get install default-jdk-headless luajit`. The run log prints the peers' versions. In the master run `43982983850` (2026-11-16, commit `31a8bf074f`) they were `openjdk version "17.0.21"` and LuaJIT 2.1 (`2.1.1703358377`), that is the runner's preinstalled JDK 17, not the OpenJDK 21 of the Docker run on [docs/performance.md](../docs/performance.md); a JDK change on the runner image would move every `objeck/java` ratio without any change to Objeck. The log's Objeck version line prints the `obc` usage text, not a version, so the commit under test is the only record of what was measured.
- **Gating statistic:** per pair and benchmark, ratio = Objeck median / peer median (lower is better). Change = (current - baseline) / baseline. `warn` above +15%, `FAIL` above +30%; `faster` below -15%; a benchmark with no baseline entry is reported `new`.

### The committed baseline and where master stands against it

`perf_baseline.json` (commit `4ed4132972`, 2026-06-17, GitHub-hosted runner) against the report from the Perf Gate run on master, `43982983850` (2026-11-16, `31a8bf074f`), taken from that run's log:

| pair | benchmark | baseline | run 43982983850 | change |
|---|---|---:|---:|---:|
| objeck/java | binarytrees | 28.691 | 3.738 | -87.0% |
| objeck/java | fannkuchredux | 1.795 | 0.995 | -44.6% |
| objeck/java | nbody | 9.650 | 2.614 | -72.9% |
| objeck/java | spectralnorm | 38.394 | 2.267 | -94.1% |
| objeck/luajit | binarytrees | 4.237 | 1.440 | -66.0% |
| objeck/luajit | fannkuchredux | 0.286 | 0.247 | -13.6% |
| objeck/luajit | nbody | 4.743 | 1.398 | -70.5% |
| objeck/luajit | spectralnorm | 44.942 | 2.849 | -93.7% |

The baseline is a pre-F7 snapshot, so the slack is large. The warn line is baseline x 1.15, so a row would warn only after its current ratio rose by a factor of 1.33 (`objeck/luajit fannkuchredux`: 0.247 to above 0.329), 2.1 (`objeck/java fannkuchredux`), 3.4 to 3.9 (`objeck/luajit` binarytrees, nbody), 4.3 (`objeck/java` nbody), 8.8 (`objeck/java` binarytrees) and up to 19.5 (`objeck/java` spectralnorm: 2.267 to above 44.15). Enforcing against this baseline would therefore catch only a collapse, not a regression. Refresh the baseline (steps below) before turning on enforcement.

### What the gate cannot find

- Anything it does not run: only four benchmarks have peers; the micro-benchmarks, the library benchmarks ([libs/README.md](libs/README.md)), memory use, start-up latency, compile time and multi-threaded scaling are outside it.
- A slowdown that Java or LuaJIT shares (a runner CPU change moves both sides, which is the point of the ratio, but a runner JDK change or a LuaJIT build change moves only the denominator).
- A regression smaller than the run-to-run noise of a shared runner, which the file's own header puts at 2-3x in absolute times; ratios cancel most of that, but nothing in the repository records how much is left. No variance of the ratios across runs is committed.
- Anything at all while the step carries `--no-fail`: the job always concludes `success` (every recent run is green, including the ones that reported `faster`), so a green check on this workflow carries no information about performance; read the job summary.

## Run locally

```bash
# build obc/obr first, then point at the bin dir:
bash perf-results/cross-lang/run_native.sh core/release/deploy-x64/bin /tmp/perf.csv 3
python3 perf-results/check_perf_gate.py /tmp/perf.csv
```

`run_native.sh` needs `javac`/`java` and `luajit` on `PATH` for the peer rows (a missing peer is skipped silently and its ratios do not appear); it is Linux or WSL only. Do not run it beside a build or a test run. To reproduce the committed baseline's exact conditions you would need the runner image of 2026-06-17, which is not recorded in the repository.

## Enabling hard enforcement

The workflow still runs **report-only** (`--no-fail`). Steps 1–3 below were done
on 2026-06-17 (commit `4ed4132972`): the committed baseline holds ratios measured
on the GitHub-hosted runner, replacing the dev-box seed. Only step 4 remains.

Those ratios predate the F7 JIT calling-convention work (PRs #757–#767), and
master now beats every one of them, by 10–94% (Perf Gate run on master,
2026-09-19). A regression that gave back part of that gain would still pass, so
repeat steps 1–3 before step 4.

1. Trigger **Perf Gate** once (Actions → Run workflow).
2. Download the `perf-gate-results` artifact → `perf_baseline.measured.json`.
3. Commit it as `perf-results/perf_baseline.json`.
4. Remove the `--no-fail` flag from the "Perf gate" step in `perf-gate.yml`.

After that a relative regression beyond the threshold fails the check. Refresh
the baseline the same way after any *intended* perf change (e.g. a new
optimization that shifts the ratios on purpose).
