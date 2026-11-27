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

# ---- placeholders match only their own field
check("<int> matches digits", m(["port <int> open"], ["port 4821 open"]))
check("<int> refuses text", not m(["port <int> open"], ["port abc open"]))
check("<any> matches one field", m(["wrote <any> bytes"], ["wrote /tmp/x.dat bytes"]))
check("<any> is not empty", not m(["wrote <any> bytes"], ["wrote bytes"]))
check("text around a placeholder is exact", not m(["GET /missing -> <int>"], ["GET /other -> 404"]))
check("a fixed word beside a placeholder", not m(["GET /missing -> <int>"], ["GET /missing -> 404 extra"]))
check("a placeholder does not span lines", not m(["a <any> b"], ["a x", "y b"]))

# ---- what a header must say
check("a block is read", block("one", "two") == ["one", "two"])
check("no block is None", be.expected_output(__file__) is None)
check("output: none is silent", block(heading="# output: none") == [])
check("a heading with no lines is None", block() is None)
check("a variable heading is still compared", block("port <int>", heading="# output (the port varies):") == ["port <int>"])

ok, note = be.judge(None, 0, ["hello"])
check("a runnable example with no block fails", not ok and "output:" in note)
ok, note = be.judge([], 0, [])
check("a silent example that is silent passes", ok)
ok, note = be.judge([], 0, ["oops"])
check("an example that says 'none' but prints fails", not ok)
ok, note = be.judge(["server listening on port <int>", "GET /missing -> 404", "server stopped"], 0, [])
check("an example that prints nothing fails", not ok)
ok, note = be.judge(["server listening on port <int>", "GET /missing -> 404", "server stopped"], 0,
                    ["server listening on port 4821", "GET /missing -> 404", "server stopped"])
check("variable output passes", ok)
ok, note = be.judge(["server listening on port <int>", "GET /missing -> 404", "server stopped"], 0,
                    ["server listening on port 4821", "GET /missing -> 500", "server stopped"])
check("altered fixed text in a variable example fails", not ok and "404" in note)
ok, note = be.judge(["a"], 1, ["a"])
check("a non-zero exit fails", not ok)

if failures:
    print("%d failed" % len(failures))
    sys.exit(1)
print("build_examples output check: all fixtures pass")
