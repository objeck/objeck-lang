#!/usr/bin/env python3
"""
An array copy constructor has to survive being compiled into a library.

Usage: check_lib_array_copy.py <bin_dir>

<bin_dir> must contain obc and obr (with .exe on Windows); the libraries are
read from the sibling lib/ directory, as check_obi_obr_parity.py does.

'Int->New[other]' compiles to a call to 'System.$Int:Copy', and the emitter had
one way of writing that call: MTHD_CALL, carrying the numeric class and method
ids of System.$Int. Those ids are assigned per compilation unit and mean
nothing outside it, which is why every other method call in the emitter picks
LIB_MTHD_CALL -- names, resolved by the linker -- when the target is a library.
These four (Byte, Char, Int, Float) did not, so '-tar lib' wrote a foreign id
into the .obl and the next build read it as one of its own:

  -opt s1 and above   the optimizer looked the id up while inlining and
                      dereferenced the end of its class map -- obc died with
                      an access violation (SIGSEGV on POSIX)
  -opt s0             no lookup, so obc happily wrote a .obe holding the
                      foreign id, and obr took the access violation instead

The second is the reason this runs both levels and checks the output rather
than just the exit status: at s0 the broken compiler *succeeded*, and only
running the program showed it had produced nonsense.

Element types are covered one per method so a single type regressing is named
by the diff, and the Int case takes two copies and writes to one, since an
emitter that returned the original array instead of a copy would otherwise
print the same values.

This needs two compilations -- one '-tar lib', one linking it -- so it cannot
be a plain programs/regression/*.obs, which the runners compile once and run.
"""

import os
import subprocess
import sys
import tempfile

EXE = ".exe" if os.name == "nt" else ""

# Compiling two small files and running them takes well under a second; this is
# only here so a wedged tool fails the check instead of hanging the job.
TIMEOUT = 120

# Deliberately not a name any shipped library could have, since it is written
# into the real lib/ directory: -lib resolves every name against
# OBJECK_LIB_PATH, so a temporary directory is not an option.
LIB_NAME = "_check_lib_array_copy"

# STATUS_ACCESS_VIOLATION, the status Windows gives a process killed for a bad
# pointer dereference.
ACCESS_VIOLATION = 0xC0000005

LIBRARY = """bundle AryCopyProbe {
  class Holder {
    @i : Int[];
    @b : Byte[];
    @c : Char[];
    @f : Float[];

    New(i : Int[], b : Byte[], c : Char[], f : Float[]) {
      @i := Int->New[i];
      @b := Byte->New[b];
      @c := Char->New[c];
      @f := Float->New[f];
    }

    method : public : DupInt() ~ Int[] { return Int->New[@i]; }
    method : public : DupByte() ~ Byte[] { return Byte->New[@b]; }
    method : public : DupChar() ~ Char[] { return Char->New[@c]; }
    method : public : DupFloat() ~ Float[] { return Float->New[@f]; }
  }
}
"""

PROGRAM = """use AryCopyProbe;

class UseAryCopy {
  function : Main(args : String[]) ~ Nil {
    i := Int->New[2];   i[0] := 7;   i[1] := 8;
    b := Byte->New[2];  b[0] := 3;   b[1] := 4;
    c := Char->New[2];  c[0] := 'x'; c[1] := 'y';
    f := Float->New[2]; f[0] := 1.5; f[1] := 2.5;

    h := Holder->New(i, b, c, f);

    di := h->DupInt();
    di[0]->PrintLine();
    di[1]->PrintLine();

    db := h->DupByte();
    db[0]->PrintLine();
    db[1]->PrintLine();

    dc := h->DupChar();
    dc[0]->PrintLine();
    dc[1]->PrintLine();

    df := h->DupFloat();
    df[0]->PrintLine();
    df[1]->PrintLine();

    # a copy, not the original: writing to one must not reach the other
    again := h->DupInt();
    again[0] := 99;
    di[0]->PrintLine();
  }
}
"""

EXPECTED = ["7", "8", "0x3", "0x4", "x", "y", "1.5", "2.5", "7"]


def fail(msg, detail=""):
    print("FAIL: " + msg)
    if detail:
        for line in detail.rstrip().splitlines()[-15:]:
            print("    | " + line)
    sys.exit(1)


def run(cmd, env, cwd):
    p = subprocess.run(cmd, cwd=cwd, env=env, stdout=subprocess.PIPE,
                       stderr=subprocess.STDOUT, timeout=TIMEOUT)
    return p.returncode, p.stdout.decode("utf-8", "replace")


def describe(rc):
    # A bad pointer is the failure this check exists to catch, so name it rather
    # than printing a status nobody can read. Windows reports the exception code
    # as the exit status, signed or not depending on the Python build; POSIX
    # reports the signal as a negative status.
    if rc in (ACCESS_VIOLATION, ACCESS_VIOLATION - (1 << 32)):
        return "exited %d (access violation)" % rc
    if rc < 0:
        return "died from signal %d" % -rc
    return "exited %d" % rc


def check_level(obc, obr, lib_dir, tmp, env, opt):
    lib_src = os.path.join(tmp, "arycopyprobe.obs")
    prog_src = os.path.join(tmp, "usearycopy.obs")
    obl = os.path.join(lib_dir, LIB_NAME + ".obl")
    obe = os.path.join(tmp, "usearycopy.obe")

    rc, out = run([obc, "-src", lib_src, "-tar", "lib", "-opt", opt,
                   "-dest", obl], env, tmp)
    if rc != 0:
        fail("-opt %s: building the library %s" % (opt, describe(rc)), out)
    if not os.path.isfile(obl):
        fail("-opt %s: obc reported success but wrote no %s" % (opt, obl), out)

    rc, out = run([obc, "-src", prog_src, "-lib", LIB_NAME, "-opt", opt,
                   "-dest", obe], env, tmp)
    if rc != 0:
        fail("-opt %s: linking the library into a program %s -- the .obl "
             "carries a class id from the library's own compilation"
             % (opt, describe(rc)), out)

    rc, out = run([obr, obe], env, tmp)
    if rc != 0:
        fail("-opt %s: running the linked program %s" % (opt, describe(rc)), out)

    got = [line.strip() for line in out.splitlines() if line.strip()]
    if got != EXPECTED:
        fail("-opt %s: wrong values from the copied arrays\n    expected %s\n    got      %s"
             % (opt, EXPECTED, got), out)

    print("  -opt %-3s library built, linked and ran: %s" % (opt, ",".join(got)))


def main():
    if len(sys.argv) < 2:
        print(__doc__)
        sys.exit(2)

    bin_dir = os.path.abspath(sys.argv[1])
    obc = os.path.join(bin_dir, "obc" + EXE)
    obr = os.path.join(bin_dir, "obr" + EXE)
    for tool in (obc, obr):
        if not os.path.isfile(tool):
            fail("missing " + tool)

    lib_dir = os.path.join(os.path.dirname(bin_dir), "lib")
    if not os.path.isdir(lib_dir):
        fail("missing " + lib_dir)

    obl = os.path.join(lib_dir, LIB_NAME + ".obl")
    if os.path.exists(obl):
        fail("%s already exists; refusing to overwrite it" % obl)

    env = dict(os.environ)
    env["OBJECK_LIB_PATH"] = lib_dir + os.sep

    try:
        with tempfile.TemporaryDirectory() as tmp:
            with open(os.path.join(tmp, "arycopyprobe.obs"), "w", newline="\n") as f:
                f.write(LIBRARY)
            with open(os.path.join(tmp, "usearycopy.obs"), "w", newline="\n") as f:
                f.write(PROGRAM)

            # s0 and s3 fail differently; see the module docstring.
            for opt in ("s0", "s3"):
                check_level(obc, obr, lib_dir, tmp, env, opt)
    finally:
        # The .obl has to live in the real lib/ directory, so take it back out
        # whatever happened -- a leftover would sit in a shipped tree.
        if os.path.exists(obl):
            os.remove(obl)

    print("PASS: array copy constructors survive a library round trip")


if __name__ == "__main__":
    main()
