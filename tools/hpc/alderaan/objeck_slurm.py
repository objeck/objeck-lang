#!/usr/bin/env python3
"""objeck-slurm: turn a parameter sweep into a Slurm job array, and collect the
results into one CSV.

    objeck_slurm.py plan    SPEC [--outdir D] [--dry-run]
    objeck_slurm.py submit  SPEC [--outdir D] [--dry-run] [--sbatch CMD] [--only-failed]
    objeck_slurm.py local   SPEC [--outdir D] [--workers N]
    objeck_slurm.py run-task --manifest M [--index I]      (what the job array runs)
    objeck_slurm.py collect OUTDIR
    objeck_slurm.py status  OUTDIR

SPEC is a JSON file (see docs/hpc_alderaan.md). The same task runner executes a
task under Slurm (array element = $SLURM_ARRAY_TASK_ID) and under `local`
(a thread pool of subprocesses), so a sweep tested locally takes the same code
path on the cluster.

Python 3.6 or newer, standard library only: that is what CentOS 8 ships.
"""
from __future__ import print_function

import argparse
import csv
import datetime
import itertools
import json
import math
import os
import re
import shlex
import socket
import subprocess
import sys
import threading
import time

VERSION = "1"
PARAM_RE = re.compile(r"\{([A-Za-z_][A-Za-z0-9_]*)\}")
FIXED_COLUMNS = [
    "task_id", "status", "exit_code", "wall_s", "start_utc", "end_utc", "host",
    "slurm_job_id", "slurm_array_job_id", "slurm_array_task_id", "cpus_per_task",
]
DEFAULT_RESOURCES = {
    "cpus_per_task": 1,
    "mem": None,            # e.g. "8G"; or use mem_per_cpu
    "mem_per_cpu": None,
    "time": "01:00:00",
    "partition": None,
    "account": None,
    "qos": None,
    "constraint": None,
    "gres": None,           # e.g. "gpu:1"
    "throttle": None,       # the %K in --array=0-N%K
    "pack": 1,              # tasks run back to back by one array element
    "extra_sbatch": [],     # raw "#SBATCH <x>" lines, without the prefix
}


class SweepError(Exception):
    pass


# --------------------------------------------------------------------------
# spec -> tasks
# --------------------------------------------------------------------------

def usable_cpus():
    """CPUs this process may use: the affinity mask, else the machine count."""
    try:
        n = len(os.sched_getaffinity(0))
    except (AttributeError, OSError):
        n = os.cpu_count() or 1
    env = os.environ.get("OBJECK_CPU_COUNT")
    if env and env.isdigit() and int(env) > 0:
        n = min(n, int(env))
    return max(1, n)


def load_spec(path):
    try:
        with open(path) as f:
            spec = json.load(f)
    except (IOError, ValueError) as e:
        raise SweepError("cannot read sweep spec %s: %s" % (path, e))
    if not isinstance(spec, dict):
        raise SweepError("sweep spec must be a JSON object")
    spec["_dir"] = os.path.dirname(os.path.abspath(path))
    return spec


def _rel(spec, p):
    """A path in the spec is relative to the spec file."""
    p = os.path.expanduser(p)
    return p if os.path.isabs(p) else os.path.normpath(os.path.join(spec["_dir"], p))


def expand_params(spec):
    """The ordered list of parameter dicts: grid x repeats, sets, or a CSV."""
    sources = [k for k in ("grid", "sets", "csv") if k in spec]
    if len(sources) != 1:
        raise SweepError("the spec needs exactly one of 'grid', 'sets' or 'csv'")
    kind = sources[0]
    if kind == "grid":
        grid = spec["grid"]
        if not isinstance(grid, dict) or not grid:
            raise SweepError("'grid' must be an object of name -> list of values")
        names = list(grid.keys())
        for n in names:
            if not isinstance(grid[n], list) or not grid[n]:
                raise SweepError("grid parameter '%s' must be a non-empty list" % n)
        sets = [dict(zip(names, combo)) for combo in itertools.product(*[grid[n] for n in names])]
    elif kind == "sets":
        sets = spec["sets"]
        if not isinstance(sets, list) or not all(isinstance(s, dict) for s in sets) or not sets:
            raise SweepError("'sets' must be a non-empty list of objects")
        sets = [dict(s) for s in sets]
    else:
        path = _rel(spec, spec["csv"])
        try:
            with open(path, newline="") as f:
                sets = [dict(row) for row in csv.DictReader(f)]
        except IOError as e:
            raise SweepError("cannot read parameter CSV %s: %s" % (path, e))
        if not sets:
            raise SweepError("parameter CSV %s has no rows" % path)
    repeats = int(spec.get("repeats", 1))
    if repeats < 1:
        raise SweepError("'repeats' must be at least 1")
    out = []
    for s in sets:
        for rep in range(repeats):
            p = dict(s)
            if repeats > 1:
                p.setdefault("rep", rep)
            out.append(p)
    for p in out:
        for k in p:
            if not re.match(r"^[A-Za-z_][A-Za-z0-9_]*$", k):
                raise SweepError("parameter name '%s' is not an identifier" % k)
    return out


def substitute(template, values):
    def repl(m):
        key = m.group(1)
        if key not in values:
            raise SweepError("argument template '%s' names unknown parameter '%s'" % (template, key))
        return str(values[key])
    return PARAM_RE.sub(repl, template)


def build_tasks(spec):
    params = expand_params(spec)
    arg_templates = [str(a) for a in spec.get("args", [])]
    tasks = []
    for i, p in enumerate(params):
        values = dict(p)
        values["task_id"] = i
        tasks.append({"id": i, "params": p, "args": [substitute(a, values) for a in arg_templates]})
    return tasks


def resources(spec):
    r = dict(DEFAULT_RESOURCES)
    r.update(spec.get("resources", {}))
    r["cpus_per_task"] = int(r["cpus_per_task"])
    r["pack"] = max(1, int(r["pack"]))
    if r["cpus_per_task"] < 1:
        raise SweepError("resources.cpus_per_task must be at least 1")
    if r["throttle"] is not None and int(r["throttle"]) < 1:
        raise SweepError("resources.throttle must be at least 1")
    if r["mem"] and r["mem_per_cpu"]:
        raise SweepError("set resources.mem or resources.mem_per_cpu, not both")
    if not re.match(r"^(\d+-)?(\d+:)?\d+(:\d+)?(:\d+)?$", str(r["time"])):
        raise SweepError("resources.time '%s' is not a Slurm time (MM, HH:MM:SS, D-HH:MM:SS)" % r["time"])
    return r


# --------------------------------------------------------------------------
# manifest + sbatch script
# --------------------------------------------------------------------------

def make_manifest(spec, outdir):
    tasks = build_tasks(spec)
    res = resources(spec)
    if "program" not in spec:
        raise SweepError("the spec needs a 'program'")
    runner = spec.get("runner", ["obr"])
    if isinstance(runner, str):
        runner = shlex.split(runner)
    program = spec["program"]
    # a program that is a path (has a separator or an extension) is relative to the spec
    if os.sep in program or "/" in program or program.endswith((".obe", ".obs", ".py", ".sh")):
        program = _rel(spec, program)
    workdir = _rel(spec, spec.get("workdir", "."))
    container = spec.get("container")
    if container:
        container = dict(container)
        if "image" not in container:
            raise SweepError("container.image is required")
        container["image"] = os.path.expanduser(container["image"])
        container.setdefault("cmd", "singularity")
        container["binds"] = list(container.get("binds", []))
        home = os.path.expanduser("~")
        for d in (outdir, workdir):
            d = os.path.abspath(d)
            if not (d == home or d.startswith(home + os.sep) or d.startswith("/tmp")):
                if not any(b.split(":")[0] == d for b in container["binds"]):
                    container["binds"].append(d)
    return {
        "format": VERSION,
        "name": re.sub(r"[^A-Za-z0-9_.-]", "_", str(spec.get("name", "objeck-sweep"))),
        "outdir": os.path.abspath(outdir),
        "workdir": workdir,
        "program": program,
        "runner": runner,
        "container": container,
        "env": {str(k): str(v) for k, v in spec.get("env", {}).items()},
        "modules": list(spec.get("modules", [])),
        "python": spec.get("python", "python3"),
        "task_timeout": spec.get("task_timeout"),
        "resources": res,
        "tasks": tasks,
    }


def element_count(manifest):
    return int(math.ceil(len(manifest["tasks"]) / float(manifest["resources"]["pack"])))


def array_spec(manifest, elements=None):
    res = manifest["resources"]
    if elements is None:
        n = element_count(manifest)
        base = "0-%d" % (n - 1) if n > 1 else "0"
    else:
        base = ",".join(str(e) for e in elements)
    if res["throttle"]:
        base += "%%%d" % int(res["throttle"])
    return base


def sbatch_script(manifest, elements=None, launcher=None):
    res = manifest["resources"]
    out = manifest["outdir"]
    launcher = launcher or os.path.abspath(__file__)
    lines = ["#!/bin/bash", "# generated by objeck-slurm %s -- do not edit, regenerate with `plan`" % VERSION]

    def sb(opt):
        lines.append("#SBATCH " + opt)

    sb("--job-name=%s" % manifest["name"])
    if res["partition"]:
        sb("--partition=%s" % res["partition"])
    if res["account"]:
        sb("--account=%s" % res["account"])
    if res["qos"]:
        sb("--qos=%s" % res["qos"])
    sb("--nodes=1")
    sb("--ntasks=1")
    sb("--cpus-per-task=%d" % res["cpus_per_task"])
    if res["mem"]:
        sb("--mem=%s" % res["mem"])
    if res["mem_per_cpu"]:
        sb("--mem-per-cpu=%s" % res["mem_per_cpu"])
    if res["constraint"]:
        sb("--constraint=%s" % res["constraint"])
    if res["gres"]:
        sb("--gres=%s" % res["gres"])
    sb("--time=%s" % res["time"])
    sb("--array=%s" % array_spec(manifest, elements))
    sb("--output=%s/logs/slurm_%%A_%%a.out" % out)
    sb("--error=%s/logs/slurm_%%A_%%a.err" % out)
    for extra in res["extra_sbatch"]:
        sb(extra)
    lines += ["", "set -eu", "cd %s" % shlex.quote(manifest["workdir"])]
    for m in manifest["modules"]:
        lines.append("module load %s" % shlex.quote(m))
    lines += [
        "",
        "# The Objeck VM sizes its thread pools from this: the CPUs Slurm gave this task,",
        "# not the node's (UsableCpuCount also honours the cgroup, this makes it explicit).",
        'export OBJECK_CPU_COUNT="${SLURM_CPUS_PER_TASK:-%d}"' % res["cpus_per_task"],
    ]
    for k, v in sorted(manifest["env"].items()):
        lines.append("export %s=%s" % (k, shlex.quote(v)))
    c = manifest["container"]
    if c:
        lines.append("# each task runs as: %s" % " ".join(shlex.quote(a) for a in task_command(manifest, manifest["tasks"][0])))
    lines += [
        "",
        "%s %s run-task --manifest %s" % (
            shlex.quote(manifest["python"]), shlex.quote(launcher),
            shlex.quote(os.path.join(out, "manifest.json"))),
        "",
    ]
    return "\n".join(lines)


def task_command(manifest, task):
    cmd = list(manifest["runner"]) + [manifest["program"]] + list(task["args"])
    c = manifest["container"]
    if c:
        wrap = [c["cmd"], "exec"]
        if c.get("nv"):
            wrap.append("--nv")
        for b in c["binds"]:
            wrap += ["--bind", b]
        wrap.append(c["image"])
        cmd = wrap + cmd
    return cmd


# --------------------------------------------------------------------------
# running one task (shared by Slurm and local mode)
# --------------------------------------------------------------------------

def parse_results(text):
    """`RESULT k=v k=v ...` lines -> dict (later lines win); else the last line."""
    found = {}
    last = ""
    for line in text.splitlines():
        s = line.strip()
        if not s:
            continue
        last = s
        if s.startswith("RESULT "):
            for tok in s[7:].split():
                if "=" in tok:
                    k, v = tok.split("=", 1)
                    found[k] = v
    if not found and last:
        found["stdout_last"] = last[:200]
    return found


def _now():
    return datetime.datetime.now(datetime.timezone.utc).strftime("%Y-%m-%dT%H:%M:%S.%fZ")


def run_task(manifest, task, slurm=None):
    """Run one task, write results/task_NNNNN.json, return the record."""
    outdir = manifest["outdir"]
    for d in ("logs", "results"):
        os.makedirs(os.path.join(outdir, d), exist_ok=True)
    tid = task["id"]
    env = dict(os.environ)
    res = manifest["resources"]
    cpus = res["cpus_per_task"]
    if "SLURM_CPUS_PER_TASK" in env:
        cpus = int(env["SLURM_CPUS_PER_TASK"])
    # The task gets exactly the CPUs it was allocated, whatever the parent's environment
    # said (a local sweep must not inherit a count meant for the whole pool).
    env["OBJECK_CPU_COUNT"] = str(cpus)
    env.update(manifest["env"])
    env["OBJECK_SWEEP_TASK_ID"] = str(tid)
    for k, v in task["params"].items():
        env["OBJECK_SWEEP_%s" % k.upper()] = str(v)
    # Singularity forwards host variables unprefixed; Apptainer too. Be explicit for both.
    env["SINGULARITYENV_OBJECK_CPU_COUNT"] = env["OBJECK_CPU_COUNT"]
    env["APPTAINERENV_OBJECK_CPU_COUNT"] = env["OBJECK_CPU_COUNT"]

    cmd = task_command(manifest, task)
    out_path = os.path.join(outdir, "logs", "task_%05d.out" % tid)
    err_path = os.path.join(outdir, "logs", "task_%05d.err" % tid)
    start = _now()
    t0 = time.time()
    status, code = "ok", 0
    timeout = manifest.get("task_timeout")
    try:
        with open(out_path, "w") as fo, open(err_path, "w") as fe:
            try:
                p = subprocess.run(cmd, stdout=fo, stderr=fe, env=env, cwd=manifest["workdir"],
                                   timeout=timeout)
                code = p.returncode
                if code != 0:
                    status = "failed"
            except subprocess.TimeoutExpired:
                status, code = "timeout", -1
    except OSError as e:
        status, code = "failed", 127
        with open(err_path, "a") as fe:
            fe.write("objeck-slurm: cannot start %s: %s\n" % (cmd[0], e))
    wall = time.time() - t0
    with open(out_path) as f:
        results = parse_results(f.read())

    s = slurm or {}
    rec = {
        "task_id": tid,
        "status": status,
        "exit_code": code,
        "wall_s": round(wall, 4),
        "start_utc": start,
        "end_utc": _now(),
        "host": socket.gethostname(),
        "slurm_job_id": s.get("job_id", env.get("SLURM_JOB_ID", "")),
        "slurm_array_job_id": s.get("array_job_id", env.get("SLURM_ARRAY_JOB_ID", "")),
        "slurm_array_task_id": s.get("array_task_id", env.get("SLURM_ARRAY_TASK_ID", "")),
        "cpus_per_task": cpus,
        "params": task["params"],
        "results": results,
    }
    path = os.path.join(outdir, "results", "task_%05d.json" % tid)
    tmp = path + ".tmp%d" % os.getpid()
    with open(tmp, "w") as f:
        json.dump(rec, f, sort_keys=True)
    os.replace(tmp, path)   # a half-written record is never visible
    return rec


def element_tasks(manifest, index):
    pack = manifest["resources"]["pack"]
    return manifest["tasks"][index * pack:(index + 1) * pack]


# --------------------------------------------------------------------------
# collect / status
# --------------------------------------------------------------------------

def read_records(outdir):
    rdir = os.path.join(outdir, "results")
    recs = {}
    if os.path.isdir(rdir):
        for name in sorted(os.listdir(rdir)):
            if name.startswith("task_") and name.endswith(".json"):
                try:
                    with open(os.path.join(rdir, name)) as f:
                        r = json.load(f)
                    recs[r["task_id"]] = r
                except (ValueError, KeyError, IOError):
                    pass   # unreadable record: counted as missing
    return recs


def collect(outdir, csv_path=None):
    manifest = load_manifest(outdir)
    recs = read_records(outdir)
    param_cols = []
    result_cols = []
    for t in manifest["tasks"]:
        for k in t["params"]:
            if k not in param_cols:
                param_cols.append(k)
    for r in recs.values():
        for k in r["results"]:
            if k not in result_cols:
                result_cols.append(k)
    taken = set(FIXED_COLUMNS) | set(param_cols)
    header = FIXED_COLUMNS + param_cols
    rename = {}
    for k in result_cols:
        name = k if k not in taken else "result_" + k
        rename[k] = name
        header.append(name)
    csv_path = csv_path or os.path.join(outdir, "results.csv")
    with open(csv_path, "w", newline="") as f:
        w = csv.writer(f)
        w.writerow(header)
        for t in manifest["tasks"]:
            r = recs.get(t["id"])
            if r is None:
                row = {"task_id": t["id"], "status": "missing"}
                row.update(t["params"])
            else:
                row = {c: r.get(c, "") for c in FIXED_COLUMNS}
                row.update(r["params"])
                for k, v in r["results"].items():
                    row[rename[k]] = v
            w.writerow([row.get(c, "") for c in header])
    return csv_path, summarize(manifest, recs)


def summarize(manifest, recs):
    total = len(manifest["tasks"])
    ok = [i for i, r in recs.items() if r["status"] == "ok"]
    bad = sorted(i for i, r in recs.items() if r["status"] != "ok")
    missing = sorted(t["id"] for t in manifest["tasks"] if t["id"] not in recs)
    return {"total": total, "ok": len(ok), "failed": bad, "missing": missing}


def unfinished_elements(manifest, recs):
    pack = manifest["resources"]["pack"]
    todo = []
    for e in range(element_count(manifest)):
        for t in element_tasks(manifest, e):
            r = recs.get(t["id"])
            if r is None or r["status"] != "ok":
                todo.append(e)
                break
    return todo


def load_manifest(outdir):
    path = os.path.join(outdir, "manifest.json")
    try:
        with open(path) as f:
            return json.load(f)
    except (IOError, ValueError) as e:
        raise SweepError("cannot read %s: %s (run `plan` or `submit` first)" % (path, e))


# --------------------------------------------------------------------------
# commands
# --------------------------------------------------------------------------

def default_outdir(spec, args):
    if args.outdir:
        return os.path.abspath(args.outdir)
    return os.path.join(spec["_dir"], "sweep_" + re.sub(r"[^A-Za-z0-9_.-]", "_", str(spec.get("name", "run"))))


def write_plan(manifest, elements=None):
    out = manifest["outdir"]
    os.makedirs(os.path.join(out, "logs"), exist_ok=True)
    os.makedirs(os.path.join(out, "results"), exist_ok=True)
    with open(os.path.join(out, "manifest.json"), "w") as f:
        json.dump(manifest, f, indent=1, sort_keys=True)
    script = os.path.join(out, "job.sbatch")
    with open(script, "w") as f:
        f.write(sbatch_script(manifest, elements))
    os.chmod(script, 0o755)
    return script


def describe(manifest, elements=None):
    res = manifest["resources"]
    n = len(manifest["tasks"])
    print("sweep       : %s" % manifest["name"])
    print("tasks       : %d  (%d array elements, pack=%d)" % (n, element_count(manifest), res["pack"]))
    print("array       : --array=%s" % array_spec(manifest, elements))
    print("per element : %d cpus, mem=%s, time=%s" % (
        res["cpus_per_task"], res["mem"] or res["mem_per_cpu"] or "default", res["time"]))
    print("first task  : %s" % " ".join(shlex.quote(a) for a in task_command(manifest, manifest["tasks"][0])))
    print("output dir  : %s" % manifest["outdir"])


def cmd_plan(args):
    spec = load_spec(args.spec)
    manifest = make_manifest(spec, default_outdir(spec, args))
    describe(manifest)
    if args.dry_run:
        print("\n--- job.sbatch (dry run, nothing written) ---")
        print(sbatch_script(manifest))
        print("--- tasks ---")
        for t in manifest["tasks"][:20]:
            print("%5d  %s" % (t["id"], json.dumps(t["params"], sort_keys=True)))
        if len(manifest["tasks"]) > 20:
            print("  ... %d more" % (len(manifest["tasks"]) - 20))
        return 0
    script = write_plan(manifest)
    print("wrote %s" % script)
    return 0


def cmd_submit(args):
    spec = load_spec(args.spec)
    outdir = default_outdir(spec, args)
    manifest = make_manifest(spec, outdir)
    elements = None
    if args.only_failed:
        old = load_manifest(outdir)
        manifest = old
        elements = unfinished_elements(manifest, read_records(outdir))
        if not elements:
            print("nothing to resubmit: every task has an ok result")
            return 0
    describe(manifest, elements)
    if args.dry_run:
        print("\n--- job.sbatch (dry run, nothing written or submitted) ---")
        print(sbatch_script(manifest, elements))
        print("would run: %s %s" % (args.sbatch, os.path.join(manifest["outdir"], "job.sbatch")))
        return 0
    script = write_plan(manifest, elements)
    try:
        p = subprocess.run(shlex.split(args.sbatch) + ["--parsable", script],
                           stdout=subprocess.PIPE, stderr=subprocess.PIPE, universal_newlines=True)
    except OSError as e:
        raise SweepError("cannot run %s: %s" % (args.sbatch, e))
    if p.returncode != 0:
        raise SweepError("sbatch failed (%d): %s" % (p.returncode, p.stderr.strip()))
    m = re.search(r"(\d+)", p.stdout)
    if not m:
        raise SweepError("could not read a job id from sbatch output: %r" % p.stdout)
    job_id = m.group(1)
    manifest["last_job_id"] = job_id
    with open(os.path.join(manifest["outdir"], "manifest.json"), "w") as f:
        json.dump(manifest, f, indent=1, sort_keys=True)
    print("submitted job %s" % job_id)
    print("when it finishes: %s collect %s" % (os.path.basename(sys.argv[0]), manifest["outdir"]))
    return 0


def cmd_run_task(args):
    with open(args.manifest) as f:
        manifest = json.load(f)
    if args.index is not None:
        index = args.index
    elif "SLURM_ARRAY_TASK_ID" in os.environ:
        index = int(os.environ["SLURM_ARRAY_TASK_ID"])
    else:
        raise SweepError("no --index and SLURM_ARRAY_TASK_ID is not set")
    tasks = element_tasks(manifest, index)
    if not tasks:
        raise SweepError("array element %d has no tasks (the sweep has %d)" % (index, len(manifest["tasks"])))
    rc = 0
    for t in tasks:
        rec = run_task(manifest, t)
        print("task %d: %s in %.2fs" % (t["id"], rec["status"], rec["wall_s"]))
        if rec["status"] != "ok":
            rc = 1
    return rc


def cmd_local(args):
    spec = load_spec(args.spec)
    outdir = default_outdir(spec, args)
    manifest = make_manifest(spec, outdir)
    write_plan(manifest)
    cpt = manifest["resources"]["cpus_per_task"]
    workers = args.workers or max(1, usable_cpus() // cpt)
    if manifest["resources"]["throttle"] and not args.workers:
        workers = min(workers, int(manifest["resources"]["throttle"]))
    print("local: %d tasks, %d workers x %d cpus" % (len(manifest["tasks"]), workers, cpt))
    lock = threading.Lock()
    queue = list(range(element_count(manifest)))
    failures = []

    def worker():
        while True:
            with lock:
                if not queue:
                    return
                e = queue.pop(0)
            for t in element_tasks(manifest, e):
                rec = run_task(manifest, t, slurm={"job_id": "local", "array_job_id": "local",
                                                   "array_task_id": str(e)})
                if rec["status"] != "ok":
                    with lock:
                        failures.append(t["id"])

    threads = [threading.Thread(target=worker) for _ in range(workers)]
    for th in threads:
        th.start()
    for th in threads:
        th.join()
    path, summ = collect(outdir)
    print("collected %d/%d ok -> %s" % (summ["ok"], summ["total"], path))
    return 0 if not failures and not summ["missing"] else 1


def cmd_collect(args):
    path, s = collect(os.path.abspath(args.outdir))
    print("%d tasks: %d ok, %d failed, %d missing -> %s" % (
        s["total"], s["ok"], len(s["failed"]), len(s["missing"]), path))
    if s["failed"]:
        print("failed task ids : %s" % ",".join(map(str, s["failed"])))
    if s["missing"]:
        print("missing task ids: %s" % ",".join(map(str, s["missing"])))
    if s["failed"] or s["missing"]:
        print("resubmit only the unfinished array elements with: submit SPEC --only-failed")
        return 1
    return 0


def cmd_status(args):
    outdir = os.path.abspath(args.outdir)
    manifest = load_manifest(outdir)
    s = summarize(manifest, read_records(outdir))
    print("%d tasks: %d ok, %d failed, %d not finished" % (
        s["total"], s["ok"], len(s["failed"]), len(s["missing"])))
    return 0


def main(argv=None):
    ap = argparse.ArgumentParser(prog="objeck-slurm", description=__doc__.split("\n\n")[0])
    sub = ap.add_subparsers(dest="cmd")
    sub.required = True

    def common(p):
        p.add_argument("spec")
        p.add_argument("--outdir")

    p = sub.add_parser("plan", help="write manifest + job.sbatch (or print them with --dry-run)")
    common(p)
    p.add_argument("--dry-run", action="store_true")
    p.set_defaults(fn=cmd_plan)

    p = sub.add_parser("submit", help="plan, then sbatch")
    common(p)
    p.add_argument("--dry-run", action="store_true")
    p.add_argument("--sbatch", default="sbatch", help="sbatch command (a shim in tests)")
    p.add_argument("--only-failed", action="store_true",
                   help="resubmit only the array elements without an ok result")
    p.set_defaults(fn=cmd_submit)

    p = sub.add_parser("local", help="run the whole sweep here with a process pool")
    common(p)
    p.add_argument("--workers", type=int)
    p.set_defaults(fn=cmd_local)

    p = sub.add_parser("run-task", help="run one array element (what job.sbatch calls)")
    p.add_argument("--manifest", required=True)
    p.add_argument("--index", type=int)
    p.set_defaults(fn=cmd_run_task)

    p = sub.add_parser("collect", help="merge results/*.json into results.csv")
    p.add_argument("outdir")
    p.set_defaults(fn=cmd_collect)

    p = sub.add_parser("status", help="count ok / failed / unfinished tasks")
    p.add_argument("outdir")
    p.set_defaults(fn=cmd_status)

    args = ap.parse_args(argv)
    try:
        return args.fn(args)
    except SweepError as e:
        print("objeck-slurm: %s" % e, file=sys.stderr)
        return 2


if __name__ == "__main__":
    sys.exit(main())
