#!/usr/bin/env python3
"""Differential fuzzer driver for Objeck.

    python tools/fuzz/run_fuzz.py --bin core/release/deploy-x64/bin --count 300 --seed 1 -j 6

Each program (tools/fuzz/gen.py) is compiled at s0 and s3 and run under
s0/off (reference), s3/off, s3/default, s3/jit1 and s0/jit1. Runs that disagree
on stdout or on zero/non-zero exit, crash, time out, or whose compile crashes
or writes no .obe, are findings. A finding's signature (fuzzlib.signature) is
matched against known.json; unmatched signatures are new.

Under s3/jit1 the VM runs with OBJECK_JIT_REPORT=1, and every method the JIT
hands back to the interpreter is counted against the generated methods. The run
fails when fewer than --min-jit (default 90%) were compiled overall, because a
differential fuzzer whose methods never reach the JIT compares the interpreter
with itself.

Findings are saved under --out/<signature hash>/seed_<n>/ with the program,
its recorded choices (for reduce.py) and every run's output. Exit status: 0
clean, 1 new findings or JIT coverage under the floor, 2 usage error.
"""

import argparse
import concurrent.futures
import hashlib
import json
import os
import shutil
import sys
import time

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import fuzzlib  # noqa: E402
import gen      # noqa: E402

HERE = os.path.dirname(os.path.abspath(__file__))


def sig_hash(sig):
    return hashlib.sha1(sig.encode("utf-8")).hexdigest()[:10]


def fuzz_one(tc, seed, features, work_root, keep):
    prog = gen.generate(seed=seed, features=features)
    workdir = os.path.join(work_root, "seed_%d" % seed)
    outcome = fuzzlib.evaluate(tc, prog.text, workdir, "prog", prog.methods, gen.CLASS_PREFIXES)
    return prog, outcome, workdir


def save_finding(out_dir, prog, outcome, workdir, features):
    dest = os.path.join(out_dir, sig_hash(outcome.signature), "seed_%s" % prog.seed)
    os.makedirs(dest, exist_ok=True)
    with open(os.path.join(dest, "prog.obs"), "w", encoding="utf-8", newline="\n") as f:
        f.write(prog.text)
    with open(os.path.join(dest, "choices.json"), "w", encoding="utf-8") as f:
        json.dump({"seed": prog.seed, "features": prog.features,
                   "forced_features": sorted(features) if features is not None else None,
                   "choices": prog.choices}, f)
    with open(os.path.join(dest, "outcome.json"), "w", encoding="utf-8") as f:
        json.dump(outcome.to_json(), f, indent=1)
    return dest


def main(argv=None):
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--bin", required=True, help="deploy tree bin directory (obc, obr)")
    ap.add_argument("--count", type=int, default=100)
    ap.add_argument("--seed", type=int, default=1, help="first seed; programs use seed..seed+count-1")
    ap.add_argument("-j", "--jobs", type=int, default=4)
    ap.add_argument("--features", default=None, help="force a feature set, e.g. F2,F5 (default: swarm)")
    ap.add_argument("--timeout", type=float, default=30.0, help="seconds per run")
    ap.add_argument("--obc", default=None, help="override the compiler (a .py file runs under python)")
    ap.add_argument("--obr", default=None, help="override the VM (a .py file runs under python)")
    ap.add_argument("--known", default=os.path.join(HERE, "known.json"))
    ap.add_argument("--out", default=os.path.join(HERE, "out"))
    ap.add_argument("--min-jit", type=float, default=0.90, help="minimum JIT-compiled fraction")
    ap.add_argument("--keep", action="store_true", help="keep every program's work directory")
    ap.add_argument("--json", default=None, help="write the run summary here")
    args = ap.parse_args(argv)

    if not os.path.isdir(args.bin):
        print("no such bin directory: %s" % args.bin, file=sys.stderr)
        return 2
    features = None
    if args.features is not None:
        features = [f for f in args.features.split(",") if f]
        bad = [f for f in features if f not in gen.FEATURES]
        if bad:
            print("unknown feature(s): %s" % ",".join(bad), file=sys.stderr)
            return 2

    tc = fuzzlib.Toolchain(args.bin, obc=args.obc, obr=args.obr, timeout=args.timeout)
    known = fuzzlib.load_known(args.known)
    work_root = os.path.join(args.out, "work_%d_%d" % (args.seed, os.getpid()))
    os.makedirs(work_root, exist_ok=True)

    start = time.monotonic()
    stats = {"programs": 0, "clean": 0, "known": 0, "new": 0}
    signatures = {}      # signature -> {"count", "known", "first_seed", "dir"}
    compiled = total = 0
    rejected_names = {}
    seeds = range(args.seed, args.seed + args.count)

    with concurrent.futures.ThreadPoolExecutor(max_workers=max(1, args.jobs)) as pool:
        futures = {pool.submit(fuzz_one, tc, s, features, work_root, args.keep): s for s in seeds}
        for fut in concurrent.futures.as_completed(futures):
            prog, outcome, workdir = fut.result()
            stats["programs"] += 1
            c, t, rej = outcome.coverage
            compiled += c
            total += t
            for name in rej:
                rejected_names[name] = rejected_names.get(name, 0) + 1
            sig = outcome.signature
            if sig is None:
                stats["clean"] += 1
            else:
                entry = signatures.get(sig)
                k = fuzzlib.match_known(sig, known)
                if entry is None:
                    entry = signatures[sig] = {"count": 0, "known": k.get("note", True) if k else None,
                                               "first_seed": prog.seed, "dir": None}
                entry["count"] += 1
                if k:
                    stats["known"] += 1
                else:
                    stats["new"] += 1
                if entry["dir"] is None or entry["count"] <= 3:
                    d = save_finding(args.out, prog, outcome, workdir, features)
                    entry["dir"] = entry["dir"] or d
                tag = "known" if k else "NEW"
                print("[%s] seed %d: %s" % (tag, prog.seed, sig), flush=True)
            if not args.keep:
                shutil.rmtree(workdir, ignore_errors=True)
            if stats["programs"] % 25 == 0:
                print("... %d/%d programs, %d new, %d known, %.0fs" %
                      (stats["programs"], args.count, stats["new"], stats["known"], time.monotonic() - start),
                      flush=True)
    if not args.keep:
        shutil.rmtree(work_root, ignore_errors=True)

    wall = time.monotonic() - start
    fraction = (compiled / total) if total else 0.0
    print("")
    print("programs: %d  clean: %d  known: %d  new: %d" %
          (stats["programs"], stats["clean"], stats["known"], stats["new"]))
    print("signatures: %d distinct" % len(signatures))
    for sig, e in sorted(signatures.items(), key=lambda kv: -kv[1]["count"]):
        print("  %4d x %s %s  (seed %s, %s)" % (e["count"], "known" if e["known"] else "NEW  ", sig,
                                                e["first_seed"], e["dir"]))
    print("JIT-compiled fraction (s3/jit1): %d/%d = %.1f%%" % (compiled, total, 100.0 * fraction))
    if rejected_names:
        top = sorted(rejected_names.items(), key=lambda kv: -kv[1])[:10]
        print("  most rejected: " + ", ".join("%s x%d" % kv for kv in top))
    print("wall time: %.1fs (%.2fs/program, -j %d)" % (wall, wall / max(1, stats["programs"]), args.jobs))

    if args.json:
        with open(args.json, "w", encoding="utf-8") as f:
            json.dump({"stats": stats, "jit_compiled": compiled, "jit_total": total,
                       "jit_fraction": fraction, "wall_seconds": wall,
                       "signatures": signatures, "rejected": rejected_names}, f, indent=1)

    status = 0
    if stats["new"]:
        status = 1
    if total and fraction < args.min_jit:
        print("FAIL: JIT-compiled fraction %.1f%% is under the %.0f%% floor" % (100 * fraction, 100 * args.min_jit))
        status = 1
    if stats["programs"] and not total:
        print("FAIL: no JIT coverage was measured")
        status = 1
    return status


if __name__ == "__main__":
    sys.exit(main())
