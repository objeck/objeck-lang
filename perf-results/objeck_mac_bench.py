#!/usr/bin/env python3
"""
Objeck v2026.10.0 hardening -- macOS arm64 measurement driver.

Covers the three items the integrator assigned to the Mac:

  baseline  plan Phase 0.3 perf baselines + the DeadStore perf check.
            master vs v2026.9.4, same boot, interleaved ABAB, warm-ups
            discarded. Answers "is any benchmark >5% slower, p<0.01".
  nursery   G3 nursery sweep (#841): --nursery=16m/32m/64m/128m.
  stress    an arm64 data point for #861 (core_thread_gc_stress SIGSEGV
            at a 256k nursery).

Why Python and not perf-results/run_benchmarks.sh: that script times with
`/usr/bin/time -f "%e %M"`, which is GNU syntax. macOS ships the BSD time,
which has no -f and reports peak RSS in BYTES under -l, not KB. Rather than
fight that, every run here sets OBJECK_GC_STATS=1 and reads the
`[gc-stats] ... peak_rss_bytes=N` line the VM already prints to stderr at
exit -- same number on every platform, and it carries the GC counters too.

Nothing here writes to the repo. Results go to --out.

Usage:
  ./objeck_mac_bench.py baseline --master DIR --base DIR --repo DIR --out DIR
  ./objeck_mac_bench.py nursery  --master DIR --repo DIR --out DIR
  ./objeck_mac_bench.py stress   --master DIR --repo DIR --out DIR

DIR for --master/--base is a deploy tree (the one holding bin/obc, bin/obr).
"""

import argparse
import math
import csv
import json
import os
import re
import statistics
import subprocess
import sys
import time
from pathlib import Path

# Inputs copied from perf-results/run_benchmarks.sh so the numbers stay
# comparable with every baseline taken on the x64 box.
CLBG = {
    "nbody": "50000000",
    "binarytrees": "17",
    "spectralnorm": "5500",
    "fannkuchredux": "12",
    "fasta": "25000000",
    "mandelbrot": "4000",
}
PERF = {
    "bench_loop_invariant": "",
    "bench_cse": "",
    "bench_strength_ext": "",
    "bench_gc_churn": "",
    "bench_gc_large_heap": "18",
    "bench_array_intensive": "",
    "bench_matrix_multiply": "500",
    "bench_method_dispatch": "",
    "bench_copy_prop": "",
    "bench_dead_code": "",
    "bench_tco": "",
    "bench_spectralnorm_native": "",
}
# Allocation-heavy picks for the nursery sweep.
NURSERY_BENCHES = ["binarytrees", "bench_gc_churn", "bench_gc_large_heap"]
NURSERY_SIZES = ["16m", "32m", "64m", "128m"]

# Adaptive run count. Short benchmarks need more samples than long ones: at n=15
# bench_matrix_multiply (0.2 s) read +8.95% (p=0.042), and at n=60 the same pair
# read -1.58% (p=0.22) -- the instability was in the sampling, not in either build.
# Runs therefore scale to ~TARGET_SECONDS of wall time per build.
TARGET_SECONDS = 10.0
MIN_RUNS = 15
MAX_RUNS = 101

GC_STATS_RE = re.compile(r"\[gc-stats\]\s+(.*)")
RSS_RE = re.compile(r"^\s*(\d+)\s+maximum resident set size", re.M)


def gc_stats_fields(stderr_text):
    """Parse the VM's single [gc-stats] line into {name: int}."""
    out = {}
    m = GC_STATS_RE.search(stderr_text or "")
    if not m:
        return out
    for tok in m.group(1).split():
        if "=" in tok:
            k, v = tok.split("=", 1)
            try:
                out[k] = int(v)
            except ValueError:
                pass
    return out


def src_path(repo, name):
    for sub in ("programs/tests/clbg", "programs/tests/perf", "programs/regression"):
        p = Path(repo) / sub / (name + ".obs")
        if p.is_file():
            return p
    raise FileNotFoundError("no source for %s under %s" % (name, repo))


def compile_bench(deploy, repo, name, out_dir, tag):
    """Compile with THIS build's own obc; each build gets its own .obe."""
    obc = Path(deploy) / "bin" / "obc"
    src = src_path(repo, name)
    obe = Path(out_dir) / "obe" / tag / (name + ".obe")
    obe.parent.mkdir(parents=True, exist_ok=True)
    env = dict(os.environ, OBJECK_LIB_PATH=str(Path(deploy) / "lib"))
    r = subprocess.run(
        [str(obc), "-src", str(src), "-dest", str(obe), "-opt", "s3"],
        env=env, capture_output=True, text=True,
    )
    if r.returncode != 0:
        raise RuntimeError("compile failed for %s (%s):\n%s\n%s"
                           % (name, tag, r.stdout[-2000:], r.stderr[-2000:]))
    return obe


def run_once(deploy, obe, args, extra_env=None, obr_flags=(), timeout=1800):
    """One timed run. Returns (wall_seconds, exit_code, gc fields, stderr)."""
    obr = Path(deploy) / "bin" / "obr"
    env = dict(os.environ,
               OBJECK_LIB_PATH=str(Path(deploy) / "lib"),
               OBJECK_GC_STATS="1")
    if extra_env:
        env.update(extra_env)
    cmd = [str(obr)] + list(obr_flags) + [str(obe)]
    if args:
        cmd += args.split()
    # v2026.9.4 predates OBJECK_GC_STATS (0a5e7ba008), so its runs emit no
    # [gc-stats] line and would have no RSS at all. BSD time -l reports
    # "maximum resident set size" in BYTES on macOS -- same unit as
    # peak_rss_bytes -- so wrap every run and prefer the VM's own number when
    # it is there. The extra fork is constant and applies to both builds, so
    # the master-vs-9.4 delta is unaffected.
    if sys.platform == "darwin":
        cmd = ["/usr/bin/time", "-l"] + cmd
    t0 = time.perf_counter()
    try:
        r = subprocess.run(cmd, env=env, capture_output=True, text=True, timeout=timeout)
        wall = time.perf_counter() - t0
        gc = gc_stats_fields(r.stderr)
        m = RSS_RE.search(r.stderr or "")
        if m:
            gc["peak_rss_time_l"] = int(m.group(1))
        return wall, r.returncode, gc, r.stderr
    except subprocess.TimeoutExpired:
        return time.perf_counter() - t0, -99, {}, "TIMEOUT after %ds" % timeout


# ---- statistics ------------------------------------------------------------

def mann_whitney_p(a, b):
    """
    Two-sided Mann-Whitney U, normal approximation with tie correction.
    n=15 per group is comfortably enough for the approximation, and it makes
    no normality assumption -- which matters, because run-time distributions
    are right-skewed (a stray scheduler hit only ever makes a run slower).
    Returns None when either group is too small to say anything.
    """
    n1, n2 = len(a), len(b)
    if n1 < 5 or n2 < 5:
        return None
    merged = sorted([(v, 0) for v in a] + [(v, 1) for v in b])
    ranks = [0.0] * len(merged)
    i = 0
    tie_term = 0.0
    while i < len(merged):
        j = i
        while j + 1 < len(merged) and merged[j + 1][0] == merged[i][0]:
            j += 1
        avg = (i + j) / 2.0 + 1.0
        for k in range(i, j + 1):
            ranks[k] = avg
        t = j - i + 1
        if t > 1:
            tie_term += t ** 3 - t
        i = j + 1
    r1 = sum(rk for rk, (_, g) in zip(ranks, merged) if g == 0)
    u1 = r1 - n1 * (n1 + 1) / 2.0
    u = min(u1, n1 * n2 - u1)
    mu = n1 * n2 / 2.0
    n = n1 + n2
    sigma_sq = (n1 * n2 / 12.0) * ((n + 1) - tie_term / float(n * (n - 1)))
    if sigma_sq <= 0:
        return None
    z = (u - mu + 0.5) / (sigma_sq ** 0.5)
    # two-sided normal tail
    import math
    return max(0.0, min(1.0, 2.0 * (1.0 - 0.5 * (1.0 + math.erf(abs(z) / math.sqrt(2.0))))))


# ---- modes -----------------------------------------------------------------

def mode_baseline(a):
    benches = list(CLBG.items()) + list(PERF.items())
    out = Path(a.out)
    out.mkdir(parents=True, exist_ok=True)
    builds = [("master", a.master), ("v2026.9.4", a.base)]

    print("compiling %d benchmarks with each build" % len(benches))
    obes = {}
    for tag, deploy in builds:
        for name, _ in benches:
            obes[(tag, name)] = compile_bench(deploy, a.repo, name, out, tag)

    raw_path = out / "baseline_raw.csv"
    raw = open(raw_path, "w", newline="")
    w = csv.writer(raw)
    w.writerow(["build", "benchmark", "run", "wall_s", "peak_rss_time_l", "peak_rss_vm",
                "gc_minor", "gc_major", "gc_promoted_bytes",
                "pause_p50_us", "pause_p95_us", "pause_max_us", "exit"])

    samples = {}
    chosen = {}
    for name, args in benches:
        print("\n=== %s (args=%r) ===" % (name, args or "-"))
        # Warm-ups are discarded for measurement, but they are timed, so the run
        # count calibrates itself at no extra cost.
        warm = {}
        for tag, deploy in builds:
            ws = [run_once(deploy, obes[(tag, name)], args)[0] for _ in range(a.warmups)]
            if ws:
                warm[tag] = statistics.median(ws)
        runs = a.runs
        if a.adaptive and warm:
            # Calibrate on the SLOWER build so both columns get equal power and the
            # ABAB pairing stays symmetric; unequal n would sample one build better.
            t = max(warm.values())
            if t > 0:
                runs = max(MIN_RUNS, min(MAX_RUNS, int(math.ceil(TARGET_SECONDS / t))))
            print("  warm-up %.4fs on the slower build -> n=%d per build" % (t, runs))
        chosen[name] = runs
        # interleaved ABAB so any thermal or background drift hits both builds
        for i in range(1, runs + 1):
            for tag, deploy in builds:
                wall, code, gc, _ = run_once(deploy, obes[(tag, name)], args)
                if code != 0:
                    print("  !! %s %s run %d exited %d" % (tag, name, i, code))
                samples.setdefault((name, tag), []).append(wall)
                w.writerow([tag, name, i, "%.6f" % wall,
                            gc.get("peak_rss_time_l", ""), gc.get("peak_rss_bytes", ""), gc.get("minor", ""),
                            gc.get("major", ""), gc.get("promoted_bytes", ""),
                            gc.get("pause_p50_us", ""), gc.get("pause_p95_us", ""),
                            gc.get("pause_max_us", ""), code])
                raw.flush()
            print("  run %d/%d  master=%.3fs  9.4=%.3fs"
                  % (i, runs, samples[(name, "master")][-1],
                     samples[(name, "v2026.9.4")][-1]))
    raw.close()

    # summary: master vs 9.4, the >5% / p<0.01 question
    rows = []
    regressions = []
    for name, _ in benches:
        m = samples.get((name, "master"), [])
        b = samples.get((name, "v2026.9.4"), [])
        if not m or not b:
            continue
        mm, mb = statistics.median(m), statistics.median(b)
        delta = (mm - mb) / mb * 100.0 if mb else float("nan")
        p = mann_whitney_p(m, b)
        rows.append({"benchmark": name, "runs": len(m), "master_median_s": round(mm, 6),
                     "v2026_9_4_median_s": round(mb, 6),
                     "delta_pct": round(delta, 2),
                     "p_value": (round(p, 6) if p is not None else None),
                     "master_samples": [round(x, 6) for x in m],
                     "v2026_9_4_samples": [round(x, 6) for x in b]})
        if delta > 5.0 and p is not None and p < 0.01:
            regressions.append((name, delta, p))

    (out / "baseline_summary.json").write_text(json.dumps(rows, indent=2))
    print("\n%-28s %12s %12s %9s %10s" % ("benchmark", "master", "v2026.9.4", "delta%", "p"))
    for r in rows:
        print("%-28s %12.4f %12.4f %+9.2f %10s"
              % (r["benchmark"], r["master_median_s"], r["v2026_9_4_median_s"],
                 r["delta_pct"],
                 "%.4g" % r["p_value"] if r["p_value"] is not None else "-"))
    print("\nGATE (>5%% slower AND p<0.01): %s"
          % ("PASS -- no regression" if not regressions else "FAIL"))
    for name, d, p in regressions:
        print("  REGRESSION %s +%.2f%% (p=%.4g)" % (name, d, p))
    print("\nraw: %s\nsummary: %s" % (raw_path, out / "baseline_summary.json"))


def mode_nursery(a):
    out = Path(a.out)
    out.mkdir(parents=True, exist_ok=True)
    path = out / "nursery_sweep.csv"
    f = open(path, "w", newline="")
    w = csv.writer(f)
    w.writerow(["benchmark", "nursery", "run", "wall_s", "peak_rss_bytes", "peak_rss_time_l",
                "gc_minor", "gc_major", "gc_promoted_objects", "gc_promoted_bytes",
                "pause_p50_us", "pause_p95_us", "pause_max_us", "exit"])
    allargs = dict(CLBG); allargs.update(PERF)
    for name in NURSERY_BENCHES:
        obe = compile_bench(a.master, a.repo, name, out, "master")
        args = allargs.get(name, "")
        for size in NURSERY_SIZES:
            print("\n=== %s @ --nursery=%s ===" % (name, size))
            for _ in range(a.warmups):
                run_once(a.master, obe, args, obr_flags=["--nursery=" + size])
            for i in range(1, a.runs + 1):
                wall, code, gc, _ = run_once(a.master, obe, args,
                                             obr_flags=["--nursery=" + size])
                w.writerow([name, size, i, "%.6f" % wall,
                            gc.get("peak_rss_bytes", ""), gc.get("peak_rss_time_l", ""), gc.get("minor", ""),
                            gc.get("major", ""), gc.get("promoted_objects", ""),
                            gc.get("promoted_bytes", ""), gc.get("pause_p50_us", ""),
                            gc.get("pause_p95_us", ""), gc.get("pause_max_us", ""), code])
                f.flush()
                print("  %d/%d  %.3fs  rss=%s minor=%s major=%s p95=%sus"
                      % (i, a.runs, wall, gc.get("peak_rss_bytes", "?"),
                         gc.get("minor", "?"), gc.get("major", "?"),
                         gc.get("pause_p95_us", "?")))
    f.close()
    print("\nwrote %s" % path)


def mode_stress(a):
    """#861: core_thread_gc_stress at a 256k nursery, 4 configs x N runs."""
    out = Path(a.out)
    (out / "crashes").mkdir(parents=True, exist_ok=True)
    obe = compile_bench(a.master, a.repo, "core_thread_gc_stress", out, "master")
    configs = []
    for verify in (False, True):
        for jit_off in (False, True):
            configs.append({
                "name": "nursery256k%s%s" % ("+verify" if verify else "",
                                             "+jitoff" if jit_off else "+jitdefault"),
                "env": {"OBJECK_NURSERY": "256k",
                        **({"OBJECK_GC_VERIFY": "1"} if verify else {})},
                "flags": ["--jit=off"] if jit_off else [],
            })
    path = out / "stress_861.csv"
    f = open(path, "w", newline="")
    w = csv.writer(f); w.writerow(["config", "run", "exit", "wall_s", "note"])
    failures = 0
    for cfg in configs:
        print("\n=== %s : %d runs ===" % (cfg["name"], a.runs))
        for i in range(1, a.runs + 1):
            wall, code, _, err = run_once(a.master, obe, "",
                                          extra_env=cfg["env"], obr_flags=cfg["flags"])
            note = ""
            if code != 0:
                failures += 1
                note = "FAILURE"
                p = out / "crashes" / ("%s_run%03d.txt" % (cfg["name"], i))
                p.write_text("exit=%d\nwall=%.3f\n\n%s" % (code, wall, err))
                print("  !! run %d exit %d -> %s" % (i, code, p))
                print("     inspect any core with: lldb -c /cores/core.<pid> "
                      "%s/bin/obr  then  thread backtrace all" % a.master)
            w.writerow([cfg["name"], i, code, "%.6f" % wall, note]); f.flush()
            if i % 25 == 0:
                print("  %d/%d done (%d failures so far)" % (i, a.runs, failures))
    f.close()
    total = len(configs) * a.runs
    print("\n%d failures in %d runs -> %s" % (failures, total,
          "CROSS-PLATFORM, tell the integrator" if failures else
          "clean; bounds macOS arm64 under ~%.2f%% (rule of three)" % (300.0 / total)))
    print("wrote %s" % path)


def main():
    ap = argparse.ArgumentParser(description=__doc__,
                                 formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("mode", choices=["baseline", "nursery", "stress"])
    ap.add_argument("--master", required=True, help="deploy tree built from master")
    ap.add_argument("--base", help="deploy tree built from v2026.9.4 (baseline mode)")
    ap.add_argument("--repo", required=True, help="checkout holding programs/tests")
    ap.add_argument("--out", required=True, help="output directory (outside the repo)")
    ap.add_argument("--runs", type=int, default=None)
    ap.add_argument("--warmups", type=int, default=2)
    ap.add_argument("--no-adaptive", action="store_true",
                    help="keep the run count fixed instead of scaling short benchmarks up")
    a = ap.parse_args()
    explicit_runs = a.runs is not None
    if a.runs is None:
        a.runs = 200 if a.mode == "stress" else MIN_RUNS
    # Adaptive only in baseline mode, and never when the caller fixed --runs.
    a.adaptive = (a.mode == "baseline") and not a.no_adaptive and not explicit_runs
    if a.mode == "baseline" and not a.base:
        ap.error("baseline mode needs --base (the v2026.9.4 deploy tree)")
    if sys.platform != "darwin":
        print("note: not macOS; running anyway", file=sys.stderr)
    {"baseline": mode_baseline, "nursery": mode_nursery, "stress": mode_stress}[a.mode](a)


if __name__ == "__main__":
    main()
