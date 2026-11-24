#!/usr/bin/env python3
"""
A library built with `-debug` can be linked and run.

Usage: check_lib_debug_symbols.py <bin_dir>

<bin_dir> must contain obc and obr (with .exe on Windows); the libraries are
read from the sibling lib/ directory, as check_lib_array_copy.py does.

`obc -tar lib -debug` writes a line number into every instruction of the .obl,
after the type byte (IntermediateInstruction::Write). The .obl reader
(Library::LoadStatements) read the line number FIRST and the type second. For a
library built without -debug there is no line number and the order does not
matter, which is why every shipped library and every test passed. For a debug
library the reader took the type byte for part of the line number and the first
byte of the line number for the type: the build of the consuming program
reported success and wrote a program whose library methods were scrambled, and
the VM refused it at load ("the method can run past its last instruction"). Both
-opt levels fail, but differently: at s3 the library's instructions are inlined
into the caller, at s0 they are copied unchanged.

This builds a debug library with a loop, a branch and a call, links a program
against it and runs it. Three things would let a broken check pass silently, and
each one fails here:
- obc "succeeding" by crashing: every invocation's return code is checked, and a
  negative code (a signal on POSIX) is named as such.
- a missing output file read as success: the .obl and the .obe must exist and be
  non-empty before the next step runs.
- the program running but computing nothing: its exact stdout must match.
"""

import os
import subprocess
import sys
import tempfile

EXE = ".exe" if os.name == "nt" else ""
TIMEOUT = 180

LIBRARY = """class DebugLib {
  function : Sum(n : Int) ~ Int {
    total := 0;
    for(i := 1; i <= n; i += 1;) {
      total += i;
    };
    return total;
  }

  function : Pick(a : Int, b : Int) ~ Int {
    if(a > b) {
      return a - b;
    };

    return b * 2;
  }
}
"""

PROGRAM = """class User {
  function : Main(args : String[]) ~ Nil {
    a := DebugLib->Sum(10);
    b := DebugLib->Pick(9, 4);
    c := DebugLib->Pick(3, 4);
    "sum={$a} pick={$b},{$c}"->PrintLine();
  }
}
"""

# 1+..+10 = 55; Pick(9,4) = 5; Pick(3,4) = 8
EXPECTED = ["sum=55 pick=5,8"]


def fail(msg):
    print("FAIL: %s" % msg)
    sys.exit(1)


def describe(code):
    if code < 0:
        return "died on signal %d" % -code
    return "exit %d" % code


def run(argv, cwd, env, what):
    try:
        proc = subprocess.run(argv, cwd=cwd, env=env, timeout=TIMEOUT,
                              stdin=subprocess.DEVNULL,
                              stdout=subprocess.PIPE, stderr=subprocess.STDOUT)
    except subprocess.TimeoutExpired:
        fail("%s timed out after %ds" % (what, TIMEOUT))
    out = proc.stdout.decode("utf-8", "replace").strip()
    if proc.returncode != 0:
        fail("%s %s\n%s" % (what, describe(proc.returncode), out))
    return out


def non_empty(path, what):
    if not os.path.isfile(path) or os.path.getsize(path) == 0:
        fail("%s produced no %s" % (what, os.path.basename(path)))


def main():
    if len(sys.argv) != 2:
        print(__doc__.strip())
        return 2

    bin_dir = os.path.abspath(sys.argv[1])
    obc = os.path.join(bin_dir, "obc" + EXE)
    obr = os.path.join(bin_dir, "obr" + EXE)
    lib_dir = os.path.join(os.path.dirname(bin_dir), "lib")
    for path in (obc, obr):
        if not os.path.isfile(path):
            fail("no %s" % path)
    if not os.path.isdir(lib_dir):
        fail("no library directory at %s" % lib_dir)

    with tempfile.TemporaryDirectory() as work:
        # obc resolves -lib against OBJECK_LIB_PATH, so the generated library
        # goes into a private copy of lib/ rather than the real deploy tree.
        private_lib = os.path.join(work, "lib")
        os.mkdir(private_lib)
        for name in os.listdir(lib_dir):
            if name.endswith(".obl"):
                with open(os.path.join(lib_dir, name), "rb") as src:
                    with open(os.path.join(private_lib, name), "wb") as dst:
                        dst.write(src.read())

        env = dict(os.environ)
        env["OBJECK_LIB_PATH"] = private_lib

        lib_src = os.path.join(work, "debuglib.obs")
        prog_src = os.path.join(work, "user.obs")
        with open(lib_src, "w") as handle:
            handle.write(LIBRARY)
        with open(prog_src, "w") as handle:
            handle.write(PROGRAM)

        # Distinct names per level, so a file left by the first round cannot
        # stand in for one the second round failed to write.
        for opt in ("s3", "s0"):
            lib_name = "debuglib_" + opt
            lib_obl = os.path.join(private_lib, lib_name + ".obl")
            prog_obe = os.path.join(work, "user_" + opt + ".obe")
            at = " at -opt " + opt

            run([obc, "-src", lib_src, "-tar", "lib", "-debug", "-dest", lib_obl, "-opt", opt],
                work, env, "debug library build" + at)
            non_empty(lib_obl, "debug library build" + at)

            run([obc, "-src", prog_src, "-lib", lib_name, "-dest", prog_obe, "-opt", opt],
                work, env, "program build" + at)
            non_empty(prog_obe, "program build" + at)

            out = run([obr, prog_obe], work, env, "program run" + at)

            lines = [l.strip() for l in out.splitlines() if l.strip()]
            if lines != EXPECTED:
                fail("wrong output%s\n  expected: %s\n  got:      %s"
                     % (at, EXPECTED, lines))

    print("PASS: a library built with -debug links and runs (-opt s3 and s0)")
    return 0


if __name__ == "__main__":
    sys.exit(main())
