#!/usr/bin/env python3
"""Assert that every regression test which can report a failure can also fail.

`run_regression.cmd` and `run_regression.sh` decide PASS/FAIL from the test
process's **exit code** and nothing else. A test that prints `FAIL: ...` and then
returns normally from Main exits 0, and the runner scores it a PASS.

That hole was not theoretical. When this script was written, 125 of the 168
regression tests that print "FAIL" had no non-zero exit path at all -- 74% of the
assertion-carrying suite could fail every check it made and still be counted
green.

Two of them were failing on the day it was found. A full x64 run reported

    Results: 195 passed, 3 skipped, 2 failed

while `results/core_http_server_output.txt` held four `FAIL:` lines and
`results/core_net_buffer_output.txt` held three. Both were scored PASS. The real
result was 193/3/4, and every one of the four failures was the same open
in-process transport defect. It had looked confined to `mcp_debug_test` for
weeks, purely because `mcp_debug_test` was one of the 43 tests permitted to say so.

A count is not enough to catch this and neither is reading the summary line: the
suite is loudest exactly when it is wrong, because the silent tests inflate the
"passed" number they should have decremented.

What this checks is presence, not reachability: a test that prints FAIL must
contain `Runtime->Exit(` or `System->Exit(` somewhere. A test could still put the
exit on an unreachable branch and defeat it. That is deliberate -- proving
reachability needs the compiler, and the failure mode this actually guards
against is a new test written from an old one as a template, which is how all 125
got there.

Run from anywhere:  python3 tools/cicd/check_test_exit_codes.py
Exit 0 when every asserting test can fail, 1 otherwise.
"""
import os
import re
import sys

# Printed by a test to report a failed check. Any of these means the test makes
# assertions, and therefore needs a way to tell the runner about them.
FAIL_MARKERS = ("FAIL", "Failed:")

# A non-zero exit path. Both spellings appear in the tree.
EXIT_CALLS = ("Runtime->Exit(", "System->Exit(")

# Compile-error tests are checked by the runner on the compiler's exit code and
# output and never execute, so they have nothing to exit with. The runners honor
# the marker only at the start of a line, so that is all that exempts a test.
COMPILE_ERROR_MARKER = "# EXPECT_COMPILE_ERROR"
COMPILE_ERROR_LINE = re.compile(r"^# EXPECT_COMPILE_ERROR", re.M)

# Characters run_regression.cmd cannot carry through `set` and `findstr /C:`.
UNSAFE_MESSAGE_CHARS = ('"', "!", "\\")


def marker_problems(source):
    """Return what is wrong with a test's EXPECT_* marker lines, as strings.

    '# EXPECT_COMPILE_ERROR: <message>' must name a message both runners can
    match identically, and a marker behind a UTF-8 byte-order mark is invisible
    to the runners (they match at column 0), so the test would run as positive.
    """
    problems = []
    if re.match("﻿# EXPECT_(COMPILE|RUNTIME)_ERROR", source):
        problems.append("marker follows a UTF-8 byte-order mark; the runners will not see it")
    for line in source.splitlines():
        if not line.startswith(COMPILE_ERROR_MARKER):
            continue
        rest = line[len(COMPILE_ERROR_MARKER):]
        if rest == "":
            continue
        if not rest.startswith(":"):
            problems.append("unrecognized marker %r; write '# EXPECT_COMPILE_ERROR: <message>'" % line)
            continue
        message = rest[1:].lstrip(" \t")
        if not message.strip():
            problems.append("'# EXPECT_COMPILE_ERROR:' names no message")
        elif message != message.rstrip():
            problems.append("expected message %r has trailing blanks" % message)
        elif any(c in message for c in UNSAFE_MESSAGE_CHARS):
            problems.append("expected message %r contains one of %s, which run_regression.cmd cannot match"
                            % (message, " ".join(UNSAFE_MESSAGE_CHARS)))
    return problems


def repo_root():
    return os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))


def main():
    tests_dir = os.path.join(repo_root(), "programs", "regression")
    if not os.path.isdir(tests_dir):
        sys.stderr.write("not found: %s\n" % tests_dir)
        return 1

    offenders = []
    bad_markers = []
    checked = 0
    for name in sorted(os.listdir(tests_dir)):
        if not name.endswith(".obs"):
            continue
        path = os.path.join(tests_dir, name)
        with open(path, encoding="utf-8", errors="replace", newline="") as handle:
            source = handle.read()

        for problem in marker_problems(source):
            bad_markers.append("programs/regression/%s: %s" % (name, problem))

        if COMPILE_ERROR_LINE.search(source):
            continue
        if not any(marker in source for marker in FAIL_MARKERS):
            continue

        checked += 1
        if not any(call in source for call in EXIT_CALLS):
            offenders.append(name)

    if bad_markers:
        sys.stderr.write("%d regression test marker problem(s):\n\n" % len(bad_markers))
        for problem in bad_markers:
            sys.stderr.write("    %s\n" % problem)
        sys.stderr.write("\n")
        if not offenders:
            return 1

    if offenders:
        sys.stderr.write(
            "%d of %d asserting regression tests cannot report a failure.\n"
            "They print FAIL and exit 0, so the runner scores them PASS:\n\n"
            % (len(offenders), checked)
        )
        for name in offenders:
            sys.stderr.write("    programs/regression/%s\n" % name)
        sys.stderr.write(
            "\nEnd the test by counting failures and exiting non-zero, e.g.\n\n"
            "    if(failed > 0) {\n"
            '        "{$passed} passed, {$failed} failed"->PrintLine();\n'
            "        Runtime->Exit(1);\n"
            "    };\n"
        )
        return 1

    print("All %d asserting regression tests have a non-zero exit path." % checked)
    return 0


if __name__ == "__main__":
    sys.exit(main())
