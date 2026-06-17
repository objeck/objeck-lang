#!/usr/bin/env python3
"""Perf gate: compare Objeck-vs-peer ratios against a committed baseline.

Reads a cross-language results CSV (language,benchmark,run,time_seconds), takes
the median time per (language,benchmark), and computes Objeck/peer ratios
(objeck/java, objeck/luajit). Ratios on the *same* run cancel CI machine noise,
so they are the gate signal — a regression like the GC-safepoint one shows up as
Objeck getting relatively slower vs Java/LuaJIT, regardless of runner speed.

Usage:
  check_perf_gate.py RESULTS.csv [--baseline perf_baseline.json]
                                 [--warn PCT] [--fail PCT]
                                 [--update-baseline] [--no-fail] [--summary FILE]

Exit code: 0 if within tolerance (or --no-fail / --update-baseline), 1 on a
ratio regression beyond --fail.
"""
import argparse, csv, json, os, statistics, sys

PEERS = ["java", "luajit"]


def load_medians(path):
    rows = {}
    with open(path) as f:
        for r in csv.DictReader(f):
            rows.setdefault((r["language"], r["benchmark"]), []).append(float(r["time_seconds"]))
    return {k: statistics.median(v) for k, v in rows.items() if v}


def compute_ratios(med):
    benches = sorted({b for (lang, b) in med if lang == "objeck"})
    ratios = {}
    for peer in PEERS:
        rp = {}
        for b in benches:
            o = med.get(("objeck", b)); p = med.get((peer, b))
            if o and p and p > 0:
                rp[b] = round(o / p, 4)
        if rp:
            ratios[f"objeck/{peer}"] = rp
    return ratios


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("results")
    ap.add_argument("--baseline", default=os.path.join(os.path.dirname(__file__), "perf_baseline.json"))
    ap.add_argument("--warn", type=float, default=15.0, help="warn if ratio worsens >PCT%%")
    ap.add_argument("--fail", type=float, default=30.0, help="fail if ratio worsens >PCT%%")
    ap.add_argument("--update-baseline", action="store_true")
    ap.add_argument("--no-fail", action="store_true", help="report but never exit non-zero")
    ap.add_argument("--summary", default=os.environ.get("GITHUB_STEP_SUMMARY"))
    args = ap.parse_args()

    med = load_medians(args.results)
    cur = compute_ratios(med)

    if args.update_baseline:
        with open(args.baseline, "w") as f:
            json.dump({"ratios": cur, "note": "Objeck/peer median-time ratios; lower is better. Refresh on a quiet CI runner after an intended perf change."}, f, indent=2)
            f.write("\n")
        print(f"Wrote baseline {args.baseline}")
        return 0

    base = {}
    if os.path.exists(args.baseline):
        base = json.load(open(args.baseline)).get("ratios", {})

    lines = ["## Perf gate - Objeck vs peer ratios (lower = Objeck faster)", ""]
    lines.append("| Pair | Benchmark | Baseline | Current | Change | Status |")
    lines.append("|------|-----------|----------|---------|---|--------|")
    worst = 0.0
    failed = []
    have_base = bool(base)
    for pair in sorted(cur):
        for b in sorted(cur[pair]):
            c = cur[pair][b]
            bl = base.get(pair, {}).get(b)
            if bl is None:
                lines.append(f"| {pair} | {b} | - | {c:.3f} | - | new |")
                continue
            pct = (c - bl) / bl * 100.0
            worst = max(worst, pct)
            if pct > args.fail:
                st = "FAIL"; failed.append(f"{pair} {b}: {bl:.3f}->{c:.3f} (+{pct:.1f}%)")
            elif pct > args.warn:
                st = "warn"
            elif pct < -args.warn:
                st = "faster"
            else:
                st = "ok"
            lines.append(f"| {pair} | {b} | {bl:.3f} | {c:.3f} | {pct:+.1f}% | {st} |")

    if not have_base:
        lines += ["", "_No baseline committed yet — run with `--update-baseline` on a CI runner and commit `perf_baseline.json`._"]
    lines += ["", f"Thresholds: warn > +{args.warn:.0f}%, fail > +{args.fail:.0f}% (ratio worsening). Worst delta: {worst:+.1f}%."]

    report = "\n".join(lines)
    print(report)
    if args.summary:
        with open(args.summary, "a") as f:
            f.write(report + "\n")

    if failed and have_base and not args.no_fail:
        print("\nPERF GATE FAILED:")
        for x in failed:
            print("  - " + x)
        return 1
    return 0


if __name__ == "__main__":
    sys.exit(main())
