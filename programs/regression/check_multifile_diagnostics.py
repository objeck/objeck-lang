#!/usr/bin/env python3
"""
A build of several files reports each file's errors together, in line order.

Usage: check_multifile_diagnostics.py <bin_dir>

<bin_dir> must contain obc (with .exe on Windows); the libraries are read from
the sibling lib/ directory.

DiagnosticSink used to key what it collected by line number alone. With two
files that fail on the same line numbers the report interleaved them
(a.obs:3, b.obs:3, a.obs:7, b.obs:7), and where the output is capped (the parser
prints eight) the cap cut one file's errors in among another's. It keys by the
file as well now, in the order the files were given, so each file's errors read
as one run.

Three cases, each with two files whose errors sit on the same lines:

  analysis   an undefined variable on lines 5 and 9 of both files
  syntax     a missing ')' on line 5 of both files, and the error that follows it
             on line 8 (the next method's header, read from the broken state)
  recovery   the first file has a syntax error; the second begins with a stray
             token. The parser's "what follows is a follow-on" state belongs to
             one file: it used to carry into the next, which then said nothing
             about its own broken top level.

Each must exit non-zero, name only the expected positions, in the expected order.
"""

import os
import re
import subprocess
import sys
import tempfile

EXE = ".exe" if os.name == "nt" else ""

# The first file also holds Main, so no error is about a missing entry point; the second file
# has a comment on that line, so the two files' errors fall on the same line numbers.
MAIN = "\tfunction : Main(args : String[]) ~ Nil {}\n"
NO_MAIN = "\t# no entry point here\n"

ANALYSIS = """class {name} {{

{main}\tfunction : First() ~ Nil {{
\t\tx := {name}_undefined_one;
\t}}

\tfunction : Second() ~ Nil {{
\t\ty := {name}_undefined_two;
\t}}
}}
"""

SYNTAX = """class {name} {{

{main}\tfunction : First() ~ Nil {{
\t\tx := (1 + 2;
\t}}

\tfunction : Second() ~ Nil {{
\t\ty := (3 + 4;
\t}}
}}
"""

DIAGNOSTIC = re.compile(r"^(.*?[/\\])?([^/\\:]+\.obs):\((\d+),(\d+)\): (.*)$")


def compile_files(obc, env, work, files):
    proc = subprocess.run([obc, "-src", ",".join(files), "-dest", os.path.join(work, "out.obe")],
                          cwd=work, env=env, capture_output=True, timeout=60)
    text = (proc.stdout + proc.stderr).decode("utf-8", "replace")
    found = []
    for line in text.splitlines():
        match = DIAGNOSTIC.match(line.strip())
        if match and "Warning:" not in match.group(5):
            place = (match.group(2), int(match.group(3)))
            # one position can carry several errors; what is ordered is where they are
            if not found or found[-1] != place:
                found.append(place)
    return proc.returncode, found, text


def main():
    if len(sys.argv) != 2:
        print(__doc__)
        return 2
    bin_dir = os.path.abspath(sys.argv[1])
    obc = os.path.join(bin_dir, "obc" + EXE)
    lib_dir = os.path.join(os.path.dirname(bin_dir), "lib")
    if not os.path.isfile(obc):
        print("missing: %s" % obc)
        return 2

    env = dict(os.environ)
    env["OBJECK_LIB_PATH"] = lib_dir

    failures = []

    def check(name, cond, detail=""):
        print("  [%s] %s%s" % ("PASS" if cond else "FAIL", name, "" if cond else "\n         " + detail))
        if not cond:
            failures.append(name)

    with tempfile.TemporaryDirectory(prefix="multifile-diag-") as work:
        def write(name, text):
            path = os.path.join(work, name)
            with open(path, "w") as handle:
                handle.write(text)
            return path

        for label, template, lines in (("analysis", ANALYSIS, (5, 9)), ("syntax", SYNTAX, (5, 8))):
            a = write("%s_a.obs" % label, template.format(name="Alpha", main=MAIN))
            b = write("%s_b.obs" % label, template.format(name="Beta", main=NO_MAIN))
            names = ("%s_a.obs" % label, "%s_b.obs" % label)

            code, found, text = compile_files(obc, env, work, [a, b])
            expected = [(names[0], lines[0]), (names[0], lines[1]), (names[1], lines[0]), (names[1], lines[1])]
            check("%s: refused" % label, code != 0, "exit=%d" % code)
            check("%s: each file's errors read as one run, in line order" % label, found == expected,
                  "got %r\n         want %r\n%s" % (found, expected, text))

            # the same two files the other way round: the order given is the order printed
            code, found, text = compile_files(obc, env, work, [b, a])
            expected = [(names[1], lines[0]), (names[1], lines[1]), (names[0], lines[0]), (names[0], lines[1])]
            check("%s: files are reported in the order given" % label, found == expected,
                  "got %r\n         want %r\n%s" % (found, expected, text))

        # one file's recovery state must not silence the next file's top level
        a = write("recovery_a.obs", SYNTAX.format(name="Alpha", main=MAIN))
        b = write("recovery_b.obs", "stray tokens here\nclass Beta {\n\tfunction : Run() ~ Nil {\n\t}\n}\n")
        code, found, text = compile_files(obc, env, work, [a, b])
        in_b = [place for place in found if place[0] == "recovery_b.obs"]
        check("recovery: refused", code != 0, "exit=%d" % code)
        check("recovery: the second file's own top-level error is reported", in_b == [("recovery_b.obs", 1)],
              "got %r\n%s" % (in_b, text))

    print("\n%d failure(s)" % len(failures))
    return 1 if failures else 0


if __name__ == "__main__":
    sys.exit(main())
