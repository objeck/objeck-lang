#!/usr/bin/env python3
"""Scaling-benchmark harness for the Objeck runtime on one node, plus the
multi-node sweep generator and the speedup/efficiency report.

    scaling.py env      [--obr P] [--out env.json]
    scaling.py build    --obc P [--lib-path DIR] [--with-onnx] [--repo DIR]
    scaling.py run      SUITE [SUITE ...] --cores 1,2,4,8 [--reps 5] --out results.csv
    scaling.py report   results.csv [--md report.md]
    scaling.py multinode-specs --nodes 1,2,4,8 --cores-per-node 32 --outdir DIR
    scaling.py multinode-report SWEEPDIR [SWEEPDIR ...]

SUITES (see docs/hpc_alderaan.md for what each one answers)
    strong-kernel   fixed total work, threads = cores            (strong scaling)
    weak-kernel     work grows with the cores, threads = cores   (weak scaling)
    clbg-weak       k independent copies of a CLBG program, one per core (throughput)
    mandelbrot      the 3-thread CLBG mandelbrot under a cpuset of k cores
    onnx-sessions   k concurrent ONNX CPU sessions, one core each (throughput)
    onnx-intra      one ONNX session under a cpuset of k cores     (intra-op threads)

Every measurement runs the program inside a cpuset of exactly the requested
cores (os.sched_setaffinity, the same mechanism as taskset) and sets
OBJECK_CPU_COUNT to match, then records what the runtime saw. Python 3.6+,
standard library only.
"""
from __future__ import print_function

import argparse
import csv
import glob
import json
import os
import platform
import re
import shutil
import socket
import statistics
import subprocess
import sys
import time

try:
    import resource
except ImportError:      # Windows: no rusage, no affinity; the harness still runs unpinned
    resource = None

HERE = os.path.dirname(os.path.abspath(__file__))
# Where `build` writes the compiled .obe files and `run` finds them (HPC_BUILD overrides).
DEFAULT_BUILD = os.environ.get("HPC_BUILD", os.path.join(HERE, "build"))
WORKLOADS = os.path.join(HERE, "workloads")

RUN_COLUMNS = [
    "suite", "workload", "mode", "cores", "procs", "threads", "units", "rep", "wall_s", "user_s",
    "sys_s", "kernel_ms", "throughput", "checksum", "pinned", "usable_cpus", "host", "slurm_job_id",
    "timestamp",
]

# name -> (source relative to the repo's programs/tests/clbg, argument list)
CLBG_ARGS = {
    "nbody": ["50000000"],
    "fannkuchredux": ["11"],
    "spectralnorm": ["6000"],
}
MANDELBROT_N = "8000"


class HarnessError(Exception):
    pass


# --------------------------------------------------------------------------
# environment
# --------------------------------------------------------------------------

def allowed_cpus():
    try:
        return sorted(os.sched_getaffinity(0))
    except AttributeError:
        return list(range(os.cpu_count() or 1))


def cgroup_cpu_limit():
    """CPU quota of this process's cgroup as a float of CPUs, or None (unlimited/unknown)."""
    try:
        with open("/proc/self/cgroup") as f:
            lines = f.read().splitlines()
    except IOError:
        return None
    v2 = [l.split(":", 2)[2] for l in lines if l.startswith("0::")]
    cands = []
    if v2:
        path = v2[0].strip()
        cands.append(("v2", os.path.join("/sys/fs/cgroup", path.lstrip("/"), "cpu.max")))
        cands.append(("v2", "/sys/fs/cgroup/cpu.max"))
    for l in lines:
        parts = l.split(":", 2)
        if len(parts) == 3 and "cpu" in parts[1].split(","):
            # the mount is named cpu,cpuacct or cpu; inside a container (cgroup namespace) the
            # process's own path is not below it, only the mount root is
            for mount in ("/sys/fs/cgroup/cpu,cpuacct", "/sys/fs/cgroup/cpu"):
                cands.append(("v1", os.path.join(mount, parts[2].lstrip("/"))))
                cands.append(("v1", mount))
    for kind, p in cands:
        try:
            if kind == "v2":
                with open(p) as f:
                    quota, period = f.read().split()[:2]
                return None if quota == "max" else float(quota) / float(period)
            with open(os.path.join(p, "cpu.cfs_quota_us")) as f:
                quota = int(f.read())
            with open(os.path.join(p, "cpu.cfs_period_us")) as f:
                period = int(f.read())
            return None if quota < 0 else quota / float(period)
        except (IOError, ValueError, OSError):
            continue
    return None


def lscpu():
    try:
        out = subprocess.run(["lscpu"], stdout=subprocess.PIPE, stderr=subprocess.DEVNULL,
                             universal_newlines=True).stdout
    except OSError:
        return {}
    keep = ("Model name", "Socket(s)", "Core(s) per socket", "Thread(s) per core", "CPU(s)",
            "NUMA node(s)", "CPU max MHz", "L3 cache", "Architecture")
    info = {}
    for line in out.splitlines():
        k, _, v = line.partition(":")
        if k.strip() in keep:
            info[k.strip()] = v.strip()
    return info


def mem_total_kb():
    try:
        with open("/proc/meminfo") as f:
            return int(f.readline().split()[1])
    except (IOError, ValueError, IndexError):
        return None


def objeck_bin(obr_arg, name="obr"):
    if obr_arg:
        return obr_arg
    home = os.environ.get("OBJECK_HOME")
    if home and os.path.exists(os.path.join(home, "bin", name)):
        return os.path.join(home, "bin", name)
    return shutil.which(name) or name


def probe_usable_cpus(obr, cores=None):
    """What the Objeck runtime reports as usable under the given cpuset."""
    exe = os.path.join(DEFAULT_BUILD, "cpu_probe.obe")
    if not os.path.exists(exe):
        return None
    env = dict(os.environ)
    if cores:
        env["OBJECK_CPU_COUNT"] = str(len(cores))
    try:
        p = subprocess.run([obr, exe], stdout=subprocess.PIPE, stderr=subprocess.DEVNULL, env=env,
                           universal_newlines=True, preexec_fn=_pin_fn(cores))
    except OSError:
        return None
    m = re.search(r"usable_cpus=(\d+)", p.stdout)
    return int(m.group(1)) if m else None


def environment(obr):
    cg = cgroup_cpu_limit()
    return {
        "host": socket.gethostname(),
        "platform": platform.platform(),
        "python": platform.python_version(),
        "affinity_cpus": allowed_cpus(),
        "affinity_count": len(allowed_cpus()),
        "os_cpu_count": os.cpu_count(),
        "cgroup_cpu_limit": cg,
        "usable_cpus_runtime": probe_usable_cpus(obr),
        "OBJECK_CPU_COUNT": os.environ.get("OBJECK_CPU_COUNT"),
        "lscpu": lscpu(),
        "mem_total_kb": mem_total_kb(),
        "slurm": {k: v for k, v in sorted(os.environ.items()) if k.startswith("SLURM_")},
        "obr": obr,
    }


def cmd_env(args):
    env = environment(objeck_bin(args.obr))
    text = json.dumps(env, indent=1, sort_keys=True)
    if args.out:
        with open(args.out, "w") as f:
            f.write(text + "\n")
    print(text)
    return 0


# --------------------------------------------------------------------------
# build
# --------------------------------------------------------------------------

def find_repo(arg):
    cands = [arg] if arg else []
    cands.append(os.path.abspath(os.path.join(HERE, "..", "..", "..", "..")))
    for c in cands:
        if c and os.path.isdir(os.path.join(c, "programs", "tests", "clbg")):
            return c
    return None


def cmd_build(args):
    obc = objeck_bin(args.obc, "obc")
    os.makedirs(args.build_dir, exist_ok=True)
    env = dict(os.environ)
    if args.lib_path:
        env["OBJECK_LIB_PATH"] = args.lib_path
    jobs = [("scale_kernel", os.path.join(WORKLOADS, "scale_kernel.obs"), None),
            ("cpu_probe", os.path.join(WORKLOADS, "cpu_probe.obs"), None),
            ("sweep_task", os.path.join(WORKLOADS, "sweep_task.obs"), None)]
    repo = find_repo(args.repo)
    if repo:
        for n in list(CLBG_ARGS) + ["mandelbrot"]:
            jobs.append((n, os.path.join(repo, "programs", "tests", "clbg", n + ".obs"), None))
    else:
        print("note: no checkout with programs/tests/clbg found; the CLBG suites will be unavailable",
              file=sys.stderr)
    if args.with_onnx:
        jobs.append(("onnx_infer", os.path.join(WORKLOADS, "onnx_infer.obs"),
                     "models,opencv,onnx,nlp,nlp_onnx,json"))
    bad = 0
    for name, src, libs in jobs:
        dest = os.path.join(args.build_dir, name + ".obe")
        cmd = [obc, "-src", src, "-dest", dest]
        if libs:
            cmd += ["-lib", libs]
        p = subprocess.run(cmd, stdout=subprocess.PIPE, stderr=subprocess.STDOUT, env=env,
                           universal_newlines=True)
        ok = p.returncode == 0 and os.path.exists(dest)
        print("%-14s %s" % (name, "ok" if ok else "FAILED\n" + p.stdout))
        bad += 0 if ok else 1
    return 1 if bad else 0


# --------------------------------------------------------------------------
# measuring
# --------------------------------------------------------------------------

def _pin_fn(cores):
    if not cores or not hasattr(os, "sched_setaffinity"):
        return None
    cores = list(cores)

    def pin():
        os.sched_setaffinity(0, cores)
    return pin


def pick_cores(k, offset=0, stride=1):
    """k CPUs from the allowed set, `stride` apart (stride 2 skips SMT siblings on most
    layouts -- check with lscpu -e), starting `offset` CPUs in."""
    allowed = allowed_cpus()
    chosen = allowed[offset::stride][:k]
    if len(chosen) < k:
        raise HarnessError("need %d CPUs (stride %d) but this job may use %d: %s" % (
            k, stride, len(allowed), allowed))
    return chosen


def parse_result(text):
    found = {}
    for line in text.splitlines():
        if line.startswith("RESULT "):
            for tok in line[7:].split():
                if "=" in tok:
                    k, v = tok.split("=", 1)
                    found[k] = v
    return found


def run_group(obr, procs, env_extra=None):
    """Start every (argv, cores) at once, wait for all. Returns (wall_s, user_s, sys_s, results)."""
    before = resource.getrusage(resource.RUSAGE_CHILDREN) if resource else None
    running = []
    t0 = time.perf_counter()
    for argv, cores in procs:
        env = dict(os.environ)
        if cores:
            env["OBJECK_CPU_COUNT"] = str(len(cores))
        env.update(env_extra or {})
        running.append(subprocess.Popen([obr] + argv, stdout=subprocess.PIPE, stderr=subprocess.PIPE,
                                        env=env, universal_newlines=True, preexec_fn=_pin_fn(cores)))
    outs = []
    for p in running:
        o, e = p.communicate()
        if p.returncode != 0:
            raise HarnessError("run failed (%d): %s\n%s" % (p.returncode, " ".join(p.args), e[-400:]))
        outs.append(o)
    wall = time.perf_counter() - t0
    user = sys_ = 0.0
    if resource:
        after = resource.getrusage(resource.RUSAGE_CHILDREN)
        user = after.ru_utime - before.ru_utime
        sys_ = after.ru_stime - before.ru_stime
    return wall, user, sys_, [parse_result(o) for o in outs]


def exe(name):
    p = os.path.join(DEFAULT_BUILD, name + ".obe")
    if not os.path.exists(p):
        raise HarnessError("%s is not built: run `scaling.py build` first" % p)
    return p


class Suite(object):
    """One suite = a list of cases; a case is (mode, workload, cores, procs, threads, units, builder)."""

    def __init__(self, name, args):
        self.name = name
        self.args = args

    def cases(self, cores_list):
        a = self.args
        stride = a.stride
        for k in cores_list:
            if self.name == "strong-kernel":
                cs = pick_cores(k, a.offset, stride)
                yield dict(mode="strong", workload="scale_kernel", cores=k, procs=1, threads=k,
                           units=a.units, work=[([exe("scale_kernel"), str(k), str(a.units),
                                                  str(a.steps)], cs)])
            elif self.name == "weak-kernel":
                cs = pick_cores(k, a.offset, stride)
                units = a.units_per_core * k
                yield dict(mode="weak", workload="scale_kernel", cores=k, procs=1, threads=k,
                           units=units, work=[([exe("scale_kernel"), str(k), str(units),
                                                str(a.steps)], cs)])
            elif self.name == "clbg-weak":
                for prog in a.clbg:
                    if prog not in CLBG_ARGS:
                        raise HarnessError("unknown CLBG program %s (have %s)" % (prog, ", ".join(CLBG_ARGS)))
                    work = []
                    for i in range(k):
                        work.append(([exe(prog)] + CLBG_ARGS[prog],
                                     pick_cores(1, a.offset + i * stride, 1)))
                    yield dict(mode="throughput", workload=prog, cores=k, procs=k, threads=1,
                               units=k, work=work)
            elif self.name == "mandelbrot":
                cs = pick_cores(k, a.offset, stride)
                yield dict(mode="strong", workload="mandelbrot", cores=k, procs=1, threads=3,
                           units=1, work=[([exe("mandelbrot"), MANDELBROT_N], cs)])
            elif self.name == "onnx-sessions":
                work = []
                for i in range(k):
                    work.append(([exe("onnx_infer"), a.model, a.vocab, str(a.iterations), str(a.batch)],
                                 pick_cores(1, a.offset + i * stride, 1)))
                yield dict(mode="throughput", workload="onnx_infer", cores=k, procs=k, threads=1,
                           units=k, work=work)
            elif self.name == "onnx-intra":
                cs = pick_cores(k, a.offset, stride)
                yield dict(mode="strong", workload="onnx_infer", cores=k, procs=1, threads=k, units=1,
                           work=[([exe("onnx_infer"), a.model, a.vocab, str(a.iterations),
                                   str(a.batch)], cs)])
            else:
                raise HarnessError("unknown suite %s" % self.name)


SUITES = ["strong-kernel", "weak-kernel", "clbg-weak", "mandelbrot", "onnx-sessions", "onnx-intra"]


def cmd_run(args):
    obr = objeck_bin(args.obr)
    cores_list = [int(c) for c in args.cores.split(",")]
    new = not os.path.exists(args.out) or args.fresh
    if args.fresh and os.path.exists(args.out):
        os.remove(args.out)
    f = open(args.out, "a", newline="")
    w = csv.DictWriter(f, fieldnames=RUN_COLUMNS)
    if new:
        w.writeheader()
    host = socket.gethostname()
    jobid = os.environ.get("SLURM_JOB_ID", "")
    bad = 0
    for sname in args.suites:
        if sname not in SUITES:
            raise HarnessError("unknown suite %s (have %s)" % (sname, ", ".join(SUITES)))
        # one case list per suite; the order of cores is shuffled between repetitions so a
        # drifting clock or a noisy neighbour does not line up with one core count
        for case in Suite(sname, args).cases(cores_list):
            usable = probe_usable_cpus(obr, case["work"][0][1])
            ref = None
            for rep in range(args.warmup + args.reps):
                wall, user, sys_, results = run_group(obr, case["work"])
                if rep < args.warmup:
                    continue
                kms = max([int(r["kernel_ms"]) for r in results if "kernel_ms" in r] or [0])
                thr = sum(float(r["per_sec"]) for r in results if "per_sec" in r) or ""
                checks = sorted(set(r["checksum"] for r in results if "checksum" in r))
                checksum = ",".join(checks)
                if sname == "strong-kernel":
                    ref = ref or checksum
                    if checksum != ref:
                        print("CHECKSUM MISMATCH %s k=%d: %s vs %s" % (sname, case["cores"], checksum, ref),
                              file=sys.stderr)
                        bad += 1
                w.writerow({
                    "suite": sname, "workload": case["workload"], "mode": case["mode"],
                    "cores": case["cores"], "procs": case["procs"], "threads": case["threads"],
                    "units": case["units"], "rep": rep - args.warmup, "wall_s": round(wall, 4),
                    "user_s": round(user, 4), "sys_s": round(sys_, 4), "kernel_ms": kms,
                    "throughput": thr, "checksum": checksum,
                    "pinned": "affinity" if hasattr(os, "sched_setaffinity") else "none",
                    "usable_cpus": usable if usable is not None else "", "host": host,
                    "slurm_job_id": jobid, "timestamp": time.strftime("%Y-%m-%dT%H:%M:%S"),
                })
                f.flush()
            print("%-14s %-14s cores=%-3d done" % (sname, case["workload"], case["cores"]))
    f.close()
    return 1 if bad else 0


# --------------------------------------------------------------------------
# report
# --------------------------------------------------------------------------

def _cv(xs):
    return (statistics.pstdev(xs) / statistics.mean(xs) * 100.0) if len(xs) > 1 and statistics.mean(xs) else 0.0


def summarize_runs(rows):
    """rows of one CSV -> {(suite, workload, mode): [ {cores, n, median, cv, speedup, eff, ...} ]}"""
    groups = {}
    for r in rows:
        groups.setdefault((r["suite"], r["workload"], r["mode"]), {}).setdefault(int(r["cores"]), []).append(r)
    out = {}
    for key, by_cores in groups.items():
        ks = sorted(by_cores)
        base_k = ks[0]
        base = statistics.median([float(r["wall_s"]) for r in by_cores[base_k]])
        table = []
        for k in ks:
            rs = by_cores[k]
            walls = [float(r["wall_s"]) for r in rs]
            med = statistics.median(walls)
            ratio = base / med if med else 0.0
            if key[2] == "strong":
                speedup = ratio
                eff = ratio / (k / float(base_k))
            else:   # weak and throughput: work grows with k, ideal time is constant
                speedup = ratio * (k / float(base_k))
                eff = ratio
            thr = [float(r["throughput"]) for r in rs if r["throughput"] not in ("", None)]
            table.append({"cores": k, "n": len(rs), "median_s": med, "min_s": min(walls),
                          "cv_pct": _cv(walls), "speedup": speedup, "efficiency": eff,
                          "throughput": statistics.median(thr) if thr else None,
                          "checksums": sorted(set(r["checksum"] for r in rs)),
                          "usable": sorted(set(r["usable_cpus"] for r in rs))})
        out[key] = table
    return out


def render_markdown(summary, title="Scaling results"):
    lines = ["# %s" % title, ""]
    for (suite, workload, mode), table in sorted(summary.items()):
        eff_name = "parallel efficiency" if mode == "strong" else "weak-scaling efficiency (T1/Tk)"
        has_thr = any(t["throughput"] for t in table)
        head = "| cores | reps | median s | min s | CV %% | speedup | %s | usable cpus (runtime) |" % eff_name
        sep = "|---:|---:|---:|---:|---:|---:|---:|---|"
        if has_thr:
            head += " inferences/s |"
            sep += "---:|"
        lines += ["## %s / %s (%s)" % (suite, workload, mode), "", head, sep]
        for t in table:
            row = "| %d | %d | %.3f | %.3f | %.1f | %.2f | %.0f%% | %s |" % (
                t["cores"], t["n"], t["median_s"], t["min_s"], t["cv_pct"], t["speedup"],
                t["efficiency"] * 100, ",".join(t["usable"]) or "n/a")
            if has_thr:
                row += " %s |" % ("%.0f" % t["throughput"] if t["throughput"] else "")
            lines.append(row)
        flags = []
        if suite == "strong-kernel" and any(len(t["checksums"]) > 1 for t in table):
            flags.append("CHECKSUM DIFFERS WITHIN A CORE COUNT")
        if len(set(c for t in table for c in t["checksums"])) > 1 and suite == "strong-kernel":
            flags.append("CHECKSUM DIFFERS ACROSS CORE COUNTS: the speedups above are not valid")
        if any(t["cv_pct"] > 5 for t in table):
            flags.append("CV above 5%: noisy (shared node? frequency scaling?); rerun with more reps")
        if any(t["usable"] and t["usable"] != [str(t["cores"])] for t in table):
            flags.append("the runtime's usable CPU count differs from the requested cores in a row")
        lines += [""] + ["- WARNING: " + f for f in flags] + [""]
    return "\n".join(lines)


def cmd_report(args):
    with open(args.csv, newline="") as f:
        rows = list(csv.DictReader(f))
    if not rows:
        raise HarnessError("%s has no rows" % args.csv)
    text = render_markdown(summarize_runs(rows), "Scaling results: %s" % os.path.basename(args.csv))
    envs = sorted(set((r["host"], r["slurm_job_id"]) for r in rows))
    text += "\nMeasured on: %s\n" % "; ".join("%s (job %s)" % (h, j or "-") for h, j in envs)
    if args.md:
        with open(args.md, "w") as f:
            f.write(text)
    print(text)
    return 0


# --------------------------------------------------------------------------
# multi-node sweeps through objeck-slurm
# --------------------------------------------------------------------------

def cmd_multinode_specs(args):
    nodes = [int(n) for n in args.nodes.split(",")]
    cpn = args.cores_per_node
    os.makedirs(args.outdir, exist_ok=True)
    obe = args.program or os.path.join(DEFAULT_BUILD, "scale_kernel.obe")
    scripts = []
    for mode in ("strong", "weak"):
        for k in nodes:
            tasks = args.tasks if mode == "strong" else args.tasks_per_node * k
            spec = {
                "name": "ms_%s_k%d" % (mode, k),
                "program": obe,
                "runner": ["obr"],
                "args": [str(cpn), str(args.units), str(args.steps)],
                # one array element = one whole node: every core, then k elements run at once
                "grid": {"task": list(range(tasks))},
                "resources": {"cpus_per_task": cpn, "mem": args.mem, "time": args.time,
                              "throttle": k, "partition": args.partition,
                              "extra_sbatch": ["--exclusive"]},
                "env": {"OBJECK_CPU_COUNT": str(cpn)},
            }
            if args.container:
                spec["container"] = {"image": args.container}
            if args.partition is None:
                del spec["resources"]["partition"]
            path = os.path.join(args.outdir, spec["name"] + ".json")
            with open(path, "w") as f:
                json.dump(spec, f, indent=1)
            scripts.append(path)
    submit = os.path.join(args.outdir, "submit_all.sh")
    launcher = os.path.join(HERE, "..", "objeck_slurm.py")
    with open(submit, "w") as f:
        f.write("#!/bin/bash\n# Submit every multi-node scaling sweep. They are independent;\n"
                "# for clean numbers run them one after another (afterany chaining is up to you).\n")
        for p in scripts:
            f.write("python3 %s submit %s\n" % (os.path.abspath(launcher), os.path.abspath(p)))
    os.chmod(submit, 0o755)
    print("wrote %d specs and %s" % (len(scripts), submit))
    return 0


def parse_utc(s):
    import datetime
    return datetime.datetime.strptime(s, "%Y-%m-%dT%H:%M:%S.%fZ")


def cmd_multinode_report(args):
    rows = []
    for d in args.sweepdirs:
        with open(os.path.join(d, "manifest.json")) as f:
            m = json.load(f)
        with open(os.path.join(d, "results.csv"), newline="") as f:
            recs = list(csv.DictReader(f))
        ok = [r for r in recs if r["status"] == "ok"]
        if len(ok) != len(recs):
            print("WARNING: %s has %d/%d ok tasks; its makespan is not comparable" % (d, len(ok), len(recs)),
                  file=sys.stderr)
        if not ok:
            continue
        start = min(parse_utc(r["start_utc"]) for r in ok)
        end = max(parse_utc(r["end_utc"]) for r in ok)
        name = m["name"]
        mode = "strong" if "_strong_" in name else "weak"
        rows.append({"mode": mode, "k": int(m["resources"]["throttle"] or 1), "tasks": len(recs),
                     "makespan": (end - start).total_seconds(),
                     "hosts": len(set(r["host"] for r in ok)),
                     "task_median": statistics.median(float(r["wall_s"]) for r in ok)})
    lines = []
    for mode in ("strong", "weak"):
        rs = sorted([r for r in rows if r["mode"] == mode], key=lambda r: r["k"])
        if not rs:
            continue
        base = rs[0]
        lines += ["## multi-node %s scaling" % mode, "",
                  "| nodes (array throttle) | tasks | distinct hosts | makespan s | median task s | speedup | efficiency |",
                  "|---:|---:|---:|---:|---:|---:|---:|"]
        for r in rs:
            ratio = base["makespan"] / r["makespan"] if r["makespan"] else 0.0
            if mode == "strong":
                sp, eff = ratio, ratio / (r["k"] / float(base["k"]))
            else:
                sp, eff = ratio * (r["k"] / float(base["k"])), ratio
            lines.append("| %d | %d | %d | %.1f | %.2f | %.2f | %.0f%% |" % (
                r["k"], r["tasks"], r["hosts"], r["makespan"], r["task_median"], sp, eff * 100))
        lines.append("")
    print("\n".join(lines))
    return 0


def main(argv=None):
    ap = argparse.ArgumentParser(prog="scaling.py", description=__doc__.split("\n\n")[0])
    sub = ap.add_subparsers(dest="cmd")
    sub.required = True

    p = sub.add_parser("env")
    p.add_argument("--obr")
    p.add_argument("--out")
    p.set_defaults(fn=cmd_env)

    p = sub.add_parser("build")
    p.add_argument("--obc")
    p.add_argument("--lib-path")
    p.add_argument("--repo")
    p.add_argument("--build-dir", default=DEFAULT_BUILD)
    p.add_argument("--with-onnx", action="store_true")
    p.set_defaults(fn=cmd_build)

    p = sub.add_parser("run")
    p.add_argument("suites", nargs="+", metavar="SUITE", help="one of: " + ", ".join(SUITES))
    p.add_argument("--cores", default="1,2,4,8")
    p.add_argument("--reps", type=int, default=5)
    p.add_argument("--warmup", type=int, default=1)
    p.add_argument("--out", default="results.csv")
    p.add_argument("--fresh", action="store_true", help="overwrite --out instead of appending")
    p.add_argument("--obr")
    p.add_argument("--offset", type=int, default=0, help="first CPU index within the allowed set")
    p.add_argument("--stride", type=int, default=1, help="2 skips SMT siblings on interleaved layouts")
    p.add_argument("--units", type=int, default=256, help="strong-kernel: total work units")
    p.add_argument("--units-per-core", type=int, default=32, help="weak-kernel: units per core")
    p.add_argument("--steps", type=int, default=20000000, help="kernel: iterations per unit")
    p.add_argument("--clbg", nargs="+", default=["nbody", "fannkuchredux", "spectralnorm"])
    p.add_argument("--model")
    p.add_argument("--vocab")
    p.add_argument("--iterations", type=int, default=2000)
    p.add_argument("--batch", type=int, default=8)
    p.set_defaults(fn=cmd_run)

    p = sub.add_parser("report")
    p.add_argument("csv")
    p.add_argument("--md")
    p.set_defaults(fn=cmd_report)

    p = sub.add_parser("multinode-specs")
    p.add_argument("--nodes", default="1,2,4,8")
    p.add_argument("--cores-per-node", type=int, required=True)
    p.add_argument("--tasks", type=int, default=64, help="strong scaling: total tasks")
    p.add_argument("--tasks-per-node", type=int, default=8, help="weak scaling: tasks per node")
    p.add_argument("--units", type=int, default=128)
    p.add_argument("--steps", type=int, default=400000)
    p.add_argument("--mem", default="0", help="--mem value; 0 = the whole node")
    p.add_argument("--time", default="02:00:00")
    p.add_argument("--partition")
    p.add_argument("--container", help="path of the .sif to run under singularity exec")
    p.add_argument("--program")
    p.add_argument("--outdir", required=True)
    p.set_defaults(fn=cmd_multinode_specs)

    p = sub.add_parser("multinode-report")
    p.add_argument("sweepdirs", nargs="+")
    p.set_defaults(fn=cmd_multinode_report)

    args = ap.parse_args(argv)
    try:
        return args.fn(args)
    except HarnessError as e:
        print("scaling.py: %s" % e, file=sys.stderr)
        return 2


if __name__ == "__main__":
    sys.exit(main())
