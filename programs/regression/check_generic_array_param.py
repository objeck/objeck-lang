#!/usr/bin/env python3
"""A generic ARRAY parameter survives the round trip through a .obl.

Usage: check_generic_array_param.py <bin_dir>

<bin_dir> must contain obc and obr (with .exe on Windows); the libraries are
read from the sibling lib/ directory, as run_vm_flag_tests.py does.

`Vector[]<T>` as a parameter compiled fine inside a library and callers inside
that same library could reach it, but a program linking the library could not:
the call was rejected as an undefined method. The signature came back out of the
.obl with its array dimension missing, so `Vector[]<IntRef>` read as
`Vector<IntRef>` and nothing matched (#998).

The cause was one line. TypeParser::ParseGenerics stops ON the closing '>'
rather than past it, and ParseParameters then scanned for the dimension's '*'
without consuming that '>' -- so it saw '>' and left the dimension at 0.
ParseType, which handles RETURN types, already consumed it. That asymmetry is
why the same type worked in return position and failed as a parameter.

Only a library round trip shows this: put both signatures in a single program
and they resolve, because nothing is ever encoded. So this builds a library,
links a program against it, and runs it.

Four shapes are checked, because the bug was specific to the combination:

    Vector[]<IntRef>   generic + array    <- the broken one
    Vector<IntRef>     generic, no array
    Int[]              array, no generic
    Vector[]<IntRef>   in RETURN position <- went through the other parser

A fix that repaired only the first would pass a narrower test. A fix that broke
one of the other three would pass it too.

Two things that would let a broken check pass silently, and each fails here:
- a build that "succeeds" by crashing: every return code is checked, and a
  negative one (a signal on POSIX) is named as such.
- a program that runs but computes nothing: stdout must match exactly, so a
  binary that prints an empty line does not agree with one that works.
"""

import os
import subprocess
import sys
import tempfile

EXE = ".exe" if os.name == "nt" else ""
TIMEOUT = 180

LIBRARY = """use Collection;
bundle Gen.Lib {
  class Folds {
    function : TakeArray(a : Vector[]<IntRef>) ~ Int { return a->Size(); }
    function : TakeOne(v : Vector<IntRef>) ~ Int { return v->Size(); }
    function : TakePlain(a : Int[]) ~ Int { return a->Size(); }
    function : GiveArray(n : Int) ~ Vector[]<IntRef> { return Vector->New[n]<IntRef>; }
  }
}
"""

PROGRAM = """use Collection;
use Gen.Lib;
class GenUse {
  function : Main(args : String[]) ~ Nil {
    v := Vector->New()<IntRef>;
    v->AddBack(IntRef->New(7));
    v->AddBack(IntRef->New(8));

    arr := Vector->New[3]<IntRef>;
    arr[0] := v;

    plain := Int->New[4];

    a := Folds->TakeArray(arr);
    b := Folds->TakeOne(v);
    c := Folds->TakePlain(plain);
    d := Folds->GiveArray(5)->Size();

    "array={$a} one={$b} plain={$c} returned={$d}"->PrintLine();
  }
}
"""

EXPECTED = ["array=3 one=2 plain=4 returned=5"]


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

        lib_src = os.path.join(work, "genlib.obs")
        prog_src = os.path.join(work, "genuse.obs")
        with open(lib_src, "w") as handle:
            handle.write(LIBRARY)
        with open(prog_src, "w") as handle:
            handle.write(PROGRAM)

        lib_obl = os.path.join(private_lib, "genlib.obl")
        prog_obe = os.path.join(work, "genuse.obe")

        run([obc, "-src", lib_src, "-lib", "gen_collect", "-tar", "lib", "-dest", lib_obl],
            work, env, "library build")
        non_empty(lib_obl, "library build")

        run([obc, "-src", prog_src, "-lib", "genlib,gen_collect", "-dest", prog_obe],
            work, env, "program build")
        non_empty(prog_obe, "program build")

        out = run([obr, prog_obe], work, env, "program run")

    lines = [l.strip() for l in out.splitlines() if l.strip()]
    if lines != EXPECTED:
        fail("wrong output\n  expected: %s\n  got:      %s" % (EXPECTED, lines))

    print("PASS: a generic array parameter survives the .obl round trip")
    return 0


if __name__ == "__main__":
    sys.exit(main())
