#!/usr/bin/env python3
"""
A library may use the array copy constructor, and a program may link it.

Usage: check_lib_array_copy.py <bin_dir>

<bin_dir> must contain obc and obr (with .exe on Windows); the libraries are
read from the sibling lib/ directory, as run_vm_flag_tests.py does.

`Int->New[src]` compiles to a call into System.$Int:Copy. Emitting that call
from a `-tar lib` build wrote the callee's numeric ids straight into the .obl --
but those ids are local to the library being written, and the program that links
it numbers its own classes. The consuming build looked the id up, missed, and
dereferenced a map::end(): obc died of SIGSEGV with no message and no output
file, and the library was simply unusable (#958). The library itself built
cleanly, which is why this needs both halves.

No shipped library happens to use the construct, so nothing in the regression
suite reached it. This builds a library that does.

Three things would let a broken check pass silently, and each one fails here:
- obc "succeeding" by crashing. Every invocation's return code is checked, and
  a negative code (a signal on POSIX) is named as such.
- a missing output file read as success. Both the .obl and the .obe must exist
  and be non-empty before the next step runs.
- the program running but computing nothing. Its exact stdout must match, so a
  binary that prints an empty line does not "agree" with one that works.

Every element type the copy constructor supports is covered: a regression that
reached only one of the switch arms would otherwise pass. Bool is one of them as
of #995 -- it had no arm at all, so the call emitted nothing and the constructor
silently returned the source array rather than a copy.

Both -opt s3 and -opt s0 build, link and run, because the two levels failed
differently: at s1 and above obc died in the inliners, while at s0 it reported
success and handed obr a corrupt .obe. A check that ran only s3 would miss half
of that.

Not covered here: linking a .obl left over from an EARLIER BUILD of the same
version, which is the case #970's emission-time refusal exists for. Producing
one needs a pre-#960 compiler, so this check -- which only ever has the compiler
it is handed -- cannot construct the input. Keeping that honest needs a checked-in
.obl fixture, and the .obl format is not stable enough to carry one lightly.
"""

import os
import subprocess
import sys
import tempfile

EXE = ".exe" if os.name == "nt" else ""
TIMEOUT = 180

LIBRARY = """class AryCopy {
  function : Ints(src : Int[]) ~ Int[] {
    return Int->New[src];
  }

  function : Chars(src : Char[]) ~ Char[] {
    return Char->New[src];
  }

  function : Bytes(src : Byte[]) ~ Byte[] {
    return Byte->New[src];
  }

  function : Floats(src : Float[]) ~ Float[] {
    return Float->New[src];
  }

  function : Bools(src : Bool[]) ~ Bool[] {
    return Bool->New[src];
  }
}
"""

PROGRAM = """class User {
  function : Main(args : String[]) ~ Nil {
    i := Int->New[3];
    i[0] := 7; i[1] := 8; i[2] := 9;
    ic := AryCopy->Ints(i);
    ic[0] := 70;
    a := ic[0]; b := ic[1]; c := ic[2]; d := i[0];
    "int={$a},{$b},{$c} src={$d}"->PrintLine();

    ch := Char->New[2];
    ch[0] := 'a'; ch[1] := 'b';
    cc := AryCopy->Chars(ch);
    e := cc[0]; f := cc[1]; g := cc->Size();
    "char={$e}{$f} size={$g}"->PrintLine();

    by := Byte->New[2];
    by[0] := 1; by[1] := 2;
    bc := AryCopy->Bytes(by);
    h := bc[0]->ToInt(); j := bc[1]->ToInt();
    "byte={$h},{$j}"->PrintLine();

    fl := Float->New[2];
    fl[0] := 1.5; fl[1] := 2.5;
    fc := AryCopy->Floats(fl);
    k := fc[0]; m := fc[1];
    "float={$k},{$m}"->PrintLine();

    bo := Bool->New[3];
    bo[0] := true; bo[1] := false; bo[2] := true;
    lc := AryCopy->Bools(bo);
    bo[0] := false;
    p := lc[0]; q := lc[1]; r := lc[2]; s := lc->Size(); t := bo[0];
    "bool={$p},{$q},{$r} size={$s} src={$t}"->PrintLine();
  }
}
"""

# A copy is a copy: writing through it must not reach the source array -- "src=7"
# is the check that Ints() returned a copy and not the caller's own array.
# Floats interpolate at six decimal places; bytes are widened with ToInt() so the
# expectation is digits rather than two control characters.
#
# The bool line writes through the SOURCE after copying, which is the direction
# #995 failed in: BOOLEAN_TYPE had no arm in the emitter's copy-constructor
# switch, so the call emitted nothing, the source reference stayed on the stack,
# and Bool->New[src] handed back src itself. "src=false" pins the other half --
# without it the line would also pass if the write simply never happened.
EXPECTED = ["int=70,8,9 src=7", "char=ab size=2", "byte=1,2", "float=1.500000,2.500000",
            "bool=true,false,true size=3 src=false"]


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

        lib_src = os.path.join(work, "arycopy.obs")
        prog_src = os.path.join(work, "user.obs")
        with open(lib_src, "w") as handle:
            handle.write(LIBRARY)
        with open(prog_src, "w") as handle:
            handle.write(PROGRAM)

        # Both levels, because the bug failed differently at each. At s1 and
        # above the bad id was dereferenced in the inliners and obc died; at s0
        # nothing dereferenced it, obc reported success, and the corruption rode
        # into the .obe to fail in obr. Only s3 was ever exercised here.
        #
        # The run is what carries s0: #970 refuses an unresolvable id at
        # emission, so today a regression would be caught at the program build
        # -- but that check is one `if` away from being narrowed, and if it ever
        # is, s3 keeps passing while s0 quietly goes back to writing a .obe that
        # only obr rejects. Running it is what notices.
        for opt in ("s3", "s0"):
            # Distinct names per level, so a file left by the first round cannot
            # stand in for one the second round failed to write.
            lib_name = "arycopy_" + opt
            lib_obl = os.path.join(private_lib, lib_name + ".obl")
            prog_obe = os.path.join(work, "user_" + opt + ".obe")
            at = " at -opt " + opt

            run([obc, "-src", lib_src, "-tar", "lib", "-dest", lib_obl, "-opt", opt],
                work, env, "library build" + at)
            non_empty(lib_obl, "library build" + at)

            run([obc, "-src", prog_src, "-lib", lib_name, "-dest", prog_obe, "-opt", opt],
                work, env, "program build" + at)
            non_empty(prog_obe, "program build" + at)

            out = run([obr, prog_obe], work, env, "program run" + at)

            lines = [l.strip() for l in out.splitlines() if l.strip()]
            if lines != EXPECTED:
                fail("wrong output%s\n  expected: %s\n  got:      %s"
                     % (at, EXPECTED, lines))

    print("PASS: a library's array copy constructor survives being linked (-opt s3 and s0)")
    return 0


if __name__ == "__main__":
    sys.exit(main())
