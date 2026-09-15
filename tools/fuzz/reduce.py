#!/usr/bin/env python3
"""Reducer for fuzzer findings: shrinks the generator's recorded choice
sequence (Hypothesis-style), so every candidate is a valid program by
construction -- no text-level delta debugging, no syntax errors to filter.

    python tools/fuzz/reduce.py --bin core/release/deploy-x64/bin \\
        tools/fuzz/out/<hash>/seed_<n> --name jit1_basic_lambda

A candidate is interesting when:
  * it compiles at s0 and s3,
  * s0/off (the reference) exits 0 with no VM internal error ('>>>'),
  * the reference runs in at most 2x the original's time (+0.25 s of timer
    slack for sub-second programs), so reduction cannot trade the bug for a hang,
  * the configurations partition into the same output classes as the original.

The reduced program goes to --dest/<name>.obs (default tools/fuzz/findings).
"""

import argparse
import json
import os
import shutil
import sys
import time

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import fuzzlib  # noqa: E402
import gen      # noqa: E402

HERE = os.path.dirname(os.path.abspath(__file__))


def shortlex(seq):
    return (len(seq), seq)


def shrink(choices, test, max_tests=None, log=None):
    """Shrink `choices` while `test(candidate)` holds.

    `test` returns None when a candidate is not interesting, or the choice list
    the candidate actually consumed (so a replay's trailing, unused draws and
    out-of-range values are normalized away). Only shortlex-smaller results
    are accepted, so the loop always terminates. Returns (best, tests run)."""
    best = list(choices)
    seen = set()
    tests = [0]

    def attempt(cand):
        key = tuple(cand)
        if key in seen or shortlex(cand) >= shortlex(best):
            return False
        if max_tests is not None and tests[0] >= max_tests:
            return False
        seen.add(key)
        tests[0] += 1
        got = test(cand)
        if got is None:
            return False
        got = list(got)
        if shortlex(got) >= shortlex(best):
            got = cand
        best[:] = got
        if log:
            log("  -> %d choices (sum %d) after %d tests" % (len(best), sum(best), tests[0]))
        return True

    changed = True
    while changed and (max_tests is None or tests[0] < max_tests):
        changed = False
        # 1. truncate the tail: later draws are later statements and functions
        n = len(best)
        while n > 1:
            n //= 2
            if attempt(best[:len(best) - n]):
                changed = True
        # 2. delete chunks
        for size in (32, 16, 8, 4, 2, 1):
            i = len(best) - size
            while i >= 0:
                if attempt(best[:i] + best[i + size:]):
                    changed = True
                i -= 1 if size == 1 else max(1, size // 2)
                i = min(i, len(best) - size)
        # 3. zero chunks (0 is the simplest option at every draw)
        for size in (8, 4, 2, 1):
            i = 0
            while i + size <= len(best):
                if any(best[i:i + size]) and attempt(best[:i] + [0] * size + best[i + size:]):
                    changed = True
                i += size
        # 4. lower single values
        for i in range(len(best)):
            v = best[i]
            for smaller in (0, v // 2, v - 1):
                if 0 <= smaller < v and attempt(best[:i] + [smaller] + best[i + 1:]):
                    changed = True
                    break
    return best, tests[0]


def objeck_test(tc, original, forced, workdir, log=None, basic_lambdas=False):
    """The interestingness test for a real finding; see the module doc."""
    target = original.partition_text()
    limit = original.ref_seconds() * 2 + 0.25
    counter = [0]

    def test(cand):
        counter[0] += 1
        prog = gen.generate(choices=cand, features=forced, basic_lambdas=basic_lambdas)
        stem = "cand"
        d = os.path.join(workdir, "c%d" % counter[0])
        out = fuzzlib.evaluate(tc, prog.text, d, stem, prog.methods, gen.CLASS_PREFIXES)
        shutil.rmtree(d, ignore_errors=True)
        if out.compile and any(out.compile[o].code != 0 for o in fuzzlib.OPT_LEVELS):
            return None
        if not out.results:
            return None
        ref = out.results[fuzzlib.REFERENCE]
        if ref.timed_out or ref.code != 0 or ">>>" in ref.stderr:
            return None
        if ref.seconds > limit:
            return None
        if out.partition_text() != target:
            return None
        return prog.choices

    return test


def main(argv=None):
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("finding", help="a finding directory holding choices.json (from run_fuzz.py)")
    ap.add_argument("--bin", required=True)
    ap.add_argument("--name", default=None, help="output name (default: the finding's signature hash)")
    ap.add_argument("--dest", default=os.path.join(HERE, "findings"))
    ap.add_argument("--timeout", type=float, default=30.0)
    ap.add_argument("--max-tests", type=int, default=600)
    ap.add_argument("--obc", default=None)
    ap.add_argument("--obr", default=None)
    ap.add_argument("--basic-lambdas", action="store_true",
                    help="replay with gen.py's basic_lambdas knob on (implied by a finding recorded with it)")
    args = ap.parse_args(argv)

    with open(os.path.join(args.finding, "choices.json"), "r", encoding="utf-8") as f:
        rec = json.load(f)
    forced = rec.get("forced_features")
    basic = bool(rec.get("basic_lambdas", False)) or args.basic_lambdas
    tc = fuzzlib.Toolchain(args.bin, obc=args.obc, obr=args.obr, timeout=args.timeout)
    workdir = os.path.join(args.finding, "reduce_work")

    prog = gen.generate(choices=rec["choices"], features=forced, basic_lambdas=basic)
    original = fuzzlib.evaluate(tc, prog.text, os.path.join(workdir, "orig"), "orig", prog.methods,
                                gen.CLASS_PREFIXES)
    print("original: %d choices, %d lines" % (len(prog.choices), prog.text.count("\n")))
    print("  signature: %s" % original.signature)
    print("  partition: %s" % (original.partition_text() if original.results else "-"))
    test = objeck_test(tc, original, forced, workdir, log=print, basic_lambdas=basic)
    if test(prog.choices) is None:
        print("the original is not interesting under the reducer's test "
              "(compile failure, or the reference itself fails); nothing to reduce")
        return 1

    start = time.monotonic()
    best, tests = shrink(prog.choices, test, max_tests=args.max_tests, log=print)
    reduced = gen.generate(choices=best, features=forced, basic_lambdas=basic)
    final = fuzzlib.evaluate(tc, reduced.text, os.path.join(workdir, "final"), "final", reduced.methods,
                             gen.CLASS_PREFIXES)
    name = args.name or os.path.basename(os.path.dirname(os.path.abspath(args.finding)))
    os.makedirs(args.dest, exist_ok=True)
    dest = os.path.join(args.dest, name + ".obs")
    header = ("#~\nReduced fuzzer finding (tools/fuzz/reduce.py) from seed %s.\n"
              "signature: %s\npartition: %s\n~#\n" % (rec.get("seed"), final.signature, final.partition_text()))
    with open(dest, "w", encoding="utf-8", newline="\n") as f:
        f.write(header + reduced.text)
    with open(os.path.join(args.finding, "reduced_choices.json"), "w", encoding="utf-8") as f:
        json.dump({"choices": best, "forced_features": forced, "basic_lambdas": basic}, f)
    shutil.rmtree(workdir, ignore_errors=True)
    print("reduced: %d -> %d choices, %d -> %d lines, %d tests, %.0fs" %
          (len(prog.choices), len(best), prog.text.count("\n"), reduced.text.count("\n"), tests,
           time.monotonic() - start))
    print("wrote %s" % dest)
    return 0


if __name__ == "__main__":
    sys.exit(main())
