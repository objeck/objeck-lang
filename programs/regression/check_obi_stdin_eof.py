#!/usr/bin/env python3
"""
obi must exit, cleanly, when stdin reaches EOF.

Usage: check_obi_stdin_eof.py <bin_dir>

<bin_dir> must contain obi (with .exe on Windows); the libraries are read from
the sibling lib/ directory, as check_obi_obr_parity.py does. obi compiles in
process, so no other tool is needed.

Every prompt in the REPL read a line without checking whether the read succeeded.
getline empties its string when it fails, so at end of input -- a closed stdin, an
exhausted pipe, Ctrl-D, Ctrl-Z -- a prompt could not tell a blank line from input
that would never arrive. The two loops printed their prompt and read again,
forever, at full CPU: measured on the shipped 2026.9.1 obi, '/m' emits 1.9 MB of
"Insert '/m' to exit] " in 60 s and has to be killed (#917). That is how the
arm64 diagnostic jobs that ran obi with stdin closed hung until their timeout
rather than failing. The one-shot prompts acted on the empty string instead:
'/a' threw the current arguments away, '/r' swapped the line for a blank one.

Each case hands obi input that ends, and obi must leave on its own with status 0:

  no arguments         straight to the prompt loop           -- the #917 hang
  --quit               no program to run, so it just exits   -- was inert, and
                                                                fell into that loop
  --file x.obs --quit  runs the program, then exits          -- the normal path
  /a /i /m /r /u /p    a command, then EOF at its sub-prompt -- the other reads

A timeout fails, and so does death by signal or any non-zero status: obi has to
leave on its own terms. Two of the commands ('/a', '/m') recompile the buffer on
their way out, so they cover the compile-and-exit path as well.
"""

import os
import subprocess
import sys
import tempfile

EXE = ".exe" if os.name == "nt" else ""

# Generous: obi should exit in milliseconds, and the cases that recompile the
# buffer in a few seconds. Nothing here measures timing -- the bug is unbounded,
# so any bound catches it. The first timeout ends the check, so this is also the
# worst case for the whole run on a slow runner.
TIMEOUT = 120

PROGRAM = """class Eof {
  function : Main(args : String[]) ~ Nil {
    "ran-the-program"->PrintLine();
  }
}
"""


# (label, argv tail, stdin text -- None closes stdin outright)
def cases(src):
    return [
        ("prompt at EOF", [], None),
        ("--quit, nothing to run", ["--quit"], None),
        ("--file --quit", ["--file", src, "--quit"], None),
        ("/a  command arguments", [], "/a\n"),
        ("/i  insert below", [], "/i\n"),
        ("/m  multi-line insert", [], "/m\n"),
        # line 1 of the shell buffer, the one writable line '/r' will prompt for
        ("/r  replace line", [], "/r 1\n"),
        ("/u  library list", [], "/u\n"),
        ("/p  optimization level", [], "/p\n"),
    ]


def fail(msg, detail=""):
    print("FAIL: " + msg)
    if detail:
        for line in detail.rstrip().splitlines()[-10:]:
            print("    | " + line)
    sys.exit(1)


def run(obi, args, stdin_text, env, cwd):
    kwargs = dict(cwd=cwd, env=env, stdout=subprocess.PIPE,
                  stderr=subprocess.STDOUT, timeout=TIMEOUT)
    if stdin_text is None:
        kwargs["stdin"] = subprocess.DEVNULL
    else:
        kwargs["input"] = stdin_text.encode("utf-8")

    p = subprocess.run([obi] + args, **kwargs)
    return p.returncode, p.stdout.decode("utf-8", "replace")


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

    with tempfile.TemporaryDirectory() as tmp:
        src = os.path.join(tmp, "eof.obs")
        with open(src, "w", newline="\n") as f:
            f.write(PROGRAM)

        for label, args, stdin_text in cases(src):
            fed = "a closed stdin" if stdin_text is None else repr(stdin_text)
            try:
                rc, out = run(obi, args, stdin_text, env, tmp)
            except subprocess.TimeoutExpired as e:
                partial = (e.stdout or b"").decode("utf-8", "replace")
                fail("%s: obi did not exit within %ds given %s -- #917"
                     % (label, TIMEOUT, fed), partial)

            if rc < 0:
                fail("%s: obi died from signal %d at end of input" % (label, -rc), out)
            if rc != 0:
                fail("%s: obi exited %d at end of input, expected 0" % (label, rc), out)
            # the normal path has to keep working: --quit still runs the program
            if args[:1] == ["--file"] and "ran-the-program" not in out:
                fail("%s: obi exited 0 without running the program" % label, out)

            print("  %-24s exited 0" % label)

    print("PASS: obi exits when stdin reaches EOF")


if __name__ == "__main__":
    main()
