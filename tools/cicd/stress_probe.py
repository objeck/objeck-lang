#!/usr/bin/env python3
"""Stress probe: loop GC/JIT regression fixtures against a prebuilt deploy tree.

Used by .github/workflows/stress-probe.yml on every platform (stdlib only).
Each fixture is compiled ONCE with the tree's obc, then run many times in each
mode. A run fails when obr exits non-zero or exceeds the per-run timeout. The
driver counts failures per fixture and mode, saves the stdout/stderr of the
first few failures of each, writes results.json and a markdown summary, and
exits 1 on any failure (compile errors and a missing VM knob included).

    python tools/cicd/stress_probe.py run --bin core/release/deploy-x64/bin \\
        --out stress-probe-results [--runs 5] [--jobs 2] [--budget 5100] \\
        [--summary "$GITHUB_STEP_SUMMARY"]

With zero failures in n runs, the rule of three gives 3/n as an approximate
95% upper confidence bound on the per-run failure probability: 200 clean runs
say the failure rate is below 1.5%, not that it is zero.
"""

import argparse
import concurrent.futures
import json
import os
import re
import subprocess
import sys
import threading
import time

BASE_LIBS = "cipher,collect,xml,json"
OPT = "s3"
MAX_CAPTURED = 3
OUTPUT_LIMIT = 64 * 1024

# Modes: (obr flags, environment). Every knob below is cleared from the
# inherited environment first, so "default" really is the default.
KNOBS = ("OBJECK_JIT_THRESHOLD", "OBJECK_JIT_DISABLE", "OBJECK_NURSERY",
         "OBJECK_GC_VERIFY", "OBJECK_GC_VERIFY_INJECT", "OBJECK_VM_ARGS")
MODES = {
    "default": ([], {}),
    "jit1": (["--jit=1"], {"OBJECK_JIT_THRESHOLD": "1"}),
    "nursery256k": ([], {"OBJECK_NURSERY": "256k"}),
    "verify256k": ([], {"OBJECK_GC_VERIFY": "1", "OBJECK_NURSERY": "256k"}),
}
MODE_ORDER = ["default", "jit1", "nursery256k", "verify256k"]

# obr must contain these strings for the mode to mean anything: an obr that
# does not read the variable would run the fixture unstressed and report green.
MODE_REQUIRES = {
    "nursery256k": ["OBJECK_NURSERY"],
    "verify256k": ["OBJECK_NURSERY", "OBJECK_GC_VERIFY"],
}

# Runs per fixture per mode. Seconds per run measured on the integration-5 x64
# tree (Ryzen 7950X3D, Windows, --jobs 1), default / jit1 / nursery256k /
# verify256k:
#
#   core_thread_gc_stress           7.9   7.9   82    103
#   obj_size_layout                 0.54  0.53  14.3  21.0
#   gc_minor_closure_capture        0.36  0.37  2.9   4.6
#   minor_gc_stress                 0.43  0.42  1.75  2.2
#   closure_capture_old_holder_g12  0.35  0.36  1.5   1.7
#   gc_zero_field_nursery_end       0.25  0.22  0.02  0.02  (fills scale with
#   gc_closure_capture_nursery_end  0.22  0.20  0.02  0.02   the nursery size)
#   closure_nested_capture          0.06  0.07  0.21  0.26
#   jit_gc_stress                   0.05  0.05  0.10  0.19
#   jit_closure_gc_fixup            0.02  0.02  0.02  0.05
#
# 200 runs of every cell would be ~33,000 s on that machine, so the slow cells
# are cut to ~3,600 local seconds in total, sized to fit the workflow's
# --budget 4800 at --jobs 3 on a hosted runner. The cuts, and the rule-of-three
# bound each leaves when clean: core_thread_gc_stress 30 (10%) in default and
# jit1, 3 (100%, a smoke check only) under the small nursery and the verifier;
# obj_size_layout 15 (20%) / 10 (30%); gc_minor_closure_capture 60 (5%) / 40
# (7.5%); minor_gc_stress 120 (2.5%) / 80 (3.75%); closure_capture_old_holder_g12
# 100 (3%) / 80 (3.75%). Everything else runs 200 (1.5%). Raise a count with
# --fixture-runs or the workflow's runs input for a targeted probe.
DEFAULT_RUNS = 200
RUN_COUNTS = {
    "core_thread_gc_stress": {"default": 30, "jit1": 30, "nursery256k": 3, "verify256k": 3},
    "minor_gc_stress": {"nursery256k": 120, "verify256k": 80},
    "jit_gc_stress": 200,
    "gc_minor_closure_capture": {"nursery256k": 60, "verify256k": 40},
    "closure_capture_old_holder_g12": {"nursery256k": 100, "verify256k": 80},
    "gc_zero_field_nursery_end": 200,
    "gc_closure_capture_nursery_end": 200,
    "obj_size_layout": {"nursery256k": 15, "verify256k": 10},
    "closure_nested_capture": 200,
    "jit_closure_gc_fixup": 200,
}
FIXTURES = list(RUN_COUNTS)


def planned_runs(fixture, mode, runs=0, overrides=None):
    """Runs for one cell: --runs, else --fixture-runs, else RUN_COUNTS.

    A RUN_COUNTS entry is an int (every mode) or a {mode: n} dict whose
    missing modes default to DEFAULT_RUNS.
    """
    if runs:
        return runs
    if overrides and fixture in overrides:
        return overrides[fixture]
    entry = RUN_COUNTS.get(fixture, DEFAULT_RUNS)
    if isinstance(entry, dict):
        return entry.get(mode, DEFAULT_RUNS)
    return entry


def rule_of_three(runs):
    """Approximate 95% upper bound on the failure rate after runs clean runs."""
    if runs <= 0:
        return None
    return min(1.0, 3.0 / runs)


def format_rate(rate):
    if rate is None:
        return "n/a"
    pct = rate * 100.0
    if pct >= 10:
        return "%.0f%%" % pct
    if pct >= 1:
        return "%.1f%%" % pct
    return "%.2f%%" % pct


def extra_libs(src_path):
    """The '# EXTRA_LIBS: a,b' header run_regression.* honours."""
    try:
        with open(src_path, "r", encoding="utf-8-sig", errors="replace") as f:
            for line in f:
                m = re.search(r"# EXTRA_LIBS:\s*(\S+)", line)
                if m:
                    return m.group(1)
    except OSError:
        pass
    return ""


def tool_path(bin_dir, name):
    exe = os.path.join(bin_dir, name + ".exe")
    return exe if os.name == "nt" or os.path.exists(exe) else os.path.join(bin_dir, name)


def missing_knobs(obr_path, modes):
    """Knob names a mode needs that the obr binary does not contain."""
    wanted = []
    for mode in modes:
        for knob in MODE_REQUIRES.get(mode, []):
            if knob not in wanted:
                wanted.append(knob)
    if not wanted:
        return []
    try:
        with open(obr_path, "rb") as f:
            data = f.read()
    except OSError:
        return wanted
    return [k for k in wanted if k.encode("ascii") not in data]


def mode_env(base_env, mode):
    env = {k: v for k, v in base_env.items() if k not in KNOBS}
    env.update(MODES[mode][1])
    return env


def clip(data):
    text = data.decode("utf-8", "replace") if isinstance(data, bytes) else (data or "")
    if len(text) > OUTPUT_LIMIT:
        text = "[... %d chars clipped ...]\n" % (len(text) - OUTPUT_LIMIT) + text[-OUTPUT_LIMIT:]
    return text


class Cell:
    """Counts for one fixture x mode."""

    def __init__(self, fixture, mode, planned):
        self.fixture = fixture
        self.mode = mode
        self.planned = planned
        self.runs = 0
        self.failures = 0
        self.timeouts = 0
        self.seconds = 0.0
        self.max_seconds = 0.0
        self.captured = []   # [{run, code, stderr_file, stdout_file, message}]
        self.exit_codes = {}
        self.lock = threading.Lock()

    def record(self, run, code, seconds, stdout, stderr, save_fn):
        """Record one run. code None = timed out. Returns True on failure."""
        with self.lock:
            self.runs += 1
            self.seconds += seconds
            self.max_seconds = max(self.max_seconds, seconds)
            if code == 0:
                return False
            self.failures += 1
            if code is None:
                self.timeouts += 1
                message = "timed out"
            else:
                message = "exit code %d" % code
            self.exit_codes[message] = self.exit_codes.get(message, 0) + 1
            if len(self.captured) < MAX_CAPTURED:
                entry = {"run": run, "code": code, "message": message}
                if save_fn:
                    entry.update(save_fn(self, run, message, stdout, stderr))
                self.captured.append(entry)
            return True

    def as_dict(self):
        mean = self.seconds / self.runs if self.runs else 0.0
        return {
            "fixture": self.fixture, "mode": self.mode, "planned": self.planned,
            "runs": self.runs, "failures": self.failures, "timeouts": self.timeouts,
            "mean_seconds": round(mean, 4), "max_seconds": round(self.max_seconds, 4),
            "upper_bound": rule_of_three(self.runs) if self.failures == 0 else None,
            "exit_codes": self.exit_codes, "captured": self.captured,
        }


def run_cell(cell, run_fn, jobs, remaining=None):
    """Runs cell.planned times through run_fn(run_index) -> (code, secs, out, err, save).

    Stops early (leaving runs < planned) when remaining() drops to zero.
    """
    counter = [0]
    counter_lock = threading.Lock()

    def next_index():
        with counter_lock:
            if counter[0] >= cell.planned:
                return None
            if remaining is not None and remaining() <= 0:
                return None
            counter[0] += 1
            return counter[0]

    def worker():
        while True:
            i = next_index()
            if i is None:
                return
            code, secs, out, err, save = run_fn(i)
            cell.record(i, code, secs, out, err, save)

    if jobs <= 1:
        worker()
        return cell
    with concurrent.futures.ThreadPoolExecutor(max_workers=jobs) as pool:
        futures = [pool.submit(worker) for _ in range(jobs)]
        for f in futures:
            f.result()
    return cell


def build_summary(meta, compile_results, cells, problems):
    """Markdown summary: problems, per fixture x mode table, per mode totals."""
    lines = []
    title = "### Stress probe: %s" % meta.get("leg", "local")
    lines.append(title)
    lines.append("")
    for key in ("ci_run", "commit", "branch", "elapsed"):
        if meta.get(key):
            lines.append("- %s: %s" % (key.replace("_", " "), meta[key]))
    total_runs = sum(c.runs for c in cells)
    total_fail = sum(c.failures for c in cells)
    status = "FAIL" if (total_fail or problems) else "PASS"
    lines.append("- result: **%s** (%d failure(s) in %d runs)" % (status, total_fail, total_runs))
    lines.append("")
    if problems:
        lines.append("**Problems**")
        lines.append("")
        for p in problems:
            lines.append("- " + p)
        lines.append("")
    bad_compiles = [r for r in compile_results if not r["ok"]]
    if bad_compiles:
        lines.append("**Compile failures**")
        lines.append("")
        for r in bad_compiles:
            lines.append("- `%s`" % r["fixture"])
        lines.append("")
    lines.append("Upper bound: rule of three, 3/n, an approximate 95% bound on the "
                 "per-run failure rate when no run failed.")
    lines.append("")
    lines.append("| Fixture | Mode | Runs | Failures | Upper bound | Mean s | Max s | Notes |")
    lines.append("|---|---|---:|---:|---:|---:|---:|---|")
    for c in cells:
        d = c.as_dict()
        if c.failures:
            bound = "observed %s" % format_rate(c.failures / c.runs)
        else:
            bound = format_rate(rule_of_three(c.runs))
        notes = []
        if c.runs < c.planned:
            notes.append("stopped at %d of %d" % (c.runs, c.planned))
        if c.exit_codes:
            notes.append(", ".join("%s x%d" % kv for kv in sorted(c.exit_codes.items())))
        lines.append("| %s | %s | %d | %s | %s | %.2f | %.2f | %s |" % (
            c.fixture, c.mode, c.runs,
            ("**%d**" % c.failures) if c.failures else "0",
            bound, d["mean_seconds"], d["max_seconds"], "; ".join(notes)))
    lines.append("")
    lines.append("| Mode | Runs | Failures | Upper bound |")
    lines.append("|---|---:|---:|---:|")
    modes = []
    for c in cells:
        if c.mode not in modes:
            modes.append(c.mode)
    for m in modes:
        runs = sum(c.runs for c in cells if c.mode == m)
        fails = sum(c.failures for c in cells if c.mode == m)
        bound = ("observed %s" % format_rate(fails / runs)) if fails else format_rate(rule_of_three(runs))
        lines.append("| %s | %d | %s | %s |" % (m, runs, ("**%d**" % fails) if fails else "0", bound))
    lines.append("")
    return "\n".join(lines)


def parse_fixture_runs(items):
    out = {}
    for item in items or []:
        name, _, n = item.partition("=")
        if not name or not n.isdigit():
            raise ValueError("--fixture-runs expects name=N, got '%s'" % item)
        out[name] = int(n)
    return out


def cmd_run(args):
    start = time.time()
    bin_dir = os.path.abspath(args.bin)
    out_dir = os.path.abspath(args.out)
    work = os.path.join(out_dir, "work")
    fail_dir = os.path.join(out_dir, "failures")
    os.makedirs(work, exist_ok=True)
    obc = tool_path(bin_dir, "obc")
    obr = tool_path(bin_dir, "obr")
    fixtures = [f for f in args.fixtures.split(",") if f] if args.fixtures else FIXTURES
    modes = [m for m in args.modes.split(",") if m]
    overrides = parse_fixture_runs(args.fixture_runs)
    problems = []
    for m in modes:
        if m not in MODES:
            problems.append("unknown mode '%s' (known: %s)" % (m, ", ".join(MODE_ORDER)))
    modes = [m for m in modes if m in MODES]
    for tool in (obc, obr):
        if not os.path.exists(tool):
            problems.append("missing tool %s" % tool)
    if os.path.exists(obr):
        for knob in missing_knobs(obr, modes):
            problems.append("obr does not contain %s; its mode would run unstressed" % knob)
            modes = [m for m in modes if knob not in MODE_REQUIRES.get(m, [])]

    base_env = dict(os.environ)
    lib_dir = os.path.join(os.path.dirname(bin_dir), "lib")
    base_env["OBJECK_LIB_PATH"] = base_env.get("OBJECK_LIB_PATH") or lib_dir
    native = os.path.join(lib_dir, "native")
    for var in ("PATH", "LD_LIBRARY_PATH", "DYLD_LIBRARY_PATH"):
        base_env[var] = bin_dir + os.pathsep + native + os.pathsep + base_env.get(var, "")

    def remaining():
        if not args.budget or args.budget <= 0:
            return 1
        return args.budget - (time.time() - start)

    compile_results = []
    cells = []
    progress = sys.stdout

    def save_failure(cell, run, message, stdout, stderr):
        d = os.path.join(fail_dir, cell.fixture, cell.mode)
        os.makedirs(d, exist_ok=True)
        err_file = os.path.join(d, "run%03d.stderr.txt" % run)
        out_file = os.path.join(d, "run%03d.stdout.txt" % run)
        with open(err_file, "w", encoding="utf-8") as f:
            f.write("# %s %s run %d: %s\n" % (cell.fixture, cell.mode, run, message))
            f.write(clip(stderr))
        with open(out_file, "w", encoding="utf-8") as f:
            f.write(clip(stdout))
        return {"stderr_file": os.path.relpath(err_file, out_dir).replace("\\", "/"),
                "stdout_file": os.path.relpath(out_file, out_dir).replace("\\", "/")}

    if os.path.exists(obc) and os.path.exists(obr):
        for fixture in fixtures:
            src = os.path.abspath(os.path.join(args.src_dir, fixture + ".obs"))
            program = os.path.join(work, fixture + ".obe")
            libs = BASE_LIBS
            extra = extra_libs(src)
            if extra:
                libs += "," + extra
            cmd = [obc, "-src", src, "-lib", libs, "-opt", OPT, "-dest", program]
            ok = False
            output = ""
            if not os.path.exists(src):
                output = "missing source %s" % src
            else:
                try:
                    p = subprocess.run(cmd, cwd=bin_dir, env=mode_env(base_env, "default"),
                                       stdout=subprocess.PIPE, stderr=subprocess.STDOUT,
                                       timeout=600)
                    output = clip(p.stdout)
                    ok = p.returncode == 0 and os.path.exists(program)
                except (OSError, subprocess.TimeoutExpired) as e:
                    output = str(e)
            compile_results.append({"fixture": fixture, "ok": ok, "command": cmd,
                                    "output": output})
            print("compile %s: %s" % (fixture, "ok" if ok else "FAILED"), file=progress)
            if not ok:
                print(output, file=progress)
                continue
            for mode in modes:
                planned = planned_runs(fixture, mode, args.runs, overrides)
                flags = MODES[mode][0]
                env = mode_env(base_env, mode)
                cell = Cell(fixture, mode, planned)

                def run_fn(i, flags=flags, env=env, program=program):
                    t0 = time.time()
                    try:
                        p = subprocess.run([obr] + flags + [program], cwd=work, env=env,
                                           stdout=subprocess.PIPE, stderr=subprocess.PIPE,
                                           timeout=args.timeout)
                        return p.returncode, time.time() - t0, p.stdout, p.stderr, save_failure
                    except subprocess.TimeoutExpired as e:
                        return None, time.time() - t0, e.stdout or b"", e.stderr or b"", save_failure
                    except OSError as e:
                        return -1, time.time() - t0, b"", str(e).encode(), save_failure

                run_cell(cell, run_fn, args.jobs, remaining)
                cells.append(cell)
                d = cell.as_dict()
                print("  %-32s %-12s %d/%d failed  mean %.2fs max %.2fs" % (
                    fixture, mode, cell.failures, cell.runs, d["mean_seconds"],
                    d["max_seconds"]), file=progress)
                progress.flush()
                if cell.runs < cell.planned:
                    break
            if remaining() <= 0:
                problems.append("budget of %d s exhausted at %s; later cells did not run"
                                % (args.budget, fixture))
                break

    elapsed = time.time() - start
    meta = {"leg": args.leg, "ci_run": args.ci_run, "commit": args.commit,
            "branch": args.branch, "elapsed": "%.0f s" % elapsed}
    summary = build_summary(meta, compile_results, cells, problems)
    failed = bool(problems) or any(not r["ok"] for r in compile_results) or \
        any(c.failures for c in cells)
    result = {"meta": meta, "status": "fail" if failed else "pass",
              "problems": problems, "compile": compile_results,
              "cells": [c.as_dict() for c in cells], "elapsed_seconds": round(elapsed, 1)}
    with open(os.path.join(out_dir, "results.json"), "w", encoding="utf-8") as f:
        json.dump(result, f, indent=2)
    with open(os.path.join(out_dir, "summary.md"), "w", encoding="utf-8") as f:
        f.write(summary)
    if args.summary:
        with open(args.summary, "a", encoding="utf-8") as f:
            f.write(summary + "\n")
    print(summary)
    return 1 if failed else 0


def main(argv=None):
    ap = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    sub = ap.add_subparsers(dest="cmd", required=True)
    rp = sub.add_parser("run", help="compile the fixtures once and loop them")
    rp.add_argument("--bin", required=True, help="deploy tree bin directory")
    rp.add_argument("--out", required=True, help="results directory")
    rp.add_argument("--src-dir", default="programs/regression")
    rp.add_argument("--fixtures", default="", help="comma list (default: all probe fixtures)")
    rp.add_argument("--modes", default=",".join(MODE_ORDER))
    rp.add_argument("--runs", type=int, default=0,
                    help="runs per fixture per mode for every fixture (default: RUN_COUNTS)")
    rp.add_argument("--fixture-runs", action="append", metavar="NAME=N",
                    help="override one fixture's run count")
    rp.add_argument("--jobs", type=int, default=1, help="parallel runs within a cell")
    rp.add_argument("--timeout", type=int, default=300, help="seconds per run")
    rp.add_argument("--budget", type=int, default=0,
                    help="stop starting runs after this many seconds (0 = none)")
    rp.add_argument("--summary", default="", help="append the markdown summary here")
    rp.add_argument("--leg", default="local")
    rp.add_argument("--ci-run", default="")
    rp.add_argument("--commit", default="")
    rp.add_argument("--branch", default="")
    args = ap.parse_args(argv)
    if args.cmd == "run":
        return cmd_run(args)
    return 2


if __name__ == "__main__":
    sys.exit(main())
