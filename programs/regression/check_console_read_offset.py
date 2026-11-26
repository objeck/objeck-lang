#!/usr/bin/env python3
"""
Console->ReadBuffer(offset, num, Char[]) must store what it reads at 'offset'.

Usage: check_console_read_offset.py <bin_dir>

<bin_dir> must contain obc and obr (with .exe on Windows); the libraries are read
from the sibling lib/ directory, as check_console_eof.py does.

The Char[] variant of the standard-input buffer read validated 'offset' against
the destination but then copied the decoded text to the start of the array, so
a read at offset > 0 clobbered the first characters and left the offset slots
untouched. The Byte[] variant honoured the offset. The program below fills a
Char[] with a sentinel, reads four bytes at offset 3, and prints every slot, so a
misplaced write shows as a different layout. The regression
runner cannot control a test's stdin, so this feeds it in.
"""

import os
import subprocess
import sys
import tempfile

EXE = ".exe" if os.name == "nt" else ""
TIMEOUT = 120

PROGRAM = """class ConsoleReadOffset {
  function : Main(args : String[]) ~ Nil {
    chars := Char->New[8];
    each(i : chars) {
      chars[i] := 'z';
    };
    got := System.IO.Console->ReadBuffer(3, 4, chars);
    out := "";
    each(i : chars) {
      out += chars[i];
    };
    "chars read={$got} [{$out}]"->PrintLine();
  }
}
"""

# stdin is "abcdefgh". Only the placement is checked, not how many characters
# one read returns (a Windows console is in UTF-8 text mode, where a read
# returns fewer): slots 0-2 keep their sentinel and the first character read
# lands in slot 3. Before the fix it landed in slot 0.
PREFIX = "zzza"


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
        src = os.path.join(tmp, "console_read_offset.obs")
        obe = os.path.join(tmp, "console_read_offset.obe")
        with open(src, "w", newline="\n") as f:
            f.write(PROGRAM)

        p = subprocess.run([obc, "-src", src, "-dest", obe], cwd=bin_dir, env=env,
                           stdout=subprocess.PIPE, stderr=subprocess.STDOUT, timeout=TIMEOUT)
        if p.returncode != 0 or not os.path.isfile(obe):
            fail("the fixture did not compile", p.stdout.decode("utf-8", "replace"))

        try:
            p = subprocess.run([obr, obe], cwd=tmp, env=env, input=b"abcdefgh",
                               stdout=subprocess.PIPE, stderr=subprocess.STDOUT, timeout=TIMEOUT)
        except subprocess.TimeoutExpired as e:
            fail("obr did not finish within %ds" % TIMEOUT,
                 (e.stdout or b"").decode("utf-8", "replace"))

        out = p.stdout.decode("utf-8", "replace")
        lines = [l.rstrip("\r") for l in out.splitlines() if l.strip()]
        if p.returncode != 0:
            fail("exit %d" % p.returncode, out)
        match = [l for l in lines if l.startswith("chars read=")]
        if len(match) != 1 or not match[0].split("[", 1)[-1].startswith(PREFIX):
            fail("expected a Char[] starting %r, got %r" % (PREFIX, lines), out)

    print("PASS: Console->ReadBuffer stores at the requested offset")


if __name__ == "__main__":
    main()
