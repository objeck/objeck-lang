#!/usr/bin/env python3
"""Fixture tests for programs/deploy/build_examples.py's output check (--run).

The check compares what an example prints with the 'output:' block at the top of its source. It
must not be lenient in ways that hide a broken example, so these pin the cases a review found:

- an example that prints nothing (or has no block) fails, unless it says '# output: none';
- output that varies is compared too, with placeholders for the varying fields only;
- the '...' line wildcard keeps every way of matching open until the later lines decide.

No toolchain needed:  python3 tools/cicd/test_build_examples.py
"""
import importlib.util
import os
import sys
import tempfile

ROOT = os.path.abspath(os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", ".."))
spec = importlib.util.spec_from_file_location("build_examples", os.path.join(ROOT, "programs", "deploy", "build_examples.py"))
be = importlib.util.module_from_spec(spec)
spec.loader.exec_module(be)

failures = []


def check(name, condition):
    if not condition:
        failures.append(name)
        print("FAIL: " + name)


def header(*lines, heading="# output:"):
    body = "#~\n# compile: x\n#\n" + heading + "\n" + "".join("#   %s\n" % l for l in lines) + "~#\nclass X {}\n"
    handle, path = tempfile.mkstemp(suffix=".obs")
    with os.fdopen(handle, "w", encoding="utf-8") as f:
        f.write(body)
    return path


def block(*lines, heading="# output:"):
    path = header(*lines, heading=heading)
    try:
        return be.expected_output(path)
    finally:
        os.remove(path)


# ---- the '...' wildcard (the reviewer's case: the first fitting line is not always the right one)
m = be.matches
check("skip a line that also fits", m(["start", "...", "item ...", "end"], ["start", "item ignored", "item wanted", "end"]))
check("skip finds the later fit", m(["a", "...", "c"], ["a", "b", "c"]))
check("zero lines skipped", m(["a", "...", "b"], ["a", "b"]))
check("adjacent ... collapse", m(["a", "...", "...", "d"], ["a", "b", "c", "d"]))
check("... at the start", m(["...", "z"], ["x", "y", "z"]))
check("... at the end", m(["a", "..."], ["a", "b", "c"]))
check("... alone matches anything", m(["..."], ["x", "y"]))
check("no ... means exact", not m(["a", "c"], ["a", "b", "c"]))
check("extra lines after the last fail", not m(["a", "b"], ["a", "b", "c"]))
check("missing lines fail", not m(["a", "b", "c"], ["a", "b"]))
check("... cannot supply a missing later line", not m(["a", "...", "z"], ["a", "b", "c"]))
check("abridged text inside a line", m(["GET /x -> 200 {\"t\":\"Wed..."], ["GET /x -> 200 {\"t\":\"Wednesday, Nov 2\" ..."]))
check("abridged middle of a line", m(["expected SELECT, ... found 'x'"], ["expected SELECT, INSERT, UPDATE, found 'x'"]))
check("trailing blank lines ignored", m(["a"], ["a", "", " "]))
check("the wrong line still fails", not m(["start", "...", "item wanted", "end"], ["start", "item ignored", "end"]))

if failures:
    print("%d failed" % len(failures))
    sys.exit(1)
print("build_examples output check: all fixtures pass")
