#!/usr/bin/env python3
"""Standard-library micro-benchmarks with a self-calibrating reference.

Every perf-results/libs/<name>_bench.obs prints lines of the form

    BENCH <case> <seconds> <reference_seconds>

where the reference is a plain-array (or simplest possible) loop doing comparable work in
the same process. The ratio seconds / reference cancels most of the machine's speed and
noise, so ratios measured on different machines and runs can be compared.

usage: run_lib_bench.py <bin_dir> [--lib-dir DIR] [--runs N] [--only SUBSTR] [--stat median|min]
                        [--baseline FILE] [--update-baseline] [--no-fail] [--csv FILE]

Each benchmark is compiled once (obc -opt s3, the regression suite's libraries) and run N
times (default 5). The table shows the median seconds, median reference seconds and the
median of the per-run ratios (with --stat min: the fastest run of each side and their ratio). Ratios are compared with lib_baseline.json: more than 15%
slower warns, more than 30% fails (exit 1) unless --no-fail. --update-baseline writes the
measured ratios as the new baseline instead of comparing.
"""

import argparse
import csv
import datetime
import json
import os
import platform
import re
import statistics
import subprocess
import sys
import tempfile

HERE = os.path.dirname(os.path.abspath(__file__))
WARN = 0.15
FAIL = 0.30
# a reference faster than this is mostly clock granularity (clock() ticks in ms on Windows);
# the benchmarks repeat their references 20x and divide back down, so 2 ms is 40 ticks
MIN_REFERENCE = 0.002
BENCH_LINE = re.compile(r"^BENCH\s+(\S+)\s+(\S+)\s+(\S+)\s*$")


def exe(bin_dir, name):
    path = os.path.join(bin_dir, name + (".exe" if os.name == "nt" else ""))
    if not os.path.exists(path):
        sys.exit(f"error: {path} not found")
    return path


def extra_libs(src):
    """Libraries a '# EXTRA_LIBS: a,b' line at the start of a line asks for, as in the regression suite."""
    with open(src, encoding="utf-8") as fh:
        for line in fh:
            if line.startswith("# EXTRA_LIBS:"):
                return [lib.strip() for lib in line.split(":", 1)[1].split(",") if lib.strip()]
    return []


def compile_bench(obc, src, dest, env):
    libs = ["cipher", "collect", "xml", "json"]
    libs += [lib for lib in extra_libs(src) if lib not in libs]
    cmd = [obc, "-src", src, "-lib", ",".join(libs), "-opt", "s3", "-dest", dest]
    proc = subprocess.run(cmd, env=env, capture_output=True, text=True, timeout=600)
    if proc.returncode != 0 or not os.path.exists(dest):
        return proc.stdout + proc.stderr
    return None


def run_bench(obr, obe, env):
    proc = subprocess.run([obr, obe], env=env, capture_output=True, text=True, timeout=1800)
    results = {}
    for line in proc.stdout.splitlines():
        m = BENCH_LINE.match(line.strip())
        if m:
            results[m.group(1)] = (float(m.group(2)), float(m.group(3)))
    if proc.returncode != 0:
        tail = (proc.stdout + proc.stderr).strip().splitlines()[-5:]
        raise RuntimeError(f"{os.path.basename(obe)} exited {proc.returncode}: " + " | ".join(tail))
    return results


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("bin_dir", help="directory holding obc and obr")
    ap.add_argument("--lib-dir", help="library directory (OBJECK_LIB_PATH); default <bin_dir>/../lib")
    ap.add_argument("--runs", type=int, default=5, help="runs per benchmark (default 5)")
    ap.add_argument("--only", help="run only benchmarks whose file name contains this")
    ap.add_argument("--baseline", default=os.path.join(HERE, "lib_baseline.json"))
    ap.add_argument("--update-baseline", action="store_true", help="write the measured ratios as the baseline")
    ap.add_argument("--no-fail", action="store_true", help="report regressions without failing")
    ap.add_argument("--csv", help="CSV output (default perf-results/libs/results/lib_bench_<time>.csv)")
    ap.add_argument("--stat", choices=["median", "min"], default="median",
                    help="how runs combine: median (default) or min, which ignores runs slowed by other load")
    args = ap.parse_args()

    bin_dir = os.path.abspath(args.bin_dir)
    obc = exe(bin_dir, "obc")
    obr = exe(bin_dir, "obr")
    lib_dir = os.path.abspath(args.lib_dir) if args.lib_dir else os.path.normpath(os.path.join(bin_dir, "..", "lib"))

    env = dict(os.environ)
    env["OBJECK_LIB_PATH"] = lib_dir
    env["PATH"] = bin_dir + os.pathsep + os.path.join(lib_dir, "native") + os.pathsep + env.get("PATH", "")

    benches = sorted(f for f in os.listdir(HERE) if f.endswith("_bench.obs"))
    if args.only:
        benches = [f for f in benches if args.only in f]
    if not benches:
        sys.exit("error: no *_bench.obs files found")

    rows = []
    work = tempfile.mkdtemp(prefix="objk_lib_bench_")
    for bench in benches:
        name = bench[:-len("_bench.obs")]
        obe = os.path.join(work, name + ".obe")
        err = compile_bench(obc, os.path.join(HERE, bench), obe, env)
        if err:
            print(f"{bench}: compile failed\n{err.strip()}", file=sys.stderr)
            rows.append({"bench": name, "case": "<compile>", "error": "compile failed"})
            continue

        samples = {}
        for run in range(args.runs):
            try:
                result = run_bench(obr, obe, env)
            except (RuntimeError, subprocess.TimeoutExpired) as ex:
                print(f"{bench}: run {run + 1} failed: {ex}", file=sys.stderr)
                rows.append({"bench": name, "case": "<run>", "error": str(ex)[:200]})
                break
            for case, (secs, ref) in result.items():
                samples.setdefault(case, []).append((secs, ref))
            print(f"  {name}: run {run + 1}/{args.runs} done", file=sys.stderr)

        for case, values in samples.items():
            if args.stat == "min":
                # the least-disturbed run of each side: robust on a machine with other load
                secs = min(v[0] for v in values)
                ref = min(v[1] for v in values)
                ratio = secs / ref if ref >= MIN_REFERENCE else None
            else:
                secs = statistics.median(v[0] for v in values)
                ref = statistics.median(v[1] for v in values)
                ratios = [v[0] / v[1] for v in values if v[1] >= MIN_REFERENCE]
                ratio = statistics.median(ratios) if ratios else None
            rows.append({"bench": name, "case": case, "seconds": secs, "reference": ref,
                         "ratio": ratio, "runs": len(values)})

    baseline = {}
    if os.path.exists(args.baseline) and not args.update_baseline:
        with open(args.baseline, encoding="utf-8") as fh:
            saved = json.load(fh)
        baseline = saved.get("cases", {})
        if saved.get("stat", "median") != args.stat:
            print(f"warning: the baseline was measured with --stat {saved.get('stat', 'median')}, this run uses --stat {args.stat}", file=sys.stderr)

    failures = 0
    warnings = 0
    print()
    print(f"{'case':<40} {'seconds':>9} {'reference':>10} {'ratio':>8} {'baseline':>9} {'change':>8}  status")
    print("-" * 100)
    for row in rows:
        if "error" in row:
            print(f"{row['bench'] + '/' + row['case']:<40} {'':>9} {'':>10} {'':>8} {'':>9} {'':>8}  ERROR {row['error']}")
            failures += 1
            continue
        key = f"{row['bench']}/{row['case']}"
        ratio = row["ratio"]
        base = baseline.get(key, {}).get("ratio")
        status = "ok"
        change = ""
        if ratio is None:
            status = "reference too small to time"
        elif base is None:
            status = "new" if baseline else ""
        else:
            delta = ratio / base - 1.0
            change = f"{delta * 100:+.1f}%"
            if delta > FAIL:
                status = "FAIL"
                failures += 1
            elif delta > WARN:
                status = "WARN"
                warnings += 1
        row["baseline"] = base
        row["status"] = status
        ratio_text = f"{ratio:.3f}" if ratio is not None else "-"
        base_text = f"{base:.3f}" if base is not None else "-"
        print(f"{key:<40} {row['seconds']:>9.3f} {row['reference']:>10.3f} {ratio_text:>8} {base_text:>9} {change:>8}  {status}")

    csv_path = args.csv
    if not csv_path:
        stamp = datetime.datetime.now().strftime("%Y%m%d_%H%M%S")
        csv_path = os.path.join(HERE, "results", f"lib_bench_{stamp}.csv")
    os.makedirs(os.path.dirname(os.path.abspath(csv_path)), exist_ok=True)
    with open(csv_path, "w", newline="", encoding="utf-8") as fh:
        writer = csv.writer(fh)
        writer.writerow(["bench", "case", "seconds", "reference_seconds", "ratio", "baseline_ratio", "status", "runs"])
        for row in rows:
            writer.writerow([row["bench"], row["case"], row.get("seconds", ""), row.get("reference", ""),
                             row.get("ratio", ""), row.get("baseline", ""), row.get("status", row.get("error", "")),
                             row.get("runs", "")])
    print(f"\nCSV: {csv_path}")

    if args.update_baseline:
        cases = {}
        for row in rows:
            if "error" not in row and row["ratio"] is not None:
                cases[f"{row['bench']}/{row['case']}"] = {"ratio": round(row["ratio"], 4),
                                                         "seconds": round(row["seconds"], 4)}
        data = {
            "about": "Ratio (per the stat field) of each case's time to its in-run plain-array reference; "
                     "written by run_lib_bench.py --update-baseline",
            "measured": datetime.date.today().isoformat(),
            "platform": f"{platform.system()} {platform.machine()}",
            "runs": args.runs,
            "stat": args.stat,
            "cases": dict(sorted(cases.items())),
        }
        with open(args.baseline, "w", encoding="utf-8", newline="\n") as fh:
            json.dump(data, fh, indent=2)
            fh.write("\n")
        print(f"baseline written: {args.baseline} ({len(cases)} cases)")
        return 0

    print(f"{failures} failure(s), {warnings} warning(s) (warn > +{WARN * 100:.0f}%, fail > +{FAIL * 100:.0f}% in ratio)")
    if failures and not args.no_fail:
        return 1
    return 0


if __name__ == "__main__":
    sys.exit(main())
