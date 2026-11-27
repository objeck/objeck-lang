#!/usr/bin/env python3
"""
Console->WriteBuffer(offset, num, Byte[]) must write the bytes that start at 'offset'.

Usage: check_console_write_offset.py <bin_dir>

<bin_dir> must contain obc and obr (with .exe on Windows); the libraries are read
from the sibling lib/ directory, as check_console_read_offset.py does.

The Byte[] variant of the standard-output buffer write validated 'offset' against
the array and then wrote 'num' bytes from the START of the array: a write of three
bytes at offset 2 of "abcdefgh" printed "abc" where "cde" was asked for. The Char[]
variant, and the file, pipe and socket writers, honoured the offset. Found by the
Char[] / Byte[] length audit of the VM's native methods (v2026.11.4).

The program writes four ranges of an exact-length Byte[] (no spare element in use),
each followed by a line break, so a misplaced or over-long write shows as a
different line. The regression runner cannot capture a test's own standard output,
so this reads it.
"""

import os
import subprocess
import sys
import tempfile

EXE = ".exe" if os.name == "nt" else ""
TIMEOUT = 120

PROGRAM = """class ConsoleWriteOffset {
  function : Main(args : String[]) ~ Nil {
    bytes := "abcdefgh"->ToByteArray();
    line := "\\n"->ToByteArray();

    System.IO.Console->WriteBuffer(2, 3, bytes);
    System.IO.Console->WriteBuffer(line);
    System.IO.Console->WriteBuffer(bytes);
    System.IO.Console->WriteBuffer(line);
    System.IO.Console->WriteBuffer(5, 3, bytes);
    System.IO.Console->WriteBuffer(line);
    System.IO.Console->WriteBuffer(8, 0, bytes);
    System.IO.Console->WriteBuffer(line);
    System.IO.Console->WriteBuffer(0, 1, bytes);
    System.IO.Console->WriteBuffer(line);
  }
}
"""

EXPECTED = ["cde", "abcdefgh", "fgh", "", "a"]


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
        src = os.path.join(tmp, "console_write_offset.obs")
        obe = os.path.join(tmp, "console_write_offset.obe")
        with open(src, "w", newline="\n") as f:
            f.write(PROGRAM)

        p = subprocess.run([obc, "-src", src, "-dest", obe], cwd=bin_dir, env=env,
                           stdout=subprocess.PIPE, stderr=subprocess.STDOUT, timeout=TIMEOUT)
        if p.returncode != 0 or not os.path.isfile(obe):
            fail("the fixture did not compile", p.stdout.decode("utf-8", "replace"))

        # binary stdio: the bytes reach the pipe as written, with no text-mode translation
        try:
            p = subprocess.run([obr, "--objeck-stdio=binary", obe], cwd=tmp, env=env, stdin=subprocess.DEVNULL,
                               stdout=subprocess.PIPE, stderr=subprocess.STDOUT, timeout=TIMEOUT)
        except subprocess.TimeoutExpired as e:
            fail("obr did not finish within %ds" % TIMEOUT,
                 (e.stdout or b"").decode("utf-8", "replace"))

        out = p.stdout.decode("utf-8", "replace")
        if p.returncode != 0:
            fail("exit %d" % p.returncode, out)
        lines = out.replace("\r\n", "\n").split("\n")
        if lines and lines[-1] == "":
            lines.pop()
        if lines != EXPECTED:
            fail("expected the lines %r, got %r" % (EXPECTED, lines), out)

    print("PASS: Console->WriteBuffer writes the bytes at the requested offset")


if __name__ == "__main__":
    main()
