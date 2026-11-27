#!/usr/bin/env python3
"""Per-test, per-mode manifests of a regression run, and the comparison that keeps a
release gate from going green without proving its cases ran.

A count line ("Results: 299 passed, 40 skipped, 0 failed") cannot show that the same tests
ran: a new test hides one that disappeared, a pass that turned into a skip moves one number
up and another down by the same amount. The manifest names every test and its outcome.

    gate_manifest.py collect --log LOG [--log LOG...] --platform P --mode M --out FILE
        Parse the runner's log(s) (run_regression.sh or run_regression.cmd; several logs are
        the shards of one run) into FILE: {test: PASS|SKIP|FAIL} for mode M, merged into FILE
        when it already holds other modes. Fails on a log with no tests, a test that started
        and never reported, a test reported twice, no "Results:" line, or a "Results:" line
        that disagrees with the tests named.

    gate_manifest.py judge --log LOG --exit-code N --platform P --mode M --out FILE
                      [--baseline FILE] [--reviewed-additions FILE] [--allowlist FILE]
        What verify_platform.sh/.cmd call for each regression pass: one line, "PASS: <counts>" or
        "FAIL: <why>". FAIL when the runner exited non-zero, when its log has no "Results:" line
        (counts not found), when collect or check fails, or when a baseline is given and compare
        fails. The baseline is optional by design: none exists until a gate has produced one.

    gate_manifest.py check MANIFEST [--allowlist FILE]
        Fails on a SKIP that tools/cicd/skip_allowlist.json does not allow for the manifest's
        platform and mode, on an empty mode, and on FAIL entries.

    gate_manifest.py compare BASELINE CANDIDATE [--allowlist FILE] [--mode M...]
                      [--reviewed-additions FILE]
        Fails on: a platform mismatch; a mode of the baseline missing from the candidate; a
        test of the baseline missing from the candidate; an outcome that was PASS and is not;
        a SKIP the allowlist does not allow. Tests only the candidate has are PRINTED. With
        --reviewed-additions (one "test" or "mode:test" per line, "#" comments) an addition
        not listed there fails too; without it additions are reported only.

    gate_manifest.py lint [--tests-dir DIR] [--allowlist FILE]
        The allowlist against the sources: every "# SKIP_ALLOWED:" test has an entry, every
        marker-opt-out test (VERIFY_SKIP, GC_STRESS_SKIP) has one for its mode, no entry names
        a test that is gone or does not carry the declaration, every entry has a reason.

Baselines are produced by the PREVIOUS gate, never written by hand: see tools/cicd/README.md.
Exit status 0 only when nothing failed.
"""
import argparse
import fnmatch
import glob
import hashlib
import json
import os
import re
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.abspath(os.path.join(HERE, "..", ".."))
DEFAULT_ALLOWLIST = os.path.join(HERE, "skip_allowlist.json")
FORMAT = 1

RUNNING = re.compile(r"^Running: (.+)\.\.\.\s*$")
# run_regression.sh prints "  [PASS] ...", run_regression.cmd "  PASS ..."
RESULT = re.compile(r"^ {2}\[?(PASS|SKIP|FAIL)\]?(?:\s|$)(.*)$")
RESULTS_LINE = re.compile(r"^ {0,2}Results:\s*(\d+) passed,\s*(\d+) skipped,\s*(\d+) failed")


class GateError(Exception):
    pass


def parse_log(path):
    """Return ({test: (outcome, reason)}, (passed, skipped, failed)) or raise GateError."""
    if not os.path.isfile(path):
        raise GateError("%s: no such log" % path)
    tests, cur, summary = {}, None, None
    started = []
    with open(path, encoding="utf-8", errors="replace") as f:
        for raw in f:
            line = raw.rstrip("\r\n")
            m = RUNNING.match(line)
            if m:
                cur = m.group(1)
                if cur in tests or cur in started:
                    raise GateError("%s: %s ran twice" % (path, cur))
                started.append(cur)
                continue
            m = RESULTS_LINE.match(line)
            if m:
                # the last one wins: a failing test's own output may echo such a line
                summary = tuple(int(x) for x in m.groups())
                cur = None
                continue
            m = RESULT.match(line)
            if m and cur is not None and cur not in tests:
                reason = m.group(2).strip()
                tests[cur] = (m.group(1), reason)
    unfinished = [n for n in started if n not in tests]
    if unfinished:
        raise GateError("%s: started and never reported: %s" % (path, ", ".join(unfinished[:8])))
    if not tests:
        raise GateError("%s: no test results found (an empty or unrecognised log proves nothing)" % path)
    if summary is None:
        raise GateError("%s: no 'Results: N passed, N skipped, N failed' line: the suite did not run to its summary" % path)
    return tests, summary


def sha256(path):
    h = hashlib.sha256()
    with open(path, "rb") as f:
        for chunk in iter(lambda: f.read(1 << 20), b""):
            h.update(chunk)
    return h.hexdigest()


def collect(logs, platform, mode, out):
    merged, totals, sources = {}, [0, 0, 0], []
    for log in logs:
        tests, summary = parse_log(log)
        for n in tests:
            if n in merged:
                raise GateError("%s: %s is in more than one log" % (log, n))
        merged.update(tests)
        totals = [a + b for a, b in zip(totals, summary)]
        sources.append({"log": os.path.basename(log), "sha256": sha256(log)})
    tally = [sum(1 for o, _ in merged.values() if o == k) for k in ("PASS", "SKIP", "FAIL")]
    if tally != totals:
        raise GateError("the logs name %d passed, %d skipped, %d failed tests but their Results lines say %d, %d, %d"
                        % (tuple(tally) + tuple(totals)))
    manifest = {"format": FORMAT, "platform": platform, "modes": {}}
    if os.path.isfile(out):
        with open(out, encoding="utf-8") as f:
            old = json.load(f)
        if old.get("platform") != platform:
            raise GateError("%s holds platform %r, not %r" % (out, old.get("platform"), platform))
        manifest["modes"] = old.get("modes", {})
    manifest["modes"][mode] = {
        "sources": sources,
        "counts": {"passed": totals[0], "skipped": totals[1], "failed": totals[2]},
        "tests": {n: {"outcome": o, "reason": r} for n, (o, r) in sorted(merged.items())},
    }
    os.makedirs(os.path.dirname(os.path.abspath(out)), exist_ok=True)
    with open(out, "w", encoding="utf-8", newline="\n") as f:
        json.dump(manifest, f, indent=1, sort_keys=True)
        f.write("\n")
    return manifest, tally


def load_manifest(path):
    if not os.path.isfile(path):
        raise GateError("%s: no such manifest" % path)
    with open(path, encoding="utf-8") as f:
        m = json.load(f)
    if m.get("format") != FORMAT or "modes" not in m or "platform" not in m:
        raise GateError("%s: not a format-%d gate manifest" % (path, FORMAT))
    return m


def load_allowlist(path):
    with open(path, encoding="utf-8") as f:
        data = json.load(f)
    if data.get("format") != FORMAT or not isinstance(data.get("entries"), list):
        raise GateError("%s: not a format-%d skip allowlist" % (path, FORMAT))
    return data["entries"]


def allowed(entries, test, platform, mode):
    for e in entries:
        if e["test"] != test:
            continue
        if any(fnmatch.fnmatch(platform, p) for p in e["platforms"]) and any(fnmatch.fnmatch(mode, m) for m in e["modes"]):
            return e
    return None


def check_skips(manifest, entries, modes=None):
    problems = []
    for mode, body in sorted(manifest["modes"].items()):
        if modes and mode not in modes:
            continue
        if not body["tests"]:
            problems.append("mode %s has no tests" % mode)
        for n, rec in sorted(body["tests"].items()):
            if rec["outcome"] == "FAIL":
                problems.append("%s: %s FAILED" % (mode, n))
            elif rec["outcome"] == "SKIP" and not allowed(entries, n, manifest["platform"], mode):
                problems.append("%s: %s was skipped and %s does not allow that on %s/%s (reason given: %s)"
                                % (mode, n, os.path.basename(DEFAULT_ALLOWLIST), manifest["platform"], mode, rec.get("reason") or "none"))
    return problems


def read_reviewed(path):
    out = set()
    with open(path, encoding="utf-8") as f:
        for line in f:
            line = line.split("#", 1)[0].strip()
            if line:
                out.add(line)
    return out


def compare(base, cand, entries, modes=None, reviewed=None):
    problems, additions = [], []
    if base["platform"] != cand["platform"]:
        problems.append("platform differs: baseline %s, candidate %s" % (base["platform"], cand["platform"]))
    for mode, bbody in sorted(base["modes"].items()):
        if modes and mode not in modes:
            continue
        cbody = cand["modes"].get(mode)
        if cbody is None:
            problems.append("mode %s is in the baseline and not in the candidate" % mode)
            continue
        if not cbody["tests"]:
            problems.append("mode %s: the candidate names no tests" % mode)
            continue
        for n, brec in sorted(bbody["tests"].items()):
            crec = cbody["tests"].get(n)
            if crec is None:
                problems.append("%s: %s is in the baseline and missing from the candidate" % (mode, n))
            elif brec["outcome"] == "PASS" and crec["outcome"] != "PASS":
                problems.append("%s: %s was PASS and is %s" % (mode, n, crec["outcome"]))
        for n in sorted(set(cbody["tests"]) - set(bbody["tests"])):
            additions.append((mode, n))
    for mode, body in sorted(cand["modes"].items()):
        if modes and mode not in modes:
            continue
        if mode in base["modes"]:
            problems += check_skips({"platform": cand["platform"], "modes": {mode: body}}, entries)
    if reviewed is not None:
        for mode, n in additions:
            if n not in reviewed and "%s:%s" % (mode, n) not in reviewed:
                problems.append("%s: %s is new and not in the reviewed-additions file" % (mode, n))
    return problems, additions


def judge(log, exit_code, platform, mode, out, baseline, reviewed_path, entries):
    """(ok, one line) for one regression pass."""
    try:
        _, summary = parse_log(log)
    except GateError as e:
        return False, "counts not found: %s" % e
    counts = "%d passed, %d skipped, %d failed" % summary
    if exit_code != 0:
        return False, "exit %d; %s" % (exit_code, counts)
    try:
        manifest, _ = collect([log], platform, mode, out)
    except GateError as e:
        return False, "%s; manifest: %s" % (counts, e)
    bad = check_skips(manifest, entries, [mode])
    if bad:
        return False, "%s; manifest: %s" % (counts, "; ".join(bad[:3]))
    if baseline and os.path.isfile(baseline):
        reviewed = read_reviewed(reviewed_path) if reviewed_path and os.path.isfile(reviewed_path) else None
        problems, _ = compare(load_manifest(baseline), manifest, entries, [mode], reviewed)
        if problems:
            return False, "%s; against the baseline: %s" % (counts, "; ".join(problems[:3]))
        return True, counts + "; agrees with the baseline manifest"
    return True, counts + "; no baseline manifest, checked for skips and counts only"


def lint(tests_dir, entries):
    problems = []
    declared, verify_skip, stress_skip = {}, set(), set()
    for path in sorted(glob.glob(os.path.join(tests_dir, "*.obs"))):
        name = os.path.splitext(os.path.basename(path))[0]
        with open(path, encoding="utf-8-sig", errors="replace") as f:
            lines = [l.rstrip("\r\n") for l in f]
        for l in lines[:20]:
            m = re.match(r"^#[ \t]*SKIP_ALLOWED:[ \t]*(\S.*?)[ \t]*$", l)
            if m:
                declared[name] = m.group(1)
                break
        if "# VERIFY_SKIP" in lines:
            verify_skip.add(name)
        if "# GC_STRESS_SKIP" in lines:
            stress_skip.add(name)
    if not declared and not verify_skip and not stress_skip and not glob.glob(os.path.join(tests_dir, "*.obs")):
        problems.append("no *.obs under %s" % tests_dir)
    by_test = {}
    for i, e in enumerate(entries):
        for key in ("test", "platforms", "modes", "reason", "class"):
            if not e.get(key):
                problems.append("entry %d (%s) has no %s" % (i, e.get("test", "?"), key))
        if e.get("class") not in ("skip_allowed", "verify_skip", "gc_stress_skip"):
            problems.append("entry %d (%s): class %r is not skip_allowed, verify_skip or gc_stress_skip" % (i, e.get("test"), e.get("class")))
        by_test.setdefault((e.get("test"), e.get("class")), []).append(e)
    for n in sorted(declared):
        if (n, "skip_allowed") not in by_test:
            problems.append("%s declares '# SKIP_ALLOWED:' and has no skip_allowed entry in the allowlist" % n)
    for n in sorted(verify_skip):
        if (n, "verify_skip") not in by_test:
            problems.append("%s carries '# VERIFY_SKIP' and has no verify_skip entry" % n)
    for n in sorted(stress_skip):
        if (n, "gc_stress_skip") not in by_test:
            problems.append("%s carries '# GC_STRESS_SKIP' and has no gc_stress_skip entry" % n)
    source = {"skip_allowed": declared, "verify_skip": verify_skip, "gc_stress_skip": stress_skip}
    for (n, cls), _ in sorted(by_test.items(), key=lambda kv: (str(kv[0][0]), str(kv[0][1]))):
        if cls in source and n not in source[cls]:
            problems.append("allowlist entry %s (%s) names a test that does not carry that declaration" % (n, cls))
    return problems


def main(argv=None):
    ap = argparse.ArgumentParser(description=__doc__.split("\n")[0])
    sub = ap.add_subparsers(dest="cmd", required=True)
    c = sub.add_parser("collect")
    c.add_argument("--log", action="append", required=True)
    c.add_argument("--platform", required=True)
    c.add_argument("--mode", required=True)
    c.add_argument("--out", required=True)
    j = sub.add_parser("judge")
    j.add_argument("--log", required=True)
    j.add_argument("--exit-code", type=int, required=True)
    j.add_argument("--platform", required=True)
    j.add_argument("--mode", required=True)
    j.add_argument("--out", required=True)
    j.add_argument("--baseline")
    j.add_argument("--reviewed-additions")
    j.add_argument("--allowlist", default=DEFAULT_ALLOWLIST)
    k = sub.add_parser("check")
    k.add_argument("manifest")
    k.add_argument("--allowlist", default=DEFAULT_ALLOWLIST)
    k.add_argument("--mode", action="append")
    p = sub.add_parser("compare")
    p.add_argument("baseline")
    p.add_argument("candidate")
    p.add_argument("--allowlist", default=DEFAULT_ALLOWLIST)
    p.add_argument("--mode", action="append")
    p.add_argument("--reviewed-additions")
    l = sub.add_parser("lint")
    l.add_argument("--tests-dir", default=os.path.join(ROOT, "programs", "regression"))
    l.add_argument("--allowlist", default=DEFAULT_ALLOWLIST)
    a = ap.parse_args(argv)

    try:
        if a.cmd == "collect":
            _, tally = collect(a.log, a.platform, a.mode, a.out)
            print("manifest %s [%s/%s]: %d passed, %d skipped, %d failed (%d tests)" % ((a.out, a.platform, a.mode) + tuple(tally) + (sum(tally),)))
            return 0
        if a.cmd == "lint":
            problems = lint(a.tests_dir, load_allowlist(a.allowlist))
            for x in problems:
                print("  - " + x)
            if problems:
                return 1
            print("skip allowlist agrees with the %s sources" % a.tests_dir)
            return 0
        entries = load_allowlist(a.allowlist)
        if a.cmd == "judge":
            ok, line = judge(a.log, a.exit_code, a.platform, a.mode, a.out, a.baseline, a.reviewed_additions, entries)
            print(("PASS: " if ok else "FAIL: ") + line)
            return 0 if ok else 1
        if a.cmd == "check":
            problems = check_skips(load_manifest(a.manifest), entries, a.mode)
            for x in problems:
                print("  - " + x)
            if not problems:
                print("every skip in %s is allowed" % a.manifest)
            return 1 if problems else 0
        reviewed = read_reviewed(a.reviewed_additions) if a.reviewed_additions else None
        problems, additions = compare(load_manifest(a.baseline), load_manifest(a.candidate), entries, a.mode, reviewed)
        if additions:
            print("tests new in the candidate (%s):" % ("checked against the reviewed-additions file" if reviewed is not None
                                                        else "report only: no --reviewed-additions given"))
            for mode, n in additions:
                print("  + %s: %s" % (mode, n))
        for x in problems:
            print("  - " + x)
        if problems:
            return 1
        print("candidate agrees with the baseline: no test missing, no PASS lost, every skip allowed")
        return 0
    except GateError as e:
        print("gate_manifest: %s" % e)
        return 1


if __name__ == "__main__":
    sys.exit(main())
