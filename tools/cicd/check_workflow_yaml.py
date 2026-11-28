#!/usr/bin/env python3
"""Every workflow under .github/workflows must be valid YAML.

GitHub refuses to start a run of an invalid workflow file and reports only
"This run likely failed because of a workflow file issue", with no job and no log.
That happened on master on 2026-11-27: a step name that ended in "SKIP:" is a
mapping key to a YAML parser, so every push-triggered CI run since the merge that
added it never started, and nothing in the repository's own checks noticed.

This check parses each file. It uses PyYAML when it is installed (the CI runners
have it); without PyYAML it scans for the mistake that caused the incident, an
unquoted "name:" value that holds ": " or ends in ":". --self-test shows both
catch a broken sample, so the check cannot pass by looking at nothing.

Usage: check_workflow_yaml.py [--self-test] [workflows_dir]
"""
import glob
import os
import re
import sys

ROOT = os.path.abspath(os.path.join(os.path.dirname(__file__), "..", ".."))
DEFAULT_DIR = os.path.join(ROOT, ".github", "workflows")

# "- name: text: more" or "name: text:" with the value unquoted
PLAIN_NAME = re.compile(r"^\s*(?:-\s+)?name:\s+(?![\"'|>])(?P<value>.*\S)\s*$")


def scan_text(text):
    """Find unquoted name values a YAML parser reads as a nested mapping."""
    problems = []
    for number, line in enumerate(text.splitlines(), 1):
        match = PLAIN_NAME.match(line)
        if not match:
            continue
        value = match.group("value")
        # a comment after the value does not count
        value = re.split(r"\s+#", value, 1)[0]
        if value.endswith(":") or ": " in value:
            problems.append((number, "unquoted name holds ': ' or ends in ':': " + value))
    return problems


def parse_file(path):
    """Return a list of (line, message) problems for one workflow file."""
    with open(path, "rb") as handle:
        text = handle.read().decode("utf-8")
    try:
        import yaml
    except ImportError:
        return scan_text(text)
    try:
        document = yaml.safe_load(text)
    except yaml.YAMLError as error:
        mark = getattr(error, "problem_mark", None)
        line = mark.line + 1 if mark is not None else 0
        return [(line, "not valid YAML: " + str(getattr(error, "problem", error)))]
    if not isinstance(document, dict) or "jobs" not in document:
        return [(0, "no 'jobs' mapping")]
    return []


def check_directory(directory):
    files = sorted(glob.glob(os.path.join(directory, "*.yml")) + glob.glob(os.path.join(directory, "*.yaml")))
    if not files:
        print("FAIL: no workflow files under " + directory)
        return 1
    failed = 0
    for path in files:
        for line, message in parse_file(path):
            print("FAIL: %s:%d: %s" % (os.path.relpath(path, ROOT), line, message))
            failed += 1
    if failed == 0:
        print("ok: %d workflow files parse" % len(files))
    return 1 if failed else 0


def self_test():
    bad = "jobs:\n  a:\n    steps:\n      - name: tests that print SKIP:\n        run: true\n"
    good = "jobs:\n  a:\n    steps:\n      - name: \"tests that print SKIP:\"\n        run: true\n"
    assert scan_text(bad), "the scanner missed a name that ends in a colon"
    assert not scan_text(good), "the scanner refused a quoted name"
    assert scan_text("      - name: a: b\n"), "the scanner missed ': ' inside a name"
    try:
        import yaml
    except ImportError:
        print("self-test ok (scanner only: PyYAML not installed)")
        return 0
    try:
        yaml.safe_load(bad)
    except yaml.YAMLError:
        pass
    else:
        raise AssertionError("PyYAML accepted the broken sample")
    yaml.safe_load(good)
    print("self-test ok")
    return 0


def main(argv):
    if "--self-test" in argv:
        return self_test()
    paths = [a for a in argv if not a.startswith("-")]
    return check_directory(paths[0] if paths else DEFAULT_DIR)


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
