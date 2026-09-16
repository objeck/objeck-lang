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

A second shape defeats a presence check. `odbc_sqlite_test` ended in a
`Runtime->Exit(1)`, so it satisfied the rule above, but the branch that found no
database printed `FAIL: Cannot open connection` and then `return`-ed out of Main.
On every machine without the SQLite ODBC data source -- macOS, both Windows legs,
most developer machines -- it exited 0 having run no check at all, and both
runners scored it a PASS. That went unseen until 2026-09-13.

So a FAIL print whose next statement is a bare `return;` is an offense too. Unlike
reachability, that one is decidable from the text: `return <expr>;` hands a result
to a caller that can still act on it, but a bare return from a `~ Nil` method
reports nothing, and from Main it is exit 0. Either exit non-zero, when the branch
really is a failure, or -- when it is an environment gap the suite should record
rather than pass -- print a line starting with `SKIP:` and return. Both runners
match `SKIP:` at the start of a line and count the test as skipped.

Tests marked `# EXPECT_RUNTIME_ERROR` are exempt from that second rule: the runner
passes them on a non-zero exit and fails them on exit 0, so returning normally
after printing FAIL is how one of them reports a failed check.

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

# A negative test is scored the other way round -- the runner passes it on a
# non-zero exit and fails it on exit 0 -- so returning after a FAIL print is how
# it reports a failed check, and the bare-return rule does not apply.
RUNTIME_ERROR_LINE = re.compile(r"^# EXPECT_RUNTIME_ERROR", re.M)

# How a test prints. A FAIL marker outside one of these is not a report.
PRINT_CALLS = ("->PrintLine(", "->Print(")

# `return;` with no value: from a `~ Nil` method it tells a caller nothing, and
# from Main it is exit 0, which the runners score as a pass.
BARE_RETURN = re.compile(r"^return\s*;")

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


def fail_then_return_sites(source):
    """Return (line number, text) for each FAIL print whose next statement returns.

    Block comments (`#~ ... ~#`) and line comments are skipped, so a print inside
    one does not count, and a comment written between the print and the return
    does not hide it. The return may share the print's line or follow it.
    """
    code = []
    in_block = False
    for number, raw in enumerate(source.splitlines(), 1):
        text = raw.strip()
        if in_block:
            if "~#" in text:
                in_block = False
            continue
        if text.startswith("#~"):
            if "~#" not in text[2:]:
                in_block = True
            continue
        if text and not text.startswith("#"):
            code.append((number, text))

    sites = []
    for index, (number, text) in enumerate(code):
        if not any(marker in text for marker in FAIL_MARKERS):
            continue
        if not any(call in text for call in PRINT_CALLS):
            continue
        rest = text.split(";", 1)[1].strip() if ";" in text else ""
        follows = index + 1 < len(code) and BARE_RETURN.match(code[index + 1][1])
        if BARE_RETURN.match(rest) or (not rest and follows):
            sites.append((number, text))
    return sites


def repo_root():
    return os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))


def main():
    tests_dir = os.path.join(repo_root(), "programs", "regression")
    if not os.path.isdir(tests_dir):
        sys.stderr.write("not found: %s\n" % tests_dir)
        return 1

    offenders = []
    silent_returns = []
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
        if not RUNTIME_ERROR_LINE.search(source):
            for number, text in fail_then_return_sites(source):
                silent_returns.append(("programs/regression/%s" % name, number, text))

    if bad_markers:
        sys.stderr.write("%d regression test marker problem(s):\n\n" % len(bad_markers))
        for problem in bad_markers:
            sys.stderr.write("    %s\n" % problem)
        sys.stderr.write("\n")

    if silent_returns:
        sys.stderr.write(
            "%d branch(es) print a failure and then return, which exits 0.\n"
            "The runners score the exit code, so the failure is never reported:\n\n"
            % len(silent_returns)
        )
        for where, number, text in silent_returns:
            sys.stderr.write("    %s:%d: %s\n" % (where, number, text))
        sys.stderr.write(
            "\nExit non-zero when the branch is a failure:\n\n"
            "    Runtime->Exit(1);\n\n"
            "When it is instead an environment gap -- a driver, data source or\n"
            "service the machine does not have -- say so at the start of a line,\n"
            "and the runners record a skip rather than a pass:\n\n"
            '    "SKIP: no objeck_sqlite_test data source"->PrintLine();\n'
            "    return;\n\n"
        )

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

    if bad_markers or silent_returns or offenders:
        return 1

    print("All %d asserting regression tests have a non-zero exit path, and none\n"
          "returns from a branch that printed a failure." % checked)
    return 0


if __name__ == "__main__":
    sys.exit(main())
