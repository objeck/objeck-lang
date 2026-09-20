#!/usr/bin/env python3
"""
obi must exit when stdin reaches EOF.

Usage: check_obi_stdin_eof.py <bin_dir>

The REPL prompt read a line without checking whether the read succeeded. At EOF
-- a closed stdin, an exhausted pipe, Ctrl-D, Ctrl-Z -- getline fails and leaves
the string empty, so the loop printed its prompt and read again, forever, at full
CPU (#917). It is how a diagnostic job that ran obi with stdin closed hung until
its timeout rather than failing.

Each invocation here gets a closed stdin and must exit on its own:
  obi            -- straight to the prompt
  obi --quit     -- no program to run, so it also reaches the prompt

A timeout is a failure, and so is dying from a signal: this checks that obi
leaves on its own terms.
"""

import os
import subprocess
import sys

EXE = ".exe" if os.name == "nt" else ""
TIMEOUT = 60


def fail(msg, detail=""):
    print("FAIL: " + msg)
    if detail:
        for line in detail.rstrip().splitlines()[-10:]:
            print("    | " + line)
    sys.exit(1)


def run(obi, args, env):
    cmd = [obi] + args
    try:
        p = subprocess.run(cmd, env=env, stdin=subprocess.DEVNULL,
                           stdout=subprocess.PIPE, stderr=subprocess.STDOUT,
                           timeout=TIMEOUT)
    except subprocess.TimeoutExpired as e:
        out = (e.stdout or b"").decode("utf-8", "replace")
        fail("obi %s did not exit within %ds with stdin at EOF (#917)"
             % (" ".join(args) or "(no arguments)", TIMEOUT), out)
    out = p.stdout.decode("utf-8", "replace")
    if p.returncode < 0:
        fail("obi %s died from signal %d at EOF" % (" ".join(args) or "(no arguments)", -p.returncode), out)
    print("  obi %-14s exited %d" % (" ".join(args) or "(no arguments)", p.returncode))


def main():
    if len(sys.argv) < 2:
        print(__doc__)
        sys.exit(2)

    bin_dir = os.path.abspath(sys.argv[1])
    obi = os.path.join(bin_dir, "obi" + EXE)
    if not os.path.isfile(obi):
        fail("missing " + obi)

    env = dict(os.environ)
    env["OBJECK_LIB_PATH"] = os.path.join(os.path.dirname(bin_dir), "lib") + os.sep

    run(obi, [], env)
    run(obi, ["--quit"], env)
    print("PASS: obi exits at stdin EOF")


if __name__ == "__main__":
    main()
