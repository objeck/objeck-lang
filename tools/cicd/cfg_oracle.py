#!/usr/bin/env python3
"""Dynamic oracle for the compiler's control-flow graph (core/compiler/cfg.h).

The graph is analysis only, and its own checks (Cfg::Verify) cannot tell whether it matches
what the machine does. This does: it runs programs and compares the instruction transitions
the interpreter really took with the edges the graph says exist.

For each test program and each -opt level:
  1. obc compiles it with OBJECK_CFG_DUMP=<file>: the optimizer writes every method's final
     graph (the list JumpToLocation is about to resolve; its instruction indexes are the VM's).
  2. obr, built with -DOBJECK_CFG_TRACE (see core/vm/cfg_trace.h), runs it with --jit=off and
     OBJECK_CFG_TRACE=<file>: it writes every instruction index executed, every jump taken and
     every trap that went to a handler.
  3. Every executed transition must be allowed by the graph:
       - a jump or a trap: from the last real instruction of block X to (the first instruction
         after the label of) a normal successor, or from ANY instruction of X to a handler
         successor;
       - a fall-through out of a block: X's last instruction to the next block's first, and the
         next block must be a normal successor;
     and no instruction of a block the graph calls unreachable may ever execute.
Anything else is a graph bug (the graph misses an edge a real run took).

usage: cfg_oracle.py <bin_dir> [-j N] [--opts s0,s1,s2,s3] [--timeout S] [--work DIR] [tests...]
<bin_dir> holds obc and an obr built with -DOBJECK_CFG_TRACE; tests are names in
programs/regression (none: every one), or paths to .obs files (clbg, perf).
"""
import argparse, glob, os, sys, tempfile, shutil, concurrent.futures, threading

HERE = os.path.dirname(os.path.abspath(__file__))
REG = os.path.abspath(os.path.join(HERE, "..", "..", "programs", "regression"))
sys.path.insert(0, REG)
import run_differential as rd  # parse_markers, make_env, BASE_LIBS


def parse_dump(path):
    methods = {}
    skipped = {}
    cur = None
    if not os.path.isfile(path):
        return methods, skipped
    with open(path, encoding="utf-8", errors="replace") as fh:
        for line in fh:
            parts = line.split()
            if not parts:
                continue
            if parts[0] == "M":
                cur = {"n": int(parts[2]), "blocks": []}
                methods[parts[1]] = cur
            elif parts[0] == "X":
                skipped[parts[1]] = " ".join(parts[2:])
                cur = None
            elif parts[0] == "B" and cur is not None:
                bid, first, last, lastreal, reach, islbl = [int(x) for x in parts[1:7]]
                i = parts.index("n")
                j = parts.index("h")
                cur["blocks"].append({
                    "id": bid, "first": first, "last": last, "lastreal": lastreal,
                    "reach": reach == 1, "islbl": islbl == 1,
                    "normal": [int(x) for x in parts[i + 1:j]],
                    "handlers": [int(x) for x in parts[j + 1:]],
                })
    return methods, skipped


def parse_trace(path):
    methods = {}
    cur = None
    if not os.path.isfile(path):
        return methods
    with open(path, encoding="utf-8", errors="replace") as fh:
        for line in fh:
            parts = line.split()
            if not parts:
                continue
            if parts[0] == "M" and len(parts) == 3:
                name = parts[1]
                cur = methods.setdefault(name, {"n": int(parts[2]), "X": set(), "F": set(), "E": set()})
            elif cur is not None and parts[0] in ("X", "F") and len(parts) == 2:
                cur[parts[0]].add(int(parts[1]))
            elif cur is not None and parts[0] == "E" and len(parts) == 3:
                cur["E"].add((int(parts[1]), int(parts[2])))
    return methods


def check(dump, trace):
    """Returns (methods_checked, transitions_checked, unmatched_names, violations)."""
    checked = 0
    transitions = 0
    unmatched = []
    violations = []
    for name, t in trace.items():
        g = dump.get(name)
        if g is None:
            unmatched.append(name)
            continue
        if g["n"] != t["n"]:
            violations.append("%s: instruction count differs (graph %d, vm %d)" % (name, g["n"], t["n"]))
            continue
        checked += 1
        blocks = g["blocks"]
        block_of = [None] * (g["n"] + 1)
        for b in blocks:
            for k in range(b["first"], b["last"] + 1):
                block_of[k] = b
        byid = {b["id"]: b for b in blocks}

        def land(b):
            return {b["first"], b["first"] + 1} if b["islbl"] else {b["first"]}

        for ip in sorted(t["X"]):
            b = block_of[ip]
            if b is None:
                violations.append("%s: ip %d is in no block" % (name, ip))
            elif not b["reach"]:
                violations.append("%s: ip %d executed in block %d, which the graph calls unreachable" % (name, ip, b["id"]))
        for ip in sorted(t["F"]):
            transitions += 1
            b = block_of[ip]
            if b is None:
                continue
            if ip < b["lastreal"]:
                continue
            nxt = block_of[ip + 1] if ip + 1 <= g["n"] else None
            if nxt is None or nxt["id"] not in b["normal"] or ip + 1 != nxt["first"]:
                violations.append("%s: fell through from ip %d (end of block %d) to ip %d, not an edge of the graph" % (name, ip, b["id"], ip + 1))
        for (frm, to) in sorted(t["E"]):
            transitions += 1
            b = block_of[frm]
            if b is None or to > g["n"] or block_of[to] is None:
                violations.append("%s: transition %d -> %d outside the method" % (name, frm, to))
                continue
            ok = False
            if frm == b["lastreal"]:
                for sid in b["normal"]:
                    if to in land(byid[sid]):
                        ok = True
                        break
            if not ok:
                for sid in b["handlers"]:
                    if to in land(byid[sid]):
                        ok = True
                        break
            if not ok:
                violations.append("%s: executed %d -> %d (block %d -> block %d) is not an edge of the graph; normal=%s handlers=%s"
                                  % (name, frm, to, b["id"], block_of[to]["id"], b["normal"], b["handlers"]))
    return checked, transitions, unmatched, violations


class Stats:
    def __init__(self):
        self.lock = threading.Lock()
        self.programs = 0
        self.methods = 0
        self.transitions = 0
        self.unmatched = 0
        self.skipped_methods = 0
        self.violations = []
        self.problems = []
        self.nonrun = []

    def add(self, label, checked, transitions, unmatched, skipped, violations):
        with self.lock:
            self.programs += 1
            self.methods += checked
            self.transitions += transitions
            self.unmatched += len(unmatched)
            self.skipped_methods += skipped
            for v in violations:
                self.violations.append("%s: %s" % (label, v))


def one(args, stats, env, obc, obr, src, name, markers, opts):
    libs = ",".join([rd.BASE_LIBS] + markers["extra_libs"])
    for opt in opts:
        wd = os.path.join(args.work, "%s_%s" % (name, opt))
        os.makedirs(wd, exist_ok=True)
        dump = os.path.join(wd, "dump.txt")
        trace = os.path.join(wd, "trace.txt")
        for f in [dump] + glob.glob(trace + ".*"):
            if os.path.exists(f):
                os.remove(f)
        obe = os.path.join(wd, name + ".obe")
        e = dict(env, OBJECK_CFG_DUMP=dump)
        rc, out, err, _ = rd.run_proc([obc, "-src", src, "-lib", libs, "-opt", opt, "-dest", obe], os.path.dirname(obc), e, args.timeout)
        if rc != 0 or not os.path.isfile(obe):
            with stats.lock:
                stats.nonrun.append("%s -opt %s: compile failed" % (name, opt))
            continue
        e = dict(env, OBJECK_CFG_TRACE=trace, OBJECK_TEST_OBE=obe)
        rc, out, err, _ = rd.run_proc([obr, "--jit=off", obe], os.path.dirname(src), e, args.timeout)
        traces = sorted(glob.glob(trace + ".*"))
        if rc is None or not traces:
            with stats.lock:
                stats.nonrun.append("%s -opt %s: %s" % (name, opt, "timed out" if rc is None else "no trace written"))
            continue
        dm, skipped = parse_dump(dump)
        for tf in traces:
            tm = parse_trace(tf)
            checked, transitions, unmatched, violations = check(dm, tm)
            stats.add("%s -opt %s" % (name, opt), checked, transitions, unmatched, len([n for n in tm if n in skipped]), violations)
        if not args.keep and not violations:
            shutil.rmtree(wd, ignore_errors=True)


def main(argv=None):
    ap = argparse.ArgumentParser()
    ap.add_argument("bin_dir")
    ap.add_argument("tests", nargs="*")
    ap.add_argument("-j", type=int, default=8)
    ap.add_argument("--opts", default="s0,s1,s2,s3")
    ap.add_argument("--timeout", type=int, default=120)
    ap.add_argument("--work", default=os.path.join(tempfile.gettempdir(), "cfg_oracle"))
    ap.add_argument("--keep", action="store_true")
    args = ap.parse_intermixed_args(argv)
    bin_dir = os.path.abspath(args.bin_dir)
    lib_dir = os.path.abspath(os.path.join(bin_dir, "..", "lib"))
    env = rd.make_env(bin_dir, lib_dir)
    obc = rd.find_tool(bin_dir, "obc")
    obr = rd.find_tool(bin_dir, "obr")
    opts = args.opts.split(",")
    os.makedirs(args.work, exist_ok=True)

    todo = []
    names = args.tests
    if not names:
        names = sorted(os.path.splitext(f)[0] for f in os.listdir(REG) if f.endswith(".obs"))
    for n in names:
        if n.endswith(".obs") and os.path.isfile(n):
            src = os.path.abspath(n)
            name = os.path.splitext(os.path.basename(n))[0]
        else:
            src = os.path.join(REG, n + ".obs")
            name = n
        with open(src, encoding="utf-8", errors="replace") as fh:
            markers = rd.parse_markers(fh.read())
        if markers["compile_error"] or markers["errors"]:
            continue
        todo.append((src, name, markers))

    stats = Stats()
    with concurrent.futures.ThreadPoolExecutor(max_workers=args.j) as pool:
        futs = [pool.submit(one, args, stats, env, obc, obr, src, name, markers, opts) for src, name, markers in todo]
        for f in futs:
            f.result()

    print("cfg_oracle: %d program/opt runs, %d methods checked, %d transitions checked, %d trace methods without a graph, "
          "%d methods the graph skipped, %d runs not checked, %d VIOLATIONS"
          % (stats.programs, stats.methods, stats.transitions, stats.unmatched, stats.skipped_methods, len(stats.nonrun), len(stats.violations)))
    by_test = {}
    for v in stats.violations:
        by_test[v.split(":")[0]] = by_test.get(v.split(":")[0], 0) + 1
    for k, n in sorted(by_test.items(), key=lambda kv: -kv[1])[:40]:
        print("  %6d violation(s) in %s" % (n, k))
    for v in stats.violations[:30]:
        print("  VIOLATION " + v)
    if os.environ.get("ORACLE_SHOW_NONRUN"):
        for v in stats.nonrun:
            print("  not run: " + v)
    return 1 if stats.violations else 0


if __name__ == "__main__":
    sys.exit(main())
