#!/usr/bin/env python3
"""
Method-level generics reach a library's methods (#1242, phase 2;
docs/METHOD_GENERICS_DESIGN.md).

Usage: check_lib_generic_methods.py <bin_dir>

<bin_dir> must contain obc and obr (with .exe on Windows); the libraries are
read from the sibling lib/ directory, as check_lib_array_copy.py does.

A library's signatures carry the bound a type parameter was erased to, not the
parameter, so until the .obl recorded where each one stands a caller could not
infer it: Pick->First(names) on a library function returned a Base, not a String.
This builds a library with generic functions and methods -- a plain T, a T[],
a C<T>, a bound, a function parameter -- links a program against it, and checks
the inferred types are used without a cast, in three modes. A bound the
argument does not meet must not compile.
"""

import os
import subprocess
import sys
import tempfile

EXE = ".exe" if os.name == "nt" else ""
TIMEOUT = 180

LIBRARY = """use Collection;

bundle GenLib {
  class Pick {
    function : First<T>(a : T[]) ~ T {
      return a[0];
    }

    function : Max<T : Compare>(values : Vector<T>) ~ T {
      best := values->Get(0);
      for(i := 1; i < values->Size(); i += 1;) {
        v := values->Get(i);
        if(v->Compare(best) > 0) {
          best := v;
        };
      };
      return best;
    }

    function : Apply<A, B>(a : A, f : (A) ~ B) ~ B {
      return f(a);
    }
  }

  class Box {
    @items : String[];

    New() {
      @items := ["x", "yy", "zzz"];
    }

    method : public : At<T>(a : T[], i : Int) ~ T {
      return a[i];
    }

    method : public : Items() ~ String[] {
      return @items;
    }
  }
}
"""

PROGRAM = """use Collection;
use GenLib;

class User {
  function : Size(s : String) ~ IntRef {
    return IntRef->New(s->Size());
  }

  function : Main(args : String[]) ~ Nil {
    names := ["pear", "apple"];
    Pick->First(names)->ToUpper()->PrintLine();
    v := Vector->New()<IntRef>;
    v->AddBack(IntRef->New(3));
    v->AddBack(IntRef->New(9));
    (Pick->Max(v)->Get() + 1)->PrintLine();
    Pick->Apply("four", Size(String) ~ IntRef)->Get()->PrintLine();
    b := Box->New();
    b->At(b->Items(), 2)->Size()->PrintLine();
    total := 0;
    for(i := 0; i < 3000; i += 1;) {
      total += b->At(b->Items(), i % 3)->Size();
    };
    total->PrintLine();
  }
}
"""

EXPECTED = ["PEAR", "10", "4", "3", "6000"]

BAD_PROGRAM = """use Collection;
use GenLib;

class Bad {
  function : Main(args : String[]) ~ Nil {
    v := Vector->New()<Base>;
    v->AddBack(Base->New());
    Pick->Max(v);
  }
}
"""

BAD_MESSAGE = "does not meet its bound"

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

        sources = {"genlib.obs": LIBRARY, "user.obs": PROGRAM, "bad.obs": BAD_PROGRAM}
        for name, text in sources.items():
            with open(os.path.join(work, name), "w") as handle:
                handle.write(text)

        lib_obl = os.path.join(private_lib, "genlib.obl")
        prog_obe = os.path.join(work, "user.obe")

        run([obc, "-src", os.path.join(work, "genlib.obs"), "-tar", "lib", "-lib", "collect", "-dest", lib_obl],
            work, env, "library build")
        non_empty(lib_obl, "library build")

        run([obc, "-src", os.path.join(work, "user.obs"), "-lib", "collect,genlib", "-dest", prog_obe],
            work, env, "program build")
        non_empty(prog_obe, "program build")

        for mode, extra in MODES:
            mode_env = dict(env)
            mode_env.update(extra)
            out = run([obr, prog_obe], work, mode_env, "program run (%s)" % mode)
            lines = [l.strip() for l in out.splitlines() if l.strip()]
            if lines != EXPECTED:
                fail("wrong output (%s)\n  expected: %s\n  got:      %s" % (mode, EXPECTED, lines))

        out = run([obc, "-src", os.path.join(work, "bad.obs"), "-lib", "collect,genlib",
                   "-dest", os.path.join(work, "bad.obe")], work, env, "a bound not met", expect_ok=False)
        if BAD_MESSAGE not in out:
            fail("a bound not met: expected '%s'\n%s" % (BAD_MESSAGE, out))

    print("PASS: a library's generic methods infer their type arguments (three modes), and a bound is checked")
    return 0


if __name__ == "__main__":
    sys.exit(main())
