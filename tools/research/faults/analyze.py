#!/usr/bin/env python3
"""Turns results.csv into the tables of RESULTS.md (Wilson 95% intervals).

  python3 analyze.py results.csv > summary.md

Per repetition (one seeded run of one scenario) the run-level properties are Bernoulli trials; the
unit-level rows pool the injected units of all repetitions (units inside a run are not independent, so
those intervals are optimistic; the run-level ones are the ones to trust).
"""
import csv, math, statistics, sys
from collections import defaultdict


def wilson(k, n, z=1.96):
    if n == 0:
        return (float("nan"),) * 3
    p = k / n
    d = 1 + z * z / n
    c = (p + z * z / (2 * n)) / d
    h = z * math.sqrt(p * (1 - p) / n + z * z / (4 * n * n)) / d
    return p, max(0.0, c - h), min(1.0, c + h)


def fmt(k, n):
    if n == 0:
        return "n/a"
    p, lo, hi = wilson(k, n)
    return "%d/%d = %.0f%% [%.0f-%.0f]" % (k, n, 100 * p, 100 * lo, 100 * hi)


def num(x, default=None):
    try:
        return float(x)
    except (TypeError, ValueError):
        return default


def med(xs):
    xs = [x for x in xs if x is not None]
    return statistics.median(xs) if xs else None


def load(path):
    rows = defaultdict(list)
    for r in csv.DictReader(open(path)):
        if r.get("driver_error"):
            print("DRIVER ERROR in %s rep %s: %s" % (r["scenario"], r["rep"], r["driver_error"]), file=sys.stderr)
            continue
        rows[r["scenario"]].append(r)
    return rows


def control_for(rows, sid, sample):
    """the fault-free scenario with the same system, kind, config and backend"""
    s = sample
    cands = []
    if s["kind"] == "agents":
        cands = ["agents_%s_none_%s_%s" % (s["system"], s["config"], t) for t in ("control", "httpcontrol")]
    elif s["kind"] == "infer":
        cands = ["infer_%s_none_control_%s" % (s["system"], s["ep"]), "infer_%s_none_%s" % (s["system"], s["ep"])]
    for c in cands:
        if c in rows and c != sid:
            if ("httpcontrol" in c) == (s["fault"] in ("http500", "garbage", "truncjson", "wrongshape", "delay", "hold", "drop", "truncate", "rst", "slowloris")):
                return rows[c]
    return []


def summarize(rows):
    out = {}
    for sid, rs in rows.items():
        s = rs[0]
        ctl = control_for(rows, sid, s)
        ctl_threads = med([num(r["threads_last"]) for r in ctl])
        ctl_fds = med([num(r["fds_last"]) for r in ctl])
        ctl_stall = sorted([num(r["by_ms_max"], 0) for r in ctl])
        stall_limit = (ctl_stall[min(len(ctl_stall) - 1, int(0.95 * len(ctl_stall)))] * 3 + 100) if ctl_stall else 1000
        n = len(rs)
        o = {"sid": sid, "n": n, "system": s["system"], "config": s["config"], "fault": s["fault"], "kind": s["kind"], "ep": s["ep"]}
        o["contained"] = sum(1 for r in rs if r["by_expected"] and r["by_seen"] == r["by_expected"] and r["by_ok"] == r["by_expected"]
                             and num(r["by_ms_max"], 0) <= stall_limit)
        o["by_complete"] = sum(1 for r in rs if r["by_expected"] and r["by_seen"] == r["by_expected"])
        o["stalled"] = sum(1 for r in rs if num(r["by_ms_max"], 0) > stall_limit)
        o["hang"] = sum(1 for r in rs if int(r["watchdog_killed"]) or int(num(r["stuck"], 0)) > 0)
        o["died"] = sum(1 for r in rs if int(num(r["rc"], 0)) != 0 and r["kind"] != "process")
        o["silent"] = sum(1 for r in rs if int(num(r["silent"], 0)) > 0)
        o["no_summary"] = sum(1 for r in rs if int(num(r["summary_seen"], 0)) == 0)
        o["threads_leak"] = (med([num(r["threads_last"]) for r in rs]) or 0) - (ctl_threads or 0) if ctl_threads is not None else None
        o["fds_leak"] = (med([num(r["fds_last"]) for r in rs]) or 0) - (ctl_fds or 0) if ctl_fds is not None else None
        o["rss"] = med([num(r["rss_peak_mb"]) for r in rs])
        o["wall"] = med([num(r["wall_s"]) for r in rs])
        # unit level
        ie = sum(int(num(r["inj_expected"], 0)) for r in rs)
        o["inj_expected"] = ie
        for k in ("inj_seen", "inj_detected", "inj_recovered", "inj_absorbed", "inj_error", "inj_wrong", "inj_lost"):
            o[k] = sum(int(num(r[k], 0)) for r in rs)
        o["inj_ms_med"] = med([num(r["inj_ms_median"]) for r in rs])
        o["inj_ms_p95"] = med([num(r["inj_ms_p95"]) for r in rs])
        o["cancel_ms"] = med([num(r["cancel_ms_median"]) for r in rs])
        # inference
        o["vic_injected"] = sum(int(num(r["vic_injected"], 0)) for r in rs)
        o["vic_clean"] = sum(int(num(r["vic_clean"], 0)) for r in rs)
        o["by_wrong"] = sum(int(num(r["by_wrong"], 0)) for r in rs)
        o["by_refused"] = sum(int(num(r["by_refused"], 0)) for r in rs)
        o["all_ok"] = sum(int(num(r["all_ok_calls"], 0)) for r in rs)
        o["all_refused"] = sum(int(num(r["all_refused_calls"], 0)) for r in rs)
        o["gpu_delta"] = med([num(r["gpu_delta_mb"]) for r in rs])
        o["tput"] = med([(num(r["units_done"], 0) or num(r["all_ok_calls"], 0)) / num(r["wall_s"], 1) for r in rs if num(r["wall_s"], 0)])
        out[sid] = o
    return out


def label(o):
    t = o["sid"].split("_")[-1]
    return o["fault"] + (" (%s)" % t if o["fault"] in ("none", "clock_step") else "")


def table(title, header, lines):
    print("\n### " + title + "\n")
    print("| " + " | ".join(header) + " |")
    print("|" + "|".join("---" for _ in header) + "|")
    for l in lines:
        print("| " + " | ".join(str(x) for x in l) + " |")


def main():
    rows = load(sys.argv[1])
    S = summarize(rows)
    order = sorted(S.values(), key=lambda o: (o["kind"], o["fault"], o["system"], o["config"], o["ep"]))
    print("Repetitions per scenario: see the n column. Intervals are Wilson 95%%. Rows: %d scenarios, %d runs." % (len(S), sum(o["n"] for o in S.values())))

    def agent_rows(faults):
        L = []
        for o in order:
            if o["kind"] != "agents" or o["fault"] not in faults:
                continue
            L.append([label(o), "%s/%s" % (o["system"], o["config"]), o["n"], fmt(o["contained"], o["n"]), fmt(o["hang"], o["n"]),
                      fmt(o["died"], o["n"]), fmt(o["silent"], o["n"]),
                      fmt(o["inj_detected"], o["inj_expected"]) if o["inj_expected"] else "-",
                      fmt(o["inj_lost"], o["inj_expected"]) if o["inj_expected"] else "-",
                      "%s / %s" % (o["inj_ms_med"], o["inj_ms_p95"]) if o["inj_ms_med"] is not None else "-",
                      "%+d" % o["threads_leak"] if o["threads_leak"] is not None else "-"])
        return L

    H = ["fault", "system/config", "reps", "bystanders contained (run)", "a run had a hung agent", "process died",
         "silently wrong result", "injected units detected", "injected units lost", "injected unit ms (median / p95)", "threads left vs control"]
    tool = ["none", "tool_exception", "tool_fault", "tool_timeout", "tool_huge", "tool_hang", "cancel_coop", "cancel_uncoop", "thread_fault", "thread_exit"]
    table("Agents: in-process faults (scripted model)", H, [r for r in agent_rows(tool) if True])
    net = ["none", "http500", "garbage", "truncjson", "wrongshape", "drop", "truncate", "rst", "delay", "slowloris", "hold"]
    table("Agents: model-call and network faults (loopback fake LLM)", H, agent_rows(net + ["clock_step"]))

    L = []
    for o in order:
        if o["kind"] != "infer":
            continue
        L.append([o["fault"], "%s/%s" % (o["system"], o["ep"]), o["n"], fmt(o["contained"], o["n"]) if o["fault"] != "close_inflight" else "n/a (all workers share the session)",
                  fmt(o["hang"], o["n"]), fmt(o["died"], o["n"]), fmt(o["silent"], o["n"]),
                  fmt(o["vic_clean"], o["vic_injected"]) if o["vic_injected"] else "-",
                  "%d wrong, %d refused" % (o["by_wrong"], o["by_refused"]),
                  "%s" % (o["gpu_delta"] if o["gpu_delta"] is not None else "-"), "%.1f" % (o["wall"] or 0)])
    table("Inference (shared session, 4 workers)", ["fault", "system/ep", "reps", "bystander workers unaffected", "hung worker", "process died",
                                                     "silently wrong run", "injected bad calls cleanly refused", "bystander calls", "GPU MB delta", "wall s"], L)

    L = []
    for o in order:
        if o["kind"] != "process":
            continue
        rs = rows[o["sid"]]
        k = o["fault"]
        extra = ""
        if k in ("sigkill", "sigterm"):
            extra = "exit after signal %s s; graceful %s; restart ok %s; restart %.2f s; GPU MB delta %s" % (
                med([num(r["exit_after_signal_s"]) for r in rs]), fmt(sum(int(num(r["graceful"], 0)) for r in rs), o["n"]),
                fmt(sum(int(num(r["restart_ok"], 0)) for r in rs), o["n"]), med([num(r["restart_s"]) for r in rs]) or 0,
                med([num(r["gpu_delta_mb"]) for r in rs]))
        elif k == "stdin_closed":
            extra = "EOF detected %s; empty reads %s" % (fmt(sum(int(num(r["eof_detected"], 0)) for r in rs), o["n"]), med([num(r["empty_reads"]) for r in rs]))
        elif k == "disk_full":
            extra = "writes reported failed by the checked call %s of %s; units ok %s/%s" % (
                sum(int(num(r["disk_short"], 0)) for r in rs), sum(int(num(r["disk_writes"], 0)) for r in rs),
                sum(int(num(r["by_ok"], 0)) for r in rs), sum(int(num(r["by_expected"], 0)) for r in rs))
        L.append([o["sid"], o["n"], extra, "rc %s" % med([num(r["rc"]) for r in rs])])
    table("Process-level faults", ["scenario", "reps", "result", "exit"], L)

    L = []
    for o in order:
        if o["kind"] == "agents" and o["fault"] == "none" or (o["kind"] == "infer" and o["fault"] == "none"):
            L.append([o["sid"], o["n"], "%.0f" % (o["tput"] or 0), o["rss"], "%.2f" % (o["wall"] or 0)])
    table("Fault-free throughput (overhead of the mitigations)", ["scenario", "reps", "units (or calls) per s, median", "peak RSS MB", "wall s"], L)


if __name__ == "__main__":
    main()
