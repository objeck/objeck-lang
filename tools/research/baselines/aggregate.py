#!/usr/bin/env python3
"""Aggregates results/raw.jsonl into results/results.csv (one row per run) and results/summary.csv /
summary.md (one row per host/config/ep: median and min-max across repetitions), for all runs and for the
quiet runs only, with checksum agreement against the C++ control.

  python3 aggregate.py [results_dir]
"""
import csv
import json
import os
import statistics as st
import sys

d = sys.argv[1] if len(sys.argv) > 1 else os.path.join(os.path.dirname(os.path.abspath(__file__)), "results")
rows = [json.loads(l) for l in open(os.path.join(d, "raw.jsonl"))]


def gc_count(r):
    g = r["result"]["gc"]
    h = r["host"]
    if h == "objeck":
        return g["minor"] + g["major"]
    if h == "java":
        return g["events"]
    if h == "dotnet":
        return g["gen0"]  # gen0 count includes every collection of a higher generation
    if h == "go":
        return g["num_gc"]
    if h == "python":
        return sum(g["gen_collections"])
    return 0


def gc_pause_ms(r):
    g = r["result"]["gc"]
    h = r["host"]
    if h == "java":
        return float(g["mxbean_ms"])
    if h in ("dotnet", "go", "python"):
        return float(g["pause_total_ms"])
    return None


def gc_max_ms(r):
    g = r["result"]["gc"]
    h = r["host"]
    if h == "java":
        return max([v["max_ms"] for v in g["notif"].values()] or [0])
    if h == "go" or h == "python":
        return g["pause_max_ms"]
    if h == "objeck":
        return g["max_pause_us_cum"] / 1000.0  # cumulative since process start (includes warm-up)
    return None


# one run per (host, config, ep, rep): the last attempt, but a quiet one beats a noisy one
best = {}
for r in rows:
    if not r.get("result"):
        continue
    k = (r["host"], r["config"], r["ep"], r["rep"])
    if k not in best:
        best[k] = r
    elif best[k].get("noisy") and not r.get("noisy"):
        best[k] = r
    elif bool(best[k].get("noisy")) == bool(r.get("noisy")) and r["attempt"] > best[k]["attempt"]:
        best[k] = r
runs = sorted(best.values(), key=lambda r: (r["ep"], r["host"], r["config"], r["rep"]))

cols = ["host", "config", "ep", "rep", "noisy", "load1_before", "others_cores_before", "others_cores_during", "stall_n",
        "stall_p50_us", "stall_p95_us", "stall_p99_us", "stall_p999_us", "stall_max_us", "infer_n", "infer_p50_ms",
        "infer_p95_ms", "infer_p99_ms", "img_per_s", "gc_count", "gc_pause_total_ms", "gc_pause_max_ms",
        "gpu_mem_peak_mib", "gpu_mem_delta_mib", "gpu_sm_mhz_before", "gpu_sm_mhz_after", "checksum_sum", "argmax0"]
with open(os.path.join(d, "results.csv"), "w", newline="") as f:
    w = csv.writer(f)
    w.writerow(cols)
    for r in runs:
        x = r["result"]
        gb = r["gpu_before"].split(",")
        ga = r["gpu_after"].split(",")
        w.writerow([r["host"], r["config"], r["ep"], r["rep"], int(bool(r.get("noisy"))), r["load1_before"],
                    round(r["others_cores_before"], 2), round(r["others_cores_during"], 2), x["stall_us"]["n"],
                    x["stall_us"]["p50"], x["stall_us"]["p95"], x["stall_us"]["p99"], x["stall_us"]["p999"],
                    x["stall_us"]["max"], x["infer_ms"]["n"], x["infer_ms"]["p50"], x["infer_ms"]["p95"],
                    x["infer_ms"]["p99"], x["img_s"], gc_count(r), gc_pause_ms(r), gc_max_ms(r), r["gpu_mem_peak_mib"],
                    r["gpu_mem_peak_mib"] - r["gpu_mem_base_mib"], gb[0].strip(), ga[0].strip(), x["checksum"]["sum"],
                    x["checksum"]["argmax0"]])

metrics = [("stall_p50_us", lambda r: r["result"]["stall_us"]["p50"]), ("stall_p95_us", lambda r: r["result"]["stall_us"]["p95"]),
           ("stall_p99_us", lambda r: r["result"]["stall_us"]["p99"]), ("stall_p999_us", lambda r: r["result"]["stall_us"]["p999"]),
           ("stall_max_us", lambda r: r["result"]["stall_us"]["max"]), ("infer_p50_ms", lambda r: r["result"]["infer_ms"]["p50"]),
           ("infer_p95_ms", lambda r: r["result"]["infer_ms"]["p95"]), ("infer_p99_ms", lambda r: r["result"]["infer_ms"]["p99"]),
           ("img_per_s", lambda r: r["result"]["img_s"]), ("gc_count", gc_count),
           ("gc_pause_total_ms", gc_pause_ms), ("gc_pause_max_ms", gc_max_ms),
           ("gpu_mem_delta_mib", lambda r: r["gpu_mem_peak_mib"] - r["gpu_mem_base_mib"])]

refs = {}
for r in runs:
    if r["host"] == "cpp":
        refs.setdefault(r["ep"], r["result"]["checksum"])

sumcols = ["subset", "ep", "host", "config", "reps", "noisy_reps"]
for m, _ in metrics:
    sumcols += [m + "_median", m + "_min", m + "_max"]
sumcols += ["load1_median", "others_cores_median", "others_cores_max", "checksum_max_abs_diff_v0_v2", "argmax_agrees"]
ix = {c: i for i, c in enumerate(sumcols)}


def build(subset, pick):
    groups = {}
    for r in runs:
        if pick(r):
            groups.setdefault((r["ep"], r["host"], r["config"]), []).append(r)
    out = []
    for (ep, host, cfg), rs in sorted(groups.items()):
        row = [subset, ep, host, cfg, len(rs), sum(1 for r in rs if r.get("noisy"))]
        for m, fn in metrics:
            vals = [v for v in (fn(r) for r in rs) if v is not None]
            row += [round(st.median(vals), 3), round(min(vals), 3), round(max(vals), 3)] if vals else ["", "", ""]
        row.append(round(st.median([r["load1_before"] for r in rs]), 2))
        row.append(round(st.median([r["others_cores_during"] for r in rs]), 2))
        row.append(round(max(r["others_cores_during"] for r in rs), 2))
        rc = refs.get(ep)
        if rc:
            diff = max(max(abs(r["result"]["checksum"][k] - rc[k]) for k in ("v0", "v1", "v2")) for r in rs)
            row += [f"{diff:.2e}", all(r["result"]["checksum"]["argmax0"] == rc["argmax0"] for r in rs)]
        else:
            row += ["", ""]
        out.append(row)
    return out


all_rows = build("all", lambda r: True)
quiet_rows = build("quiet", lambda r: not r.get("noisy"))
with open(os.path.join(d, "summary.csv"), "w", newline="") as f:
    w = csv.writer(f)
    w.writerow(sumcols)
    w.writerows(all_rows + quiet_rows)


def fmt(row, m, digits=1):
    a, b, c = row[ix[m + "_median"]], row[ix[m + "_min"]], row[ix[m + "_max"]]
    if a == "":
        return "n/a"
    return f"{a:,.{digits}f} ({b:,.{digits}f}-{c:,.{digits}f})"


with open(os.path.join(d, "summary.md"), "w") as f:
    for subset, rows_ in (("all runs", all_rows), ("quiet runs only (other tenants under 3 cores, load1 under 4)", quiet_rows)):
        for ep in ("cuda", "cpu"):
            sel = [r for r in rows_ if r[ix["ep"]] == ep]
            if not sel:
                continue
            for title, ms, dg in (("Allocator stall (microseconds)", ["stall_p50_us", "stall_p95_us", "stall_p99_us", "stall_p999_us", "stall_max_us"], 0),
                                   ("Inference latency (ms) and throughput (images/s)", ["infer_p50_ms", "infer_p95_ms", "infer_p99_ms", "img_per_s"], 1),
                                   ("Collector activity and GPU memory", ["gc_count", "gc_pause_total_ms", "gc_pause_max_ms", "gpu_mem_delta_mib"], 1)):
                f.write(f"\n### {ep}, {subset}: {title}; median (min-max) across repetitions\n\n| host | config | reps (noisy) | " + " | ".join(ms) + " |\n|---|---|---|" + "---|" * len(ms) + "\n")
                for r in sel:
                    f.write(f"| {r[ix['host']]} | {r[ix['config']]} | {r[ix['reps']]} ({r[ix['noisy_reps']]}) | " + " | ".join(fmt(r, m, dg) for m in ms) + " |\n")
            f.write(f"\n### {ep}, {subset}: contention and correctness\n\n| host | config | load1 median | other tenants' cores median (max) | max abs diff of 3 logits vs C++ | argmax agrees |\n|---|---|---|---|---|---|\n")
            for r in sel:
                f.write(f"| {r[ix['host']]} | {r[ix['config']]} | {r[ix['load1_median']]} | {r[ix['others_cores_median']]} ({r[ix['others_cores_max']]}) | {r[ix['checksum_max_abs_diff_v0_v2']]} | {r[ix['argmax_agrees']]} |\n")
print("wrote", d)
