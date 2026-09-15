#!/usr/bin/env python3
"""Differential regression: one test, several compiler/VM configurations, one answer.

The regression runners compile every test at -opt s3 and run it once, with the
JIT in its default mode. A miscompile that only the optimizer makes, or a JIT
bug that only a compiled method shows, passes there as long as the test's own
assertions do not happen to cover it -- and most tests assert only a few of the
values they print. This runner compiles each test twice and runs it under
several configurations, then requires every run to print the same stdout and
agree on success:

    s0/off      -opt s0, --jit=off      the reference: no optimizer, no JIT
    s3/off      -opt s3, --jit=off      the optimizer alone
    s3/default  -opt s3, no flag        what users run
    s3/jit1     -opt s3, --jit=1        every method compiled
    s0/jit1     -opt s0, --jit=1        with --rotate only

The oracle is stdout (exact bytes) plus zero/non-zero exit status. Not the exact
exit code: an escaped VM error is -1 from Execute but 1 from the JIT bridge, and
both are correct. stderr is saved but never compared, so a test that prints
timings or other run-dependent numbers should print them to stderr
(`"..."->ErrorLine()`), keeping stdout for what must not change.

Libraries are whatever the deploy tree holds (s3; lang.obl s2). The s0 compile
covers the test's own code only.

Usage:
    python3 run_differential.py <bin_dir> [-j N] [--rotate] [tests...]

<bin_dir> holds obc and obr (a deploy tree's bin); its ../lib is OBJECK_LIB_PATH.
Tests are names or .obs paths; none means every programs/regression/*.obs.
Exit 0 when nothing diverged or failed, 1 otherwise, 2 on a usage error.

Per-test markers, each a whole line at column 0 (like the JIT opt-out marker),
checked by tools/cicd/check_diff_markers.py:

    # DIFF_CONFIGS: s3             compare only these configurations: an -opt
                                   level (all its configs) or a config name,
                                   separated by commas or spaces
    # DIFF_REQUIRES_JIT            skip the --jit=off configurations
    # NONDETERMINISTIC_OUTPUT      compare exit status only, never stdout
    # DIFF_SERIAL                  never run beside another test

The first three reduce coverage, so the lint requires a "# reason: ..." line
right after each, and caps NONDETERMINISTIC_OUTPUT at 5% of the suite.

Also honored, with the regression runners' meaning:
    # EXPECT_COMPILE_ERROR         skipped: nothing to run
    # EXPECT_RUNTIME_ERROR         every configuration must exit non-zero
    # EXTRA_LIBS: a,b              appended to -lib cipher,collect,xml,json
    the JIT opt-out marker         only the --jit=off configurations are run

Serial tests: a test runs after the parallel pool, alone, when it carries
DIFF_SERIAL or its source matches SERIAL_PATTERNS below -- sockets (fixed
ports, loopback servers) and threads (their tests assert timing bounds that
fail under a loaded machine rather than a wrong VM). Serial tests are not
skipped or relaxed; they only never share the machine with another test.
"""
import argparse
import json
import os
import re
import subprocess
import sys
import threading
import time
from concurrent.futures import ThreadPoolExecutor

HERE = os.path.dirname(os.path.abspath(__file__))

BASE_LIBS = "cipher,collect,xml,json"

# (name, -opt level, obr flags). The first entry is the reference.
CONFIGS = [
    ("s0/off", "s0", ["--jit=off"]),
    ("s3/off", "s3", ["--jit=off"]),
    ("s3/default", "s3", []),
    ("s3/jit1", "s3", ["--jit=1"]),
]
ROTATE_CONFIGS = [
    ("s0/jit1", "s0", ["--jit=1"]),
]
ALL_CONFIG_NAMES = [c[0] for c in CONFIGS + ROTATE_CONFIGS]
OPT_LEVELS = sorted(set(c[1] for c in CONFIGS + ROTATE_CONFIGS))

SERIAL_PATTERNS = [
    re.compile(r"\bSystem\.IO\.Net\b"),
    re.compile(r"\bWeb\.HTTP\b"),
    re.compile(r"\b(TCP|UDP|DTLS)\w*Socket"),
    re.compile(r"\bSystem\.Concurrency\b"),
    re.compile(r"\bfrom\s+(System\.Concurrency\.)?Thread\b"),
]

# Environment variables that would change what a configuration means.
SCRUB_ENV = ("OBJECK_JIT_DISABLE", "OBJECK_JIT_THRESHOLD", "OBJECK_VM_ARGS")

print_lock = threading.Lock()


def say(line):
    with print_lock:
        print(line, flush=True)


def source_lines(text):
    if text.startswith("﻿"):
        text = text[1:]
    return [l.rstrip("\r") for l in text.split("\n")]


def parse_markers(text):
    """Read the directives a test carries. Every marker is line-anchored."""
    lines = source_lines(text)
    m = {
        "compile_error": any(l.startswith("# EXPECT_COMPILE_ERROR") for l in lines),
        "runtime_error": any(l.startswith("# EXPECT_RUNTIME_ERROR") for l in lines),
        "jit_disable": "# JIT_DISABLE" in lines,
        "requires_jit": "# DIFF_REQUIRES_JIT" in lines,
        "nondeterministic": "# NONDETERMINISTIC_OUTPUT" in lines,
        "serial_marker": "# DIFF_SERIAL" in lines,
        "diff_configs": None,
        "extra_libs": [],
        "errors": [],
    }
    for l in lines:
        if l.startswith("# EXTRA_LIBS:"):
            extra = l[len("# EXTRA_LIBS:"):].strip().split()
            if extra:
                m["extra_libs"].append(extra[0])
        elif l.startswith("# DIFF_CONFIGS:"):
            tokens = [t for t in re.split(r"[,\s]+", l[len("# DIFF_CONFIGS:"):]) if t]
            bad = [t for t in tokens if t not in ALL_CONFIG_NAMES and t not in OPT_LEVELS]
            if bad or not tokens:
                m["errors"].append("bad DIFF_CONFIGS value(s): %s" % (", ".join(bad) or "(empty)"))
            m["diff_configs"] = (m["diff_configs"] or []) + tokens
    m["serial"] = m["serial_marker"] or any(p.search(text) for p in SERIAL_PATTERNS)
    return m


def select_configs(markers, rotate):
    listed = markers["diff_configs"]
    pool = CONFIGS + ROTATE_CONFIGS if (rotate or listed) else CONFIGS
    chosen = []
    for name, opt, flags in pool:
        if listed is not None and name not in listed and opt not in listed:
            continue
        if listed is None and not rotate and (name, opt, flags) in ROTATE_CONFIGS:
            continue
        uses_jit = "--jit=off" not in flags
        if markers["requires_jit"] and not uses_jit:
            continue
        if markers["jit_disable"] and uses_jit:
            continue
        chosen.append((name, opt, flags))
    return chosen


def find_tool(bin_dir, base):
    # .cmd/plain names let a wrapper script stand in for the real binary
    for cand in (base + ".exe", base, base + ".cmd", base + ".bat"):
        path = os.path.join(bin_dir, cand)
        if os.path.isfile(path):
            return path
    return None


def make_env(bin_dir, lib_dir):
    env = dict(os.environ)
    for k in SCRUB_ENV:
        env.pop(k, None)
    native = os.path.join(lib_dir, "native")
    if os.path.isdir(lib_dir):
        env["OBJECK_LIB_PATH"] = lib_dir
    prefix = [bin_dir] + ([native] if os.path.isdir(native) else [])
    env["PATH"] = os.pathsep.join(prefix + [env.get("PATH", "")])
    if os.name != "nt" and os.path.isdir(native):
        for k in ("LD_LIBRARY_PATH", "DYLD_LIBRARY_PATH"):
            env[k] = native + (os.pathsep + env[k] if env.get(k) else "")
    return env


def run_proc(cmd, cwd, env, timeout):
    start = time.perf_counter()
    try:
        p = subprocess.run(cmd, cwd=cwd, env=env, stdout=subprocess.PIPE,
                           stderr=subprocess.PIPE, timeout=timeout)
        return p.returncode, p.stdout, p.stderr, time.perf_counter() - start
    except subprocess.TimeoutExpired as x:
        return None, x.stdout or b"", x.stderr or b"", time.perf_counter() - start


def first_difference(a, b):
    la, lb = a.split(b"\n"), b.split(b"\n")
    for i in range(max(len(la), len(lb))):
        x = la[i] if i < len(la) else b"<end of output>"
        y = lb[i] if i < len(lb) else b"<end of output>"
        if x != y:
            return i + 1, x.rstrip(b"\r")[:160], y.rstrip(b"\r")[:160]
    return None


class Runner:
    def __init__(self, args):
        self.args = args
        self.bin_dir = os.path.abspath(args.bin_dir)
        self.lib_dir = os.path.abspath(args.lib_dir or os.path.join(self.bin_dir, "..", "lib"))
        self.obc = find_tool(self.bin_dir, "obc")
        self.obr = find_tool(self.bin_dir, "obr")
        self.reg_dir = os.path.abspath(args.regression_dir)
        self.work = os.path.abspath(args.work)
        self.env = make_env(self.bin_dir, self.lib_dir)

    def test_one(self, name):
        src = os.path.join(self.reg_dir, name + ".obs")
        rec = {"test": name, "status": "ok", "details": [], "configs": {}, "self_skip": False}
        with open(src, encoding="utf-8", errors="replace") as fh:
            markers = parse_markers(fh.read())
        rec["serial"] = markers["serial"]
        if markers["compile_error"]:
            rec["status"] = "skip"
            rec["details"].append("compile-error test")
            return rec
        if markers["errors"]:
            rec["status"] = "fail"
            rec["details"] += markers["errors"]
            return rec
        configs = select_configs(markers, self.args.rotate)
        if not configs:
            rec["status"] = "fail"
            rec["details"].append("markers leave no configuration to run")
            return rec

        wd = os.path.join(self.work, name)
        os.makedirs(wd, exist_ok=True)
        libs = ",".join([BASE_LIBS] + markers["extra_libs"])
        obes = {}
        for opt in sorted(set(c[1] for c in configs)):
            obe = os.path.join(wd, "%s_%s.obe" % (name, opt))
            if os.path.exists(obe):
                os.remove(obe)
            cmd = [self.obc, "-src", src, "-lib", libs, "-opt", opt, "-dest", obe]
            rc, out, err, _ = run_proc(cmd, self.bin_dir, self.env, self.args.timeout)
            with open(os.path.join(wd, "compile_%s.log" % opt), "wb") as fh:
                fh.write(out + err)
            if rc is None:
                rec["details"].append("compile -opt %s timed out" % opt)
            elif rc != 0:
                tail = (out + err).decode("utf-8", "replace").strip().splitlines()[:3]
                rec["details"].append("compile -opt %s exited %d: %s" % (opt, rc, " | ".join(tail)))
            elif not os.path.isfile(obe):
                rec["details"].append("compile -opt %s exited 0 but wrote no .obe" % opt)
            else:
                obes[opt] = obe
        if rec["details"]:
            rec["status"] = "fail"
            return rec

        env = self.env
        if markers["jit_disable"]:
            env = dict(env, OBJECK_JIT_DISABLE="1")
        results = []
        for cname, opt, flags in configs:
            rc, out, err, dt = run_proc([self.obr] + flags + [obes[opt]], self.reg_dir, env,
                                        self.args.timeout)
            tag = cname.replace("/", "_")
            with open(os.path.join(wd, tag + ".out"), "wb") as fh:
                fh.write(out)
            with open(os.path.join(wd, tag + ".err"), "wb") as fh:
                fh.write(err)
            rec["configs"][cname] = {"rc": rc, "secs": round(dt, 3), "stdout_bytes": len(out)}
            results.append((cname, rc, out))

        timeouts = [c for c, rc, _ in results if rc is None]
        if timeouts:
            rec["status"] = "fail"
            rec["details"].append("timed out after %ds: %s" % (self.args.timeout, ", ".join(timeouts)))
            return rec

        ref_name, ref_rc, ref_out = results[0]
        rec["self_skip"] = any(l.startswith(b"SKIP:") for l in ref_out.split(b"\n"))
        for cname, rc, out in results[1:]:
            if (rc != 0) != (ref_rc != 0):
                rec["status"] = "diff"
                rec["details"].append("exit status: %s=%d, %s=%d" % (ref_name, ref_rc, cname, rc))
            if not markers["nondeterministic"] and out != ref_out:
                rec["status"] = "diff"
                line, x, y = first_difference(ref_out, out)
                rec["details"].append("stdout: %s vs %s differ at line %d: %r vs %r"
                                      % (ref_name, cname, line, x, y))
        if rec["status"] == "ok":
            want_nonzero = markers["runtime_error"]
            if (ref_rc != 0) != want_nonzero:
                rec["status"] = "fail"
                rec["details"].append("every configuration exited %d; expected %s"
                                      % (ref_rc, "non-zero" if want_nonzero else "0"))
        return rec

    def report(self, rec):
        label = {"ok": "ok  ", "diff": "DIFF", "fail": "FAIL", "skip": "skip"}[rec["status"]]
        line = "%s %s" % (label, rec["test"])
        if rec["status"] == "ok":
            line += "  [%s]" % ", ".join(rec["configs"])
        for d in rec["details"]:
            if rec["status"] != "skip":
                line += "\n       " + d
        say(line)

    def guarded(self, name):
        try:
            rec = self.test_one(name)
        except Exception as e:  # a runner bug must not look like a pass
            rec = {"test": name, "status": "fail", "details": ["runner error: %r" % e],
                   "configs": {}, "serial": False, "self_skip": False}
        self.report(rec)
        return rec


def list_tests(reg_dir, names):
    if not names:
        return sorted(f[:-4] for f in os.listdir(reg_dir) if f.endswith(".obs"))
    out = []
    for n in names:
        base = os.path.basename(n)
        out.append(base[:-4] if base.endswith(".obs") else base)
    return out


def main(argv=None):
    ap = argparse.ArgumentParser(description=__doc__.split("\n")[0])
    ap.add_argument("bin_dir", help="directory holding obc and obr")
    ap.add_argument("tests", nargs="*", help="test names or .obs paths (default: all)")
    ap.add_argument("-j", "--jobs", type=int, default=max(1, min(4, (os.cpu_count() or 2) // 4)),
                    help="parallel tests (serial tests always run alone)")
    ap.add_argument("--rotate", action="store_true", help="add the s0/jit1 configuration")
    ap.add_argument("--timeout", type=int, default=180, help="seconds per compile or run")
    ap.add_argument("--lib-dir", help="OBJECK_LIB_PATH (default: <bin_dir>/../lib)")
    ap.add_argument("--regression-dir", default=HERE, help="where the .obs files are")
    ap.add_argument("--work", default=os.path.join(HERE, "results", "differential"),
                    help="where .obe files and per-config output go")
    ap.add_argument("--json", help="write per-test results here")
    args = ap.parse_args(argv)

    runner = Runner(args)
    if not runner.obc or not runner.obr:
        sys.stderr.write("obc/obr not found in %s\n" % runner.bin_dir)
        return 2
    names = list_tests(runner.reg_dir, args.tests)
    missing = [n for n in names if not os.path.isfile(os.path.join(runner.reg_dir, n + ".obs"))]
    if missing:
        sys.stderr.write("no such test: %s\n" % ", ".join(missing))
        return 2
    os.makedirs(runner.work, exist_ok=True)

    serial, parallel = [], []
    for n in names:
        with open(os.path.join(runner.reg_dir, n + ".obs"), encoding="utf-8", errors="replace") as fh:
            (serial if parse_markers(fh.read())["serial"] else parallel).append(n)

    configs = [c[0] for c in CONFIGS + (ROTATE_CONFIGS if args.rotate else [])]
    say("Differential regression: %d test(s), configs %s, -j %d (%d serial)"
        % (len(names), ", ".join(configs), args.jobs, len(serial)))
    start = time.perf_counter()
    with ThreadPoolExecutor(max(1, args.jobs)) as ex:
        results = list(ex.map(runner.guarded, parallel))
    results += [runner.guarded(n) for n in serial]
    wall = time.perf_counter() - start

    count = lambda s: sum(1 for r in results if r["status"] == s)
    ran = len(results) - count("skip")
    self_skips = sorted(r["test"] for r in results if r.get("self_skip"))
    say("")
    say("Results: %d test(s), %d compared, %d skipped (compile-error), %d divergent, %d failed; "
        "wall %.1fs" % (len(results), ran, count("skip"), count("diff"), count("fail"), wall))
    if self_skips:
        say("  reported SKIP (compared, but their checks did not run): %s" % ", ".join(self_skips))
    bad = sorted((r for r in results if r["status"] in ("diff", "fail")), key=lambda r: r["test"])
    for r in bad:
        say("  %s %s: %s" % (r["status"].upper(), r["test"], "; ".join(r["details"])))
    if args.json:
        with open(args.json, "w") as fh:
            json.dump({"wall": wall, "configs": configs, "results": results}, fh, indent=1)
    return 1 if bad else 0


if __name__ == "__main__":
    sys.exit(main())
