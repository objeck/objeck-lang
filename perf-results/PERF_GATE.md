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

## Run locally

```bash
# build obc/obr first, then point at the bin dir:
bash perf-results/cross-lang/run_native.sh core/release/deploy-x64/bin /tmp/perf.csv 3
python3 perf-results/check_perf_gate.py /tmp/perf.csv
```

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
