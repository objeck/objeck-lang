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
  * the shards' totals add up to the number of tests in the directory,
  * no test failed,
  * the tests that ran are, by name, exactly the tests in the directory, each once: the shard's
    log (regression_<pass>.log, which the runner writes a "Running: <test>..." line and one
    PASS/SKIP/FAIL line per test into) is read, so a shard that ran other tests, the same test
    twice, or nothing, does not pass on its counts alone, and
  * a test is skipped only if its source says it can be: a test that prints "SKIP:" when the
    machine lacks what it needs (an API key, an OpenGL context, a GPU, a model file) has that
    string in it. Any other test skipped is a problem, and so is a pass in which no test passed.

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


def may_skip(tests_dir):
    """Names of the tests whose source can print the runner's skip marker."""
    out = set()
    for path in glob.glob(os.path.join(tests_dir, "*.obs")):
        with open(path, encoding="utf-8", errors="replace") as f:
            if '"SKIP:' in f.read():
                out.add(os.path.splitext(os.path.basename(path))[0])
    return out


RUNNING = re.compile(r"^Running: (.+)\.\.\.$")
RESULT = re.compile(r"^ {2}(PASS|SKIP|FAIL)\b")


def read_log(path):
    """Return ([names in order run], {name: PASS|SKIP|FAIL|None}), or a string naming what is wrong.

    The runner (run_regression.cmd) writes "Running: <name>..." when it starts a test and a
    line of two blanks and PASS, SKIP or FAIL when it finishes it. The first such line after a
    "Running:" line is the test's; what follows it (a failing test's output) is not read."""
    if not os.path.isfile(path):
        return "log missing"
    order, status, cur = [], {}, None
    with open(path, encoding="utf-8", errors="replace") as f:
        for line in f:
            line = line.rstrip("\r\n")
            m = RUNNING.match(line)
            if m:
                cur = m.group(1)
                order.append(cur)
                status.setdefault(cur, None)
                continue
            m = RESULT.match(line)
            if m and cur is not None and status[cur] is None:
                status[cur] = m.group(1)
    return order, status


def check(platform, shards, names, shards_dir, skippable=()):
    """Return (markdown, problems). names: the tests in the suite, without .obs."""
    total = len(names)
    md = []
    problems = []
    for p in PASSES:
        md.append("#### %s, %s pass" % (platform, p))
        md.append("")
        md.append("| shard | tests | passed | skipped | failed |")
        md.append("| :---: | ---: | ---: | ---: | ---: |")
        sums = dict.fromkeys(FIELDS, 0)
        seen = {}
        for i in range(shards):
            shard_dir = os.path.join(shards_dir, "winshard-%s-%d" % (platform, i))
            path = os.path.join(shard_dir, "counts_%s.txt" % p)
            c = read_counts(path)
            log = read_log(os.path.join(shard_dir, "regression_%s.log" % p))
            if isinstance(log, str):
                problems.append("%s pass, shard %d: %s, so which tests ran is unknown" % (p, i, log))
            else:
                order, status = log
                for n in order:
                    if n in seen:
                        problems.append("%s pass: %s ran twice (shards %d and %d)" % (p, n, seen[n][0], i))
                    seen[n] = (i, status[n])
                if not isinstance(c, str):
                    tally = {k: sum(1 for v in status.values() if v == k) for k in ("PASS", "SKIP", "FAIL")}
                    if (tally["PASS"], tally["SKIP"], tally["FAIL"]) != (c["passed"], c["skipped"], c["failed"]):
                        problems.append("%s pass, shard %d: the log has %d passed, %d skipped, %d failed but the counts say %d, %d, %d"
                                        % (p, i, tally["PASS"], tally["SKIP"], tally["FAIL"], c["passed"], c["skipped"], c["failed"]))
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
        if sums["passed"] == 0:
            problems.append("%s pass: no test passed" % p)
        expected_names = set(names)
        for n in sorted(expected_names - set(seen)):
            problems.append("%s pass: %s never ran" % (p, n))
        for n in sorted(set(seen) - expected_names):
            problems.append("%s pass: %s ran but is not a test in the suite" % (p, n))
        for n in sorted(seen):
            st = seen[n][1]
            if st is None:
                problems.append("%s pass: %s started and never finished" % (p, n))
            elif st == "SKIP" and n not in skippable:
                problems.append("%s pass: %s was skipped, and nothing in it says it can be" % (p, n))
        skipped = sorted(n for n, v in seen.items() if v[1] == "SKIP")
        if skipped:
            md.append("Skipped (each can print SKIP: when the machine lacks something): " + ", ".join(skipped))
            md.append("")
    return "\n".join(md), problems


def main(argv):
    ap = argparse.ArgumentParser(description=__doc__.split("\n")[0])
    ap.add_argument("--platform", required=True)
    ap.add_argument("--shards", type=int, required=True)
    ap.add_argument("--tests-dir", required=True)
    ap.add_argument("--shards-dir", required=True)
    a = ap.parse_args(argv)

    names = sorted(os.path.splitext(os.path.basename(x))[0] for x in glob.glob(os.path.join(a.tests_dir, "*.obs")))
    if not names:
        print("no *.obs under %s" % a.tests_dir)
        return 2
    md, problems = check(a.platform, a.shards, names, a.shards_dir, may_skip(a.tests_dir))
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
    print("\nEvery shard reported, and together they ran all %d tests in both passes." % len(names))
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
