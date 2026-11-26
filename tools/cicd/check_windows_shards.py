#!/usr/bin/env python3
"""Proves the sharded Windows regression run covered every test, in both passes.

ci-windows.yml runs programs/regression as N parallel jobs, each taking every Nth test
(REGRESSION_SHARD=i/N, see run_regression.cmd) and each running the suite twice: with the
default JIT threshold and with every method compiled (OBJECK_JIT_THRESHOLD=1). A shard
uploads, next to its logs, one counts file per pass:

    <shards-dir>/winshard-<platform>-<i>/counts_default.txt
    <shards-dir>/winshard-<platform>-<i>/counts_jit1.txt        passed=P skipped=S failed=F expected=E

where E is the number of tests the shard was handed. A job result alone cannot tell a lost
shard from a finished one (a matrix whose shards were skipped can still report success), so
this script is what makes "all green" mean "all tests ran": it fails unless, for each pass,

  * a counts file exists and parses for each of the N shards,
  * each shard's passed+skipped+failed equals the tests it was handed,
  * the shards' totals add up to the number of tests in the directory, and
  * no test failed.

It writes a per-pass table to $GITHUB_STEP_SUMMARY when that is set.

Usage: check_windows_shards.py --platform windows-x64 --shards 6 \
           --tests-dir programs/regression --shards-dir shards
"""
import argparse
import glob
import os
import re
import sys

PASSES = ("default", "jit1")
FIELDS = ("passed", "skipped", "failed", "expected")


def read_counts(path):
    """Return {field: int}, or a string naming what is wrong with the file."""
    if not os.path.isfile(path):
        return "missing"
    text = open(path, encoding="utf-8", errors="replace").read().strip()
    found = dict(re.findall(r"(\w+)=(\S+)", text))
    out = {}
    for f in FIELDS:
        if not re.fullmatch(r"\d+", found.get(f, "")):
            return "unparseable (%r)" % text[:80]
        out[f] = int(found[f])
    return out


def check(platform, shards, total, shards_dir):
    """Return (markdown, problems)."""
    md = []
    problems = []
    for p in PASSES:
        md.append("#### %s, %s pass" % (platform, p))
        md.append("")
        md.append("| shard | tests | passed | skipped | failed |")
        md.append("| :---: | ---: | ---: | ---: | ---: |")
        sums = dict.fromkeys(FIELDS, 0)
        for i in range(shards):
            path = os.path.join(shards_dir, "winshard-%s-%d" % (platform, i), "counts_%s.txt" % p)
            c = read_counts(path)
            if isinstance(c, str):
                problems.append("%s pass, shard %d: counts file %s" % (p, i, c))
                md.append("| %d | %s | | | |" % (i, c))
                continue
            for f in FIELDS:
                sums[f] += c[f]
            done = c["passed"] + c["skipped"] + c["failed"]
            if done != c["expected"]:
                problems.append("%s pass, shard %d: ran %d of the %d tests it was handed" % (p, i, done, c["expected"]))
            if c["failed"]:
                problems.append("%s pass, shard %d: %d test(s) failed" % (p, i, c["failed"]))
            md.append("| %d | %d | %d | %d | %d |" % (i, c["expected"], c["passed"], c["skipped"], c["failed"]))
        done = sums["passed"] + sums["skipped"] + sums["failed"]
        md.append("| **all** | **%d** | **%d** | **%d** | **%d** |" % (sums["expected"], sums["passed"], sums["skipped"], sums["failed"]))
        md.append("")
        md.append("%d of %d tests accounted for." % (done, total))
        md.append("")
        if done != total:
            problems.append("%s pass: %d tests accounted for, the suite has %d" % (p, done, total))
        if sums["expected"] != total:
            problems.append("%s pass: the shards were handed %d tests, the suite has %d" % (p, sums["expected"], total))
    return "\n".join(md), problems


def main(argv):
    ap = argparse.ArgumentParser(description=__doc__.split("\n")[0])
    ap.add_argument("--platform", required=True)
    ap.add_argument("--shards", type=int, required=True)
    ap.add_argument("--tests-dir", required=True)
    ap.add_argument("--shards-dir", required=True)
    a = ap.parse_args(argv)

    total = len(glob.glob(os.path.join(a.tests_dir, "*.obs")))
    if total == 0:
        print("no *.obs under %s" % a.tests_dir)
        return 2
    md, problems = check(a.platform, a.shards, total, a.shards_dir)
    print(md)
    summary = os.environ.get("GITHUB_STEP_SUMMARY")
    if summary:
        with open(summary, "a", encoding="utf-8") as f:
            f.write("### Windows regression shards, %s\n\n%s\n\n" % (a.platform, md))
            if problems:
                f.write("**Problems**\n\n" + "\n".join("- " + x for x in problems) + "\n")
    if problems:
        print("\nPROBLEMS:")
        for x in problems:
            print("  - " + x)
        return 1
    print("\nEvery shard reported, and together they ran all %d tests in both passes." % total)
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
