#!/usr/bin/env python3
"""Differential runner for the GC-root coverage programs (docs/GC_ROOTS_DESIGN.md section 9).

    python tools/gc_roots/run_roots.py --bin <deploy>/bin --mode ci               # 30 seeds, a few minutes
    python tools/gc_roots/run_roots.py --bin <deploy>/bin --mode full --count 500 --start 1000 -j 8
    python tools/gc_roots/run_roots.py --bin <deploy>/bin --reduce 1234           # minimize a divergent seed

For each seed `gen_roots.py` writes a program that keeps references live in every
place a JIT frame or an interpreter stack can hold one, and prints a digest of
what it kept. The program is compiled at -opt s0 and s3 and run under:

    ref        s0, --jit=off, the default 128 MB nursery (no collection: the oracle)
    off64      interpreter, 64k nursery (a minor collection every few hundred
               allocations), OBJECK_GC_ROOTS=audit
    def64      default JIT (compiles after 10 calls), 64k nursery
    jit1       --jit=1 (every method compiled), 64k nursery, OBJECK_GC_ROOTS=audit
    jit1v      --jit=1, 64k nursery, --gc-threshold=256k (major collections too),
               OBJECK_GC_VERIFY=checkmark
    jit1big    --jit=1, default nursery (the JIT without collection pressure)
    thr1       OBJECK_JIT_THRESHOLD=1, 128k nursery

and every run must print what `ref` printed. A run also fails when it crashes, times
out, or its OBJECK_GC_ROOTS=audit summary reports a disagreement between the
reference maps and the live stacks (an address-valued Int, which the audit reports
by design, is allowed for programs that use one).

Exit status: 0 when every seed agrees, 1 on any divergence, 2 on a setup problem
(no toolchain, a generated program that does not compile: a generator bug).
"""

import argparse
import concurrent.futures
import json
import os
import re
import shutil
import subprocess
import sys
import tempfile
import time

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
import gen_roots  # noqa: E402

AUDIT = {"OBJECK_GC_ROOTS": "audit"}
VERIFY = {"OBJECK_GC_VERIFY": "checkmark"}

# tag -> (obr flags, extra environment)
RUNS = {
    "off64": (["--jit=off", "--nursery=64k"], AUDIT),
    "def64": (["--nursery=64k"], {}),
    "off64v": (["--jit=off", "--nursery=64k", "--gc-threshold=128k"], VERIFY),
    "jit1": (["--jit=1", "--nursery=64k"], dict(AUDIT, OBJECK_JIT_REPORT="1")),
    "jit1v": (["--jit=1", "--nursery=64k", "--gc-threshold=256k"], VERIFY),
    "jit1big": (["--jit=1"], {}),
    "thr1": (["--nursery=128k"], {"OBJECK_JIT_THRESHOLD": "1"}),
    # a callee below the threshold runs in the interpreter above its compiled caller (the bridge),
    # so these mix interpreted and compiled frames on one stack, and the audit checks that mix
    "mix3": (["--nursery=64k"], dict(AUDIT, OBJECK_JIT_THRESHOLD="3")),
    "mix7": (["--nursery=96k"], dict(AUDIT, OBJECK_JIT_THRESHOLD="7")),
}
REF = ("s0", "ref", ["--jit=off"], {})

MODES = {
    # the CI-sized set: the interpreter, the default JIT and the compile-everything JIT under
    # collection pressure, the verifier once
    "ci": [("s0", "jit1"), ("s3", "off64"), ("s3", "def64"), ("s3", "jit1"), ("s3", "jit1v"), ("s0", "mix3")],
    "full": [(o, t) for o in ("s0", "s3") for t in RUNS],
}

# A callee still below the JIT threshold runs in the interpreter above its compiled caller, and an
# interpreted frame under compiled frames is scanned on a guess (docs/GC_ROOTS_DESIGN.md section 12):
# an address-valued Int pending there is rewritten. Only the configurations that never mix the two
# (everything interpreted, everything compiled) can be held to a program that uses such Ints.
MIXED_TAGS = ("def64", "thr1", "mix3", "mix7")

AUDIT_ZERO = ("failed", "mismatched_threads", "ref_points_at_nothing", "int_points_at_object",
              "jit_value_slot_at_object")
NUMBERS = re.compile(r"(\w+)=(\d+)")
TIMEOUT = 120.0


class Toolchain:
    def __init__(self, bin_dir):
        self.bin = os.path.abspath(bin_dir)
        exe = ".exe" if os.name == "nt" else ""
        self.obc = os.path.join(self.bin, "obc" + exe)
        self.obr = os.path.join(self.bin, "obr" + exe)
        lib = os.path.join(os.path.dirname(self.bin), "lib")
        self.env = dict(os.environ)
        self.env["OBJECK_LIB_PATH"] = lib
        self.env["PATH"] = os.pathsep.join([self.bin, os.path.join(lib, "native"), self.env.get("PATH", "")])
        self.env["OBJECK_GC_TEST_HOOKS"] = "1"   # Runtime->TestAddressOf; inert without it
        for k in list(self.env):
            if k.startswith(("OBJECK_GC_", "OBJECK_JIT_")) and k != "OBJECK_GC_TEST_HOOKS":
                del self.env[k]                    # a leftover knob would change every configuration
        self.timeout = TIMEOUT

    def ok(self):
        return os.path.exists(self.obc) and os.path.exists(self.obr)

    def compile(self, src, opt, dest):
        if os.path.exists(dest):
            os.remove(dest)
        return run([self.obc, "-src", src, "-opt", opt, "-dest", dest], self.env, os.path.dirname(src), self.timeout)

    def execute(self, obe, flags, extra):
        env = dict(self.env)
        env.update(extra)
        return run([self.obr] + flags + [obe], env, os.path.dirname(obe), self.timeout)


class Result:
    def __init__(self, code, out, err, seconds, timed_out=False):
        self.code, self.out, self.err, self.seconds, self.timed_out = code, out, err, seconds, timed_out

    def describe(self):
        if self.timed_out:
            return "timeout after %.0fs" % self.seconds
        if self.code in (None, 0):
            return "exit 0"
        c = self.code & 0xFFFFFFFF if self.code < 0 else self.code
        return "exit %s (0x%X)" % (self.code, c) if c > 255 else "exit %s" % self.code


def run(cmd, env, cwd, timeout):
    t = time.monotonic()
    try:
        p = subprocess.run(cmd, env=env, cwd=cwd, capture_output=True, timeout=timeout)
        return Result(p.returncode, p.stdout.decode("utf-8", "replace"), p.stderr.decode("utf-8", "replace"),
                      time.monotonic() - t)
    except subprocess.TimeoutExpired as e:
        return Result(None, (e.stdout or b"").decode("utf-8", "replace"), (e.stderr or b"").decode("utf-8", "replace"),
                      time.monotonic() - t, True)


def audit_counts(err):
    counts = {}
    for line in err.splitlines():
        if line.startswith("[gc-roots]") and not line.startswith("[gc-roots]   "):
            for k, v in NUMBERS.findall(line):
                counts[k] = int(v)
    return counts


def audit_problems(res, uses_addr):
    c = audit_counts(res.err)
    if not c:
        return ["no audit summary (the VM crashed before exit, or ignored OBJECK_GC_ROOTS=audit)"]
    bad = []
    for k in AUDIT_ZERO:
        if k in ("int_points_at_object", "jit_value_slot_at_object") and uses_addr:
            continue   # an address-valued Int is what those counts report, by design
        if c.get(k, 0):
            bad.append("%s=%d" % (k, c[k]))
    return bad


JIT_LINE = re.compile(r"^\[jit\] (\S+?): (compiled[^\n]*|not compiled -- [^\n]{0,60}|compile failed[^\n]{0,70})", re.M)
GENERATED = ("RcMain:", "Hold", "RcU:P", "RcU:Tr", "RcU:Tiny", "RcU:Rec", "RcWorker:")


def jit_coverage(err):
    """(compiled, not compiled, {reason: count}) over the generated methods in an OBJECK_JIT_REPORT=1 run."""
    done, refused, reasons = set(), set(), {}
    for m in JIT_LINE.finditer(err):
        name = m.group(1)
        if not name.startswith(GENERATED):
            continue
        if m.group(2).startswith("compiled"):
            done.add(name)
        else:
            refused.add(name)
            why = re.sub(r"\d+", "N", re.sub(r"^(not compiled -- |compile failed at instruction \d+ \(opcode \d+\) -- )", "", m.group(2)))
            reasons[why] = reasons.get(why, 0) + 1
    return len(done - refused), len(refused), reasons


def first_diff(a, b):
    la, lb = a.splitlines(), b.splitlines()
    for i in range(max(len(la), len(lb))):
        x = la[i] if i < len(la) else "<end>"
        y = lb[i] if i < len(lb) else "<end>"
        if x != y:
            return "line %d: expected %r, got %r" % (i + 1, x, y)
    return "identical"


class SeedResult:
    def __init__(self, seed):
        self.seed = seed
        self.findings = []     # (config, why)
        self.setup_error = None
        self.features = []
        self.seconds = 0.0
        self.runs = 0
        self.collections = 0
        self.cover = {}


def good_output(res):
    return res.code == 0 and not res.timed_out and "total=" in res.out


def evaluate_text(tc, text, work, runs, uses_addr, stop_early=False, cover=None):
    """Compile `text` and run it under every (opt, tag) in `runs`; returns (findings, setup_error, nruns, collections).
    `cover`, when given, is a dict that receives the --jit=1 run's compile coverage."""
    os.makedirs(work, exist_ok=True)
    src = os.path.join(work, "prog.obs")
    with open(src, "w", newline="\n") as f:
        f.write(text)
    obes = {}
    for opt in sorted({"s0"} | {o for o, _ in runs}):
        dest = os.path.join(work, "prog_%s.obe" % opt)
        r = tc.compile(src, opt, dest)
        if r.code != 0 or not os.path.exists(dest):
            return [], "compile -opt %s failed: %s" % (opt, (r.out + r.err).strip()[-400:]), 0, 0
        obes[opt] = dest
    ref = tc.execute(obes["s0"], REF[2], REF[3])
    if not good_output(ref):
        return [], "the reference run failed: %s %s" % (ref.describe(), (ref.out + ref.err).strip()[-300:]), 1, 0
    findings = []
    n = 1
    collections = 0
    refused_by_opt = {}      # opt -> generated methods the --jit=1 run could not compile
    jit_output_diffs = []    # (opt, finding index) output mismatches of JIT configurations
    for opt, tag in runs:
        if uses_addr and tag in MIXED_TAGS:
            continue
        flags, extra = RUNS[tag]
        name = "%s/%s" % (opt, tag)
        res = tc.execute(obes[opt], flags, extra)
        n += 1
        if res.timed_out or res.code != 0:
            findings.append((name, "%s; stdout tail %r; stderr tail %r" % (res.describe(), res.out[-200:], res.err.strip()[-300:])))
        elif res.out != ref.out:
            findings.append((name, "output differs from ref: %s" % first_diff(ref.out, res.out)))
            if "--jit=1" in flags:
                jit_output_diffs.append(len(findings) - 1)
        if extra.get("OBJECK_JIT_REPORT"):
            ok, no, why = jit_coverage(res.err)
            refused_by_opt[opt] = no
            if cover is not None:
                cover["compiled"] = cover.get("compiled", 0) + ok
                cover["refused"] = cover.get("refused", 0) + no
                cover.setdefault("reasons", {})
                for k, v in why.items():
                    cover["reasons"][k] = cover["reasons"].get(k, 0) + v
        if extra.get("OBJECK_GC_ROOTS") == "audit" and not res.timed_out and res.code == 0:
            collections += audit_counts(res.err).get("collections", 0)
            bad = audit_problems(res, uses_addr)
            if bad:
                findings.append((name, "roots audit: " + ", ".join(bad)))
        if findings and stop_early:
            break
    if uses_addr:
        # a method the JIT refused runs interpreted under compiled callers, where an address-valued
        # Int is still scanned on a guess (docs/GC_ROOTS_DESIGN.md section 12): not a finding
        keep = [f for i, f in enumerate(findings)
                if not (i in jit_output_diffs and refused_by_opt.get(f[0].split("/")[0], 1))]
        findings = keep
    return findings, None, n, collections


def run_seed(tc, seed, runs, out_dir, keep, scale, addr):
    r = SeedResult(seed)
    t = time.monotonic()
    text, meta = gen_roots.generate(seed, scale, addr)
    r.features = meta["features"]
    work = tempfile.mkdtemp(prefix="rc%d_" % seed, dir=out_dir)
    findings, err, n, coll = evaluate_text(tc, text, work, runs, meta["address_int"], cover=r.cover)
    r.findings, r.setup_error, r.runs, r.collections = findings, err, n, coll
    r.seconds = time.monotonic() - t
    if findings or err or keep:
        dest = os.path.join(out_dir, "seed_%d" % seed)
        shutil.rmtree(dest, ignore_errors=True)
        shutil.copytree(work, dest)
        with open(os.path.join(dest, "report.txt"), "w") as f:
            f.write("seed %d (scale %s)\nfeatures: %s\n" % (seed, scale, ", ".join(r.features)))
            if err:
                f.write("SETUP: %s\n" % err)
            for name, why in findings:
                f.write("FAIL %s: %s\n" % (name, why))
            f.write("\nreproduce: python tools/gc_roots/run_roots.py --bin <bin> --mode full --start %d --count 1%s\n"
                    % (seed, "" if scale == 1.0 else " --scale %s" % scale))
            f.write("minimize:  python tools/gc_roots/run_roots.py --bin <bin> --reduce %d\n" % seed)
    shutil.rmtree(work, ignore_errors=True)
    return r


# ---- minimizer ---------------------------------------------------------------------------

def chunks(lines):
    """Statement-sized chunks of an indented program: a line, or a line that opens a
    block through the line that closes it. Returns (start, end) pairs, outermost first."""
    out = []
    n = len(lines)

    def indent(i):
        return len(lines[i]) - len(lines[i].lstrip())
    for i in range(n):
        s = lines[i].strip()
        if not s or s.startswith("#") or s.startswith("use "):
            continue
        end = i
        if s.endswith("{"):
            k = indent(i)
            j = i + 1
            while j < n and not (indent(j) == k and lines[j].lstrip().startswith("}") and not lines[j].rstrip().endswith("{")):
                j += 1
            end = min(j, n - 1)
        out.append((i, end))
    out.sort(key=lambda c: -(c[1] - c[0]))
    return out


def reduce_program(tc, text, runs, uses_addr, target, work, log):
    """Greedy hierarchical delta debugging: drop the biggest removable chunk until none is."""
    def failing(candidate):
        findings, err, _n, _c = evaluate_text(tc, candidate, work, [target], uses_addr, stop_early=True)
        return err is None and any(f[0] == "%s/%s" % target for f in findings)

    lines = text.split("\n")
    tests = 0
    tried = set()          # chunks (their text) that could not be removed
    changed = True
    while changed:
        changed = False
        for (a, b) in chunks(lines):
            if b >= len(lines):
                continue
            key = "\n".join(lines[a:b + 1])
            if key in tried:
                continue
            cand = lines[:a] + lines[b + 1:]
            tests += 1
            if failing("\n".join(cand)):
                lines = cand
                changed = True
                log("  removed lines %d-%d (%d lines left, %d tests)" % (a + 1, b + 1, len(lines), tests))
                break
            tried.add(key)
    return "\n".join(lines)


def seeds_for(args):
    return list(range(args.start, args.start + args.count))


def main():
    ap = argparse.ArgumentParser(description=__doc__.split("\n")[0])
    ap.add_argument("--bin", required=True, help="a deploy tree's bin directory (obc, obr; ../lib beside it)")
    ap.add_argument("--mode", choices=sorted(MODES), default="ci")
    ap.add_argument("--start", type=int, default=1)
    ap.add_argument("--count", type=int, default=None, help="seeds to run (default 30 in ci mode, 200 in full mode)")
    ap.add_argument("-j", "--jobs", type=int, default=4)
    ap.add_argument("--scale", type=float, default=1.0, help="generator statement-count multiplier")
    ap.add_argument("--addr", choices=("auto", "on", "off"), default="off",
                    help="address-valued Int statements (Runtime->TestAddressOf): off by default; auto = seeds divisible by 3")
    ap.add_argument("--out", default=None, help="directory for failing seeds (default: ./gc_roots_out)")
    ap.add_argument("--keep", action="store_true", help="keep every seed's program, not only failing ones")
    ap.add_argument("--timeout", type=float, default=TIMEOUT)
    ap.add_argument("--reduce", type=int, default=None, metavar="SEED", help="minimize a divergent seed")
    ap.add_argument("--config", default=None, help="with --reduce: the failing configuration, e.g. s3/jit1")
    a = ap.parse_args()
    if a.count is None:
        a.count = 30 if a.mode == "ci" else 200
    tc = Toolchain(a.bin)
    tc.timeout = a.timeout
    if not tc.ok():
        print("no obc/obr in %s" % a.bin, file=sys.stderr)
        return 2
    out_dir = os.path.abspath(a.out or "gc_roots_out")
    os.makedirs(out_dir, exist_ok=True)
    runs = MODES[a.mode]

    if a.reduce is not None:
        text, meta = gen_roots.generate(a.reduce, a.scale, a.addr)
        work = tempfile.mkdtemp(prefix="rcreduce_", dir=out_dir)
        findings, err, _n, _c = evaluate_text(tc, text, work, MODES["full"], meta["address_int"])
        if err:
            print("setup problem: " + err)
            return 2
        if not findings:
            print("seed %d agrees in every configuration; nothing to reduce" % a.reduce)
            return 0
        target = tuple(a.config.split("/")) if a.config else tuple(findings[0][0].split("/"))
        print("reducing seed %d against %s/%s: %s" % (a.reduce, target[0], target[1], findings[0][1][:200]))
        small = reduce_program(tc, text, runs, meta["address_int"], target, work, lambda s: print(s, flush=True))
        dest = os.path.join(out_dir, "seed_%d_reduced.obs" % a.reduce)
        with open(dest, "w", newline="\n") as f:
            f.write(small)
        print("wrote %s (%d lines, from %d)" % (dest, small.count("\n") + 1, text.count("\n") + 1))
        shutil.rmtree(work, ignore_errors=True)
        return 1

    seeds = seeds_for(a)
    t0 = time.monotonic()
    failed, broken, nruns, colls = [], [], 0, 0
    cover = {"compiled": 0, "refused": 0, "reasons": {}}
    done = 0
    with concurrent.futures.ThreadPoolExecutor(max_workers=max(1, a.jobs)) as pool:
        futs = {pool.submit(run_seed, tc, s, runs, out_dir, a.keep, a.scale, a.addr): s for s in seeds}
        for fut in concurrent.futures.as_completed(futs):
            r = fut.result()
            done += 1
            nruns += r.runs
            colls += r.collections
            cover["compiled"] += r.cover.get("compiled", 0)
            cover["refused"] += r.cover.get("refused", 0)
            for k, v in r.cover.get("reasons", {}).items():
                cover["reasons"][k] = cover["reasons"].get(k, 0) + v
            if r.setup_error:
                broken.append(r)
                print("SETUP seed %d: %s" % (r.seed, r.setup_error), flush=True)
            elif r.findings:
                failed.append(r)
                for name, why in r.findings:
                    print("FAIL seed %d %s: %s" % (r.seed, name, why[:300]), flush=True)
            if done % 25 == 0:
                print("  %d/%d seeds, %d failing (%.0fs)" % (done, len(seeds), len(failed), time.monotonic() - t0), flush=True)
    print("%d seeds (%d..%d), %d runs, %d audited collections, mode %s, %.0fs: %d diverged, %d did not compile/run the reference"
          % (len(seeds), seeds[0], seeds[-1], nruns, colls, a.mode, time.monotonic() - t0, len(failed), len(broken)))
    total = cover["compiled"] + cover["refused"]
    if total:
        top = sorted(cover["reasons"].items(), key=lambda kv: -kv[1])[:3]
        print("jit coverage (--jit=1 runs): %d of %d generated methods compiled (%.0f%%)%s"
              % (cover["compiled"], total, 100.0 * cover["compiled"] / total,
                 "; not compiled: " + "; ".join("%s x%d" % kv for kv in top) if top else ""))
    summary = {"mode": a.mode, "start": a.start, "count": a.count, "diverged": sorted(r.seed for r in failed),
               "broken": sorted(r.seed for r in broken)}
    with open(os.path.join(out_dir, "summary.json"), "w") as f:
        json.dump(summary, f, indent=1)
    if failed:
        print("divergent seeds: %s (programs and reports under %s)" % (summary["diverged"], out_dir))
        return 1
    if broken:
        return 2
    return 0


if __name__ == "__main__":
    sys.exit(main())
