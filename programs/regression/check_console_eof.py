#!/usr/bin/env python3
"""
Console->ReadLine must return Nil at the end of input, and an empty string only
for a blank line.

Usage: check_console_eof.py <bin_dir>

<bin_dir> must contain obc and obr (with .exe on Windows); the libraries are read
from the sibling lib/ directory, as check_obi_stdin_eof.py does.

ReadLine used to return "" at the end of input, forever: a closed stdin, an
exhausted pipe and a blank line were the same value, so
    while(line <> Nil) { ... line := Console->ReadLine(); }
never ended (fault-injection finding, stdin closed on a program reading prompts:
EOF detected 0 of 10 runs). The program below reads until Nil, then reads again
to show that the end stays the end. The regression runner cannot control a test's
stdin, so each case here feeds one in:

  closed stdin          no lines
  a, blank, b + EOL     three lines, the middle one empty (not the end)
  a, b without EOL      the last line has no line ending and is still a line
  one blank line        exactly one empty line, then the end
  lone partial line     one character and no line ending, then the end
"""

import os
import subprocess
import sys
import tempfile

EXE = ".exe" if os.name == "nt" else ""
TIMEOUT = 120

PROGRAM = """class ConsoleEof {
  function : Main(args : String[]) ~ Nil {
    count := 0;
    line := System.IO.Console->ReadLine();
    while(line <> Nil & count < 20) {
      size := line->Size();
      "line {$count}=[{$line}] size={$size}"->PrintLine();
      count += 1;
      line := System.IO.Console->ReadLine();
    };

    if(line = Nil) {
      "eof after {$count}"->PrintLine();
    }
    else {
      "no eof after {$count}"->PrintLine();
    };

    again := System.IO.Console->ReadLine();
    if(again = Nil) {
      "eof again"->PrintLine();
    }
    else {
      "not eof again"->PrintLine();
    };
  }
}
"""

# (label, stdin bytes -- None closes stdin outright, expected lines)
CASES = [
    ("closed stdin", None, ["eof after 0", "eof again"]),
    ("empty input", b"", ["eof after 0", "eof again"]),
    ("a, blank, b", b"a\n\nb\n",
     ["line 0=[a] size=1", "line 1=[] size=0", "line 2=[b] size=1", "eof after 3", "eof again"]),
    ("no final line ending", b"a\nb",
     ["line 0=[a] size=1", "line 1=[b] size=1", "eof after 2", "eof again"]),
    ("one blank line", b"\n", ["line 0=[] size=0", "eof after 1", "eof again"]),
    ("lone partial line", b"x", ["line 0=[x] size=1", "eof after 1", "eof again"]),
]


def fail(msg, detail=""):
    print("FAIL: " + msg)
    if detail:
        for line in detail.rstrip().splitlines()[-12:]:
            print("    | " + line)
    sys.exit(1)


def main():
    if len(sys.argv) < 2:
        print(__doc__)
        sys.exit(2)

    bin_dir = os.path.abspath(sys.argv[1])
    obc = os.path.join(bin_dir, "obc" + EXE)
    obr = os.path.join(bin_dir, "obr" + EXE)
    for exe in (obc, obr):
        if not os.path.isfile(exe):
            fail("missing " + exe)

    env = dict(os.environ)
    env["OBJECK_LIB_PATH"] = os.path.join(os.path.dirname(bin_dir), "lib") + os.sep

    with tempfile.TemporaryDirectory() as tmp:
        src = os.path.join(tmp, "console_eof.obs")
        obe = os.path.join(tmp, "console_eof.obe")
        with open(src, "w", newline="\n") as f:
            f.write(PROGRAM)

        p = subprocess.run([obc, "-src", src, "-dest", obe], cwd=bin_dir, env=env,
                           stdout=subprocess.PIPE, stderr=subprocess.STDOUT, timeout=TIMEOUT)
        if p.returncode != 0 or not os.path.isfile(obe):
            fail("the fixture did not compile", p.stdout.decode("utf-8", "replace"))

        for label, stdin_bytes, expected in CASES:
            kwargs = dict(cwd=tmp, env=env, stdout=subprocess.PIPE,
                          stderr=subprocess.STDOUT, timeout=TIMEOUT)
            if stdin_bytes is None:
                kwargs["stdin"] = subprocess.DEVNULL
            else:
                kwargs["input"] = stdin_bytes
            try:
                p = subprocess.run([obr, obe], **kwargs)
            except subprocess.TimeoutExpired as e:
                fail("%s: obr did not finish within %ds" % (label, TIMEOUT),
                     (e.stdout or b"").decode("utf-8", "replace"))

            out = p.stdout.decode("utf-8", "replace")
            lines = [l.rstrip("\r") for l in out.splitlines() if l.strip()]
            if p.returncode != 0:
                fail("%s: exit %d" % (label, p.returncode), out)
            if lines != expected:
                fail("%s: expected %r, got %r" % (label, expected, lines), out)
            print("  %-22s ok" % label)

    print("PASS: Console->ReadLine returns Nil at the end of input")


if __name__ == "__main__":
    main()
