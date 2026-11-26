#!/usr/bin/env python3
"""
RQ5 (docs/design/MCP_FIRST_CLASS.md): the feature does not slow the compiler.

Compiles a random sample of programs/regression with two compilers, alternating, and reports each run and the
median. Quiet machine, one process at a time: the point is a difference of a few percent, so run nothing else.

Usage: compile_time.py <repo_root> <reference_bin_dir> <new_bin_dir> [programs=60] [runs=5] [seed=7]
"""
import os, random, re, statistics, subprocess, sys, time

EXE = ".exe" if os.name == "nt" else ""


def libs(path):
    out = "cipher,collect,xml,json"
    for line in open(path, encoding="utf-8", errors="replace"):
        m = re.match(r"\s*#\s*EXTRA_LIBS:\s*(\S+)", line)
        if m:
            out += "," + m.group(1)
    return out


def main():
    if len(sys.argv) < 4:
        print(__doc__)
        return 2
    root, ref, new = sys.argv[1:4]
    count = int(sys.argv[4]) if len(sys.argv) > 4 else 60
    runs = int(sys.argv[5]) if len(sys.argv) > 5 else 5
    seed = int(sys.argv[6]) if len(sys.argv) > 6 else 7
    reg = os.path.join(root, "programs", "regression")
    files = sorted(f for f in os.listdir(reg) if f.endswith(".obs") and not f.startswith(("bad_", "mcp_")))
    random.Random(seed).shuffle(files)
    files = files[:count]
    scratch = os.path.join(os.path.dirname(os.path.abspath(__file__)), "_ct.obe")

    def run(bin_dir):
        env = dict(os.environ, OBJECK_LIB_PATH=os.path.join(os.path.dirname(bin_dir), "lib"))
        start = time.perf_counter()
        for f in files:
            p = os.path.join(reg, f)
            subprocess.run([os.path.join(bin_dir, "obc" + EXE), "-src", p, "-lib", libs(p), "-opt", "s3", "-dest", scratch],
                           capture_output=True, env=env, cwd=reg)
        return time.perf_counter() - start

    times = {"reference": [], "new": []}
    for i in range(runs):
        order = (("reference", ref), ("new", new)) if i % 2 == 0 else (("new", new), ("reference", ref))
        for name, bin_dir in order:
            times[name].append(run(bin_dir))
    for name in times:
        print("%-9s %s  median %.2f s" % (name, " ".join("%.2f" % t for t in times[name]), statistics.median(times[name])))
    print("delta %+.1f%%" % (100 * (statistics.median(times["new"]) / statistics.median(times["reference"]) - 1)))
    try:
        os.remove(scratch)
    except OSError:
        pass
    return 0


if __name__ == "__main__":
    sys.exit(main())
