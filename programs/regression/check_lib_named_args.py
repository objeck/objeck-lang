#!/usr/bin/env python3
"""
Named arguments reach a library method through the parameter names its .obl
records (#1244, phase 2; docs/NAMED_ARGUMENTS_DESIGN.md).

Usage: check_lib_named_args.py <bin_dir>

<bin_dir> must contain obc and obr (with .exe on Windows); the libraries are
read from the sibling lib/ directory, as check_lib_array_copy.py does.

A call that skips a defaulted parameter in the middle cannot be a positional
call: it goes through the method's mask forwarder, 'F$m', which a library build
generates for a method with two or more defaults. Few shipped methods have two,
and the ones that do need native libraries, so this builds a library that does
and links a program against it. Covered:
- a function, a constructor and an instance method;
- a middle default skipped (the mask forwarder), and the arguments reordered
  with nothing skipped (a positional call);
- a middle default skipped while a later one is left out too ('Train("f",
  rate: 0.25)'): the library's trailing-default alternate Train(name, epochs,
  rate) was planned as well, and the call was rejected as matching more than
  one overload;
- the same shape on a constructor and on a function whose parameters all have
  defaults ('Opts->New(c: true)', the four-flag shape of TF_IDF->New): the
  alternates New(a, b, c) and New(a, b) were planned too;
- a method inherited from a library parent, both on a variable and chained --
  the chained form once bound the names by position, silently;
- a method whose parameters all have defaults, called with none (the zero-
  argument alternate was never generated when the first parameter had one);
- the arguments evaluated in the order written, not in parameter order;
- an unknown name, which must not compile.

Each program runs three ways: the default, the JIT on every call
(OBJECK_JIT_THRESHOLD=1) and the interpreter alone (OBJECK_JIT_DISABLE=1).
"""

import os
import subprocess
import sys
import tempfile

EXE = ".exe" if os.name == "nt" else ""
TIMEOUT = 180

LIBRARY = """bundle NamedLib {
  class Trainer {
    function : Train(name : String, epochs : Int := 10, rate : Float := 0.5, verbose : Bool := false) ~ String {
      return "{$name} e={$epochs} r={$rate} v={$verbose}";
    }
  }

  class Box {
    @w : Int; @h : Int; @d : Int;

    New(w : Int, h : Int := 2, d : Int := 3) {
      @w := w; @h := h; @d := d;
    }

    method : public : Show(sep : String := ",", open : String := "(") ~ String {
      return "{$open}{$@w}{$sep}{$@h}{$sep}{$@d}";
    }

    method : public : Scale(k : Int := 1, extra : Int := 0) ~ Int {
      return @w * @h * @d * k + extra;
    }
  }

  class Opts {
    @a : Bool; @b : Bool; @c : Bool; @d : Bool;

    New(a : Bool := true, b : Bool := true, c : Bool := false, d : Bool := false) {
      @a := a; @b := b; @c := c; @d := d;
    }

    method : public : Show() ~ String {
      return "{$@a} {$@b} {$@c} {$@d}";
    }

    function : Make(a : Bool := true, b : Bool := true, c : Bool := false, d : Bool := false) ~ String {
      return "{$a} {$b} {$c} {$d}";
    }
  }

  class Cube from Box {
    New(s : Int) {
      Parent(s, s, s);
    }
  }
}
"""

PROGRAM = """use NamedLib;

class User {
  @trace : String;

  New() {
    @trace := "";
  }

  method : Mark(t : String, v : Int) ~ Int {
    @trace += t;
    return v;
  }

  method : Run() ~ Nil {
    Trainer->Train("a")->PrintLine();
    Trainer->Train("b", verbose: true)->PrintLine();
    Trainer->Train("c", rate: 0.1, epochs: 50)->PrintLine();
    Trainer->Train(verbose: true, name: "d")->PrintLine();
    Trainer->Train("f", rate: 0.25)->PrintLine();
    Trainer->Train("e", rate: Mark("1", 2)->ToFloat(), epochs: Mark("2", 1))->PrintLine();
    @trace->PrintLine();

    Box->New(1)->Show()->PrintLine();
    Box->New(1, d: 9)->Show()->PrintLine();
    Box->New(d: 4, w: 5)->Show(open: "[")->PrintLine();

    Opts->New(c: true)->Show()->PrintLine();
    Opts->New(b: false, c: true)->Show()->PrintLine();
    Opts->New(d: true)->Show()->PrintLine();
    Opts->Make(c: true)->PrintLine();
    Opts->Make(b: false)->PrintLine();

    c := Cube->New(2);
    c->Scale(extra: 1, k: 2)->PrintLine();
    Cube->New(2)->Scale(extra: 1, k: 2)->PrintLine();
    Cube->New(2)->Scale(extra: 1)->PrintLine();

    total := 0;
    for(i := 0; i < 3000; i += 1;) {
      total += Box->New(1, d: i % 3)->Scale(extra: 1);
    };
    total->PrintLine();
  }

  function : Main(args : String[]) ~ Nil {
    User->New()->Run();
  }
}
"""

# The loop: Box(1, 2, d) scaled by 1 plus 1 is 2*d + 1, d cycling 0, 1, 2 --
# 1000 * (1 + 3 + 5).
EXPECTED = ["a e=10 r=0.500000 v=false", "b e=10 r=0.500000 v=true",
            "c e=50 r=0.100000 v=false", "d e=10 r=0.500000 v=true",
            "f e=10 r=0.250000 v=false",
            "e e=1 r=2.000000 v=false", "12",
            "(1,2,3", "(1,2,9", "[5,2,4",
            "true true true false", "true false true false", "true true false true",
            "true true true false", "true false false false",
            "17", "17", "9", "9000"]

BAD_PROGRAM = """use NamedLib;

class Bad {
  function : Main(args : String[]) ~ Nil {
    Trainer->Train("x", epochz: 3);
  }
}
"""

BAD_MESSAGE = "No parameter named 'epochz'; the parameters are: name, epochs, rate, verbose"

MODES = [("default", {}), ("JIT on every call", {"OBJECK_JIT_THRESHOLD": "1"}),
         ("interpreter", {"OBJECK_JIT_DISABLE": "1"})]


def fail(msg):
    print("FAIL: %s" % msg)
    sys.exit(1)


def describe(code):
    if code < 0:
        return "died on signal %d" % -code
    return "exit %d" % code


def run(argv, cwd, env, what, expect_ok=True):
    try:
        proc = subprocess.run(argv, cwd=cwd, env=env, timeout=TIMEOUT,
                              stdin=subprocess.DEVNULL,
                              stdout=subprocess.PIPE, stderr=subprocess.STDOUT)
    except subprocess.TimeoutExpired:
        fail("%s timed out after %ds" % (what, TIMEOUT))
    out = proc.stdout.decode("utf-8", "replace").strip()
    if expect_ok and proc.returncode != 0:
        fail("%s %s\n%s" % (what, describe(proc.returncode), out))
    if not expect_ok and proc.returncode <= 0:
        fail("%s should have failed to compile, got %s\n%s" % (what, describe(proc.returncode), out))
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
        env.pop("OBJECK_JIT_THRESHOLD", None)
        env.pop("OBJECK_JIT_DISABLE", None)

        sources = {"namedlib.obs": LIBRARY, "user.obs": PROGRAM, "bad.obs": BAD_PROGRAM}
        for name, text in sources.items():
            with open(os.path.join(work, name), "w") as handle:
                handle.write(text)

        lib_obl = os.path.join(private_lib, "namedlib.obl")
        prog_obe = os.path.join(work, "user.obe")

        run([obc, "-src", os.path.join(work, "namedlib.obs"), "-tar", "lib", "-dest", lib_obl],
            work, env, "library build")
        non_empty(lib_obl, "library build")

        run([obc, "-src", os.path.join(work, "user.obs"), "-lib", "namedlib", "-dest", prog_obe],
            work, env, "program build")
        non_empty(prog_obe, "program build")

        for mode, extra in MODES:
            mode_env = dict(env)
            mode_env.update(extra)
            out = run([obr, prog_obe], work, mode_env, "program run (%s)" % mode)
            lines = [l.strip() for l in out.splitlines() if l.strip()]
            if lines != EXPECTED:
                fail("wrong output (%s)\n  expected: %s\n  got:      %s" % (mode, EXPECTED, lines))

        out = run([obc, "-src", os.path.join(work, "bad.obs"), "-lib", "namedlib",
                   "-dest", os.path.join(work, "bad.obe")], work, env, "an unknown name", expect_ok=False)
        if BAD_MESSAGE not in out:
            fail("an unknown name: expected '%s'\n%s" % (BAD_MESSAGE, out))

    print("PASS: named arguments reach a library's methods (three modes), and an unknown name is refused")
    return 0


if __name__ == "__main__":
    sys.exit(main())
