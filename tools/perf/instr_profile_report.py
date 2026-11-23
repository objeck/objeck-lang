#!/usr/bin/env python3
"""Summarize opcode-frequency profiles written by an obr built with -DOBJECK_INSTR_PROFILE.

    obr --jit=off program.obe 2> prof.txt        (or OBJECK_INSTR_PROFILE_OUT=prof.txt)
    python tools/perf/instr_profile_report.py prof.txt [more.txt ...] [--top N]

Several profiles are summed, so a whole suite reads as one workload. Opcode names
come from the InstructionType enum in core/shared/instrs.h: the VM prints only
numeric ids, so nothing here can fall out of step with the instruction set.

Only instructions the interpreter dispatched are counted -- run with --jit=off, or
compiled methods are invisible. A pair or triple is a run of instructions that
executed one after another in the same method (a jump, call or return ends it),
which is what a fused instruction could replace.

Prints: the opcodes by count, the pairs and triples by count, and how much of all
dispatches the top few cover, which is the ceiling on what fusing them can save.
"""

import argparse
import os
import re
import sys

REPO_ROOT = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
INSTRS_H = os.path.join(REPO_ROOT, "core", "shared", "instrs.h")


def opcode_names(path=INSTRS_H):
    text = open(path, encoding="utf-8", errors="replace").read()
    match = re.search(r"enum InstructionType\s*\{(.*?)\};", text, re.S)
    if not match:
        sys.exit("no InstructionType enum in %s" % path)
    body = re.sub(r"//.*", "", match.group(1))
    body = re.sub(r"/\*.*?\*/", "", body, flags=re.S)
    return [part.strip().split("=")[0].strip() for part in body.split(",") if part.strip()]


def load(paths):
    ops, pairs, triples = {}, {}, {}
    for path in paths:
        with open(path, encoding="utf-8", errors="replace") as fh:
            for line in fh:
                parts = line.split()
                if not parts or parts[0].startswith("#"):
                    continue
                kind, nums = parts[0], [int(x) for x in parts[1:]]
                if kind == "op":
                    ops[nums[0]] = ops.get(nums[0], 0) + nums[1]
                elif kind == "pair":
                    pairs[tuple(nums[:2])] = pairs.get(tuple(nums[:2]), 0) + nums[2]
                elif kind == "triple":
                    triples[tuple(nums[:3])] = triples.get(tuple(nums[:3]), 0) + nums[3]
    return ops, pairs, triples


def main():
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("profiles", nargs="+")
    parser.add_argument("--top", type=int, default=20)
    args = parser.parse_args()

    names = opcode_names()
    name = lambda i: names[i] if 0 <= i < len(names) else "op%d" % i
    ops, pairs, triples = load(args.profiles)
    total = sum(ops.values())
    if not total:
        sys.exit("no instructions counted: was obr built with -DOBJECK_INSTR_PROFILE and run with --jit=off?")

    print("%s dispatched instructions across %d profile(s)" % (format(total, ","), len(args.profiles)))

    print("\nOpcodes")
    covered = 0
    for rank, (op, n) in enumerate(sorted(ops.items(), key=lambda kv: -kv[1])[:args.top], 1):
        covered += n
        print("  %2d  %-22s %14s  %5.1f%%   cumulative %5.1f%%" % (rank, name(op), format(n, ","), 100.0 * n / total, 100.0 * covered / total))

    adjacent = sum(pairs.values())
    print("\nPairs (%.1f%% of dispatches are the second of an adjacent pair)" % (100.0 * adjacent / total))
    covered = 0
    for rank, (key, n) in enumerate(sorted(pairs.items(), key=lambda kv: -kv[1])[:args.top], 1):
        covered += n
        print("  %2d  %-22s %-22s %14s  %5.1f%%   cumulative %5.1f%%" % (rank, name(key[0]), name(key[1]), format(n, ","), 100.0 * n / total, 100.0 * covered / total))

    print("\nTriples")
    covered = 0
    for rank, (key, n) in enumerate(sorted(triples.items(), key=lambda kv: -kv[1])[:args.top], 1):
        covered += n
        print("  %2d  %-20s %-20s %-20s %13s  %5.1f%%" % (rank, name(key[0]), name(key[1]), name(key[2]), format(n, ","), 100.0 * n / total))
    print("\nA triple fused into one instruction saves two of its three dispatches, so its share of "
          "all dispatches, times two thirds, is the most it can remove.")


if __name__ == "__main__":
    main()
