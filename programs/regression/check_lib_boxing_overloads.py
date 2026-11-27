#!/usr/bin/env python3
"""
Boxing and unboxing choose among a LIBRARY's overloads by the same rule as a
program's.

Usage: check_lib_boxing_overloads.py <bin_dir>

<bin_dir> must contain obc and obr (with .exe on Windows); the libraries are
read from the sibling lib/ directory, as check_lib_generic_chain.py does.

core_boxing_rules.obs covers methods compiled with the program. A library's
methods are resolved by other code (the parameter types come out of the .obl,
and the candidates are matched and ranked in the library path of the
analyzer), so the rule is checked there as well. The library declares the
overloads holder-first on purpose: the choice must not depend on the order.

  - an argument of the parameter's own type is the best fit (Int for Int,
    IntRef for IntRef), whatever order the overloads are in
  - one that converts (Int for Float) is better than one that is boxed (Int
    for IntRef)
  - a primitive is boxed into its own holder before another (Int for IntRef,
    not FloatRef), and is never offered to a holder it cannot convert to
    (an Int for a BoolRef)
  - a holder is unboxed for a primitive parameter (IntRef for Int, for Float),
    from a variable and from the end of a call chain, by an instance method
    and a function
  - a generic library class that holds IntRef takes and gives Ints
  - a call two overloads fit equally is refused, on its line, with both named

Each program is built at -opt s0 and s3 and runs three ways: the default, the
JIT on every call and the interpreter alone.
"""

import os
import subprocess
import sys
import tempfile

EXE = ".exe" if os.name == "nt" else ""
TIMEOUT = 180

LIBRARY = """use Collection;

bundle BoxLib {
  class Pick {
    function : Kind(v : IntRef) ~ String { return "IntRef"; }
    function : Kind(v : Int) ~ String { return "Int"; }

    function : Wide(v : IntRef) ~ String { return "IntRef"; }
    function : Wide(v : Float) ~ String { return "Float"; }

    function : Held(v : FloatRef) ~ String { return "FloatRef"; }
    function : Held(v : IntRef) ~ String { return "IntRef"; }

    function : Flag(v : BoolRef) ~ String { return "BoolRef"; }
    function : Flag(v : Int) ~ String { return "Int"; }

    function : Twice(v : Int) ~ Int { return v * 2; }
    function : Half(v : Float) ~ Float { return v / 2.0; }

    function : Both(a : IntRef, b : Int) ~ String { return "first"; }
    function : Both(a : Int, b : IntRef) ~ String { return "second"; }
  }

  class Meter {
    @total : Int;

    New() {
      @total := 0;
    }

    method : public : Add(amount : Int) ~ Meter {
      @total += amount;
      return @self;
    }

    method : public : Total() ~ IntRef {
      return IntRef->New(@total);
    }

    method : public : Me() ~ Meter {
      return @self;
    }
  }

  class Bag<T> {
    @items : Vector<T>;

    New() {
      @items := Vector->New()<T>;
    }

    method : public : Put(item : T) ~ Nil {
      @items->AddBack(item);
    }

    method : public : Take(index : Int) ~ T {
      return @items->Get(index);
    }
  }
}
"""

PROGRAM = """use Collection;
use BoxLib;

class User {
  function : Main(args : String[]) ~ Nil {
    ir := IntRef->New(8);
    fr := FloatRef->New(3.0);

    Pick->Kind(3)->PrintLine();
    Pick->Kind(ir)->PrintLine();
    Pick->Wide(3)->PrintLine();
    Pick->Wide(3.5)->PrintLine();
    Pick->Wide(ir)->PrintLine();
    Pick->Held(3)->PrintLine();
    Pick->Held(3.5)->PrintLine();
    Pick->Flag(3)->PrintLine();
    Pick->Flag(true)->PrintLine();

    Pick->Twice(ir)->PrintLine();
    Pick->Twice(fr)->PrintLine();
    Pick->Half(ir)->PrintLine();

    meter := Meter->New();
    meter->Add(ir)->Add(2);
    total : Int := meter->Me()->Total();
    total->PrintLine();
    Pick->Twice(meter->Me()->Total())->PrintLine();
    (meter->Me()->Total() * 0.5)->PrintLine();

    bag := Bag->New()<IntRef>;
    bag->Put(40);
    bag->Put(ir);
    (bag->Take(0) + bag->Take(1))->PrintLine();
    first : Int := bag->Take(0);
    first->PrintLine();
  }
}
"""

EXPECTED = ["Int", "IntRef", "Float", "Float", "IntRef", "IntRef", "FloatRef", "Int", "BoolRef",
            "16", "6", "4", "10", "20", "5", "48", "40"]

BAD_PROGRAM = """use BoxLib;

class Bad {
  function : Main(args : String[]) ~ Nil {
    Pick->Both(1, 2)->PrintLine();
  }
}
"""

BAD_MESSAGE = "bad.obs:(5,5): Ambiguous function/method call: 'Both(..)' boxes or unboxes an argument to fit, and more than one method fits as well"
BAD_NAMES = ["function : BoxLib.Pick->Both(System.IntRef, Int) ~ System.String",
             "function : BoxLib.Pick->Both(Int, System.IntRef) ~ System.String"]

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
            if name.endswith(".obl") or name == "configobjk.ini":
                with open(os.path.join(lib_dir, name), "rb") as src:
                    with open(os.path.join(private_lib, name), "wb") as dst:
                        dst.write(src.read())

        env = dict(os.environ)
        env["OBJECK_LIB_PATH"] = private_lib
        env.pop("OBJECK_JIT_THRESHOLD", None)
        env.pop("OBJECK_JIT_DISABLE", None)

        sources = {"boxlib.obs": LIBRARY, "user.obs": PROGRAM, "bad.obs": BAD_PROGRAM}
        for name, text in sources.items():
            with open(os.path.join(work, name), "w") as handle:
                handle.write(text)

        lib_obl = os.path.join(private_lib, "boxlib.obl")
        run([obc, "-src", "boxlib.obs", "-tar", "lib", "-lib", "collect", "-opt", "s3", "-dest", lib_obl],
            work, env, "library build")
        non_empty(lib_obl, "library build")

        for opt in ("s0", "s3"):
            prog_obe = os.path.join(work, "user_%s.obe" % opt)
            run([obc, "-src", "user.obs", "-lib", "collect,boxlib", "-opt", opt, "-dest", prog_obe],
                work, env, "program build (-opt %s)" % opt)
            non_empty(prog_obe, "program build (-opt %s)" % opt)

            for mode, extra in MODES:
                mode_env = dict(env)
                mode_env.update(extra)
                out = run([obr, prog_obe], work, mode_env, "program run (-opt %s, %s)" % (opt, mode))
                lines = [l.strip() for l in out.splitlines() if l.strip()]
                if lines != EXPECTED:
                    fail("wrong output (-opt %s, %s)\n  expected: %s\n  got:      %s" % (opt, mode, EXPECTED, lines))

        out = run([obc, "-src", "bad.obs", "-lib", "collect,boxlib", "-dest", os.path.join(work, "bad.obe")],
                  work, env, "an ambiguous boxed call", expect_ok=False)
        if BAD_MESSAGE not in out:
            fail("an ambiguous boxed call: expected '%s'\n%s" % (BAD_MESSAGE, out))
        for name in BAD_NAMES:
            if name not in out:
                fail("an ambiguous boxed call: expected it to name '%s'\n%s" % (name, out))
        if out.count("bad.obs:(") != 1:
            fail("an ambiguous boxed call: expected one error\n%s" % out)

    print("PASS: boxing and unboxing choose among a library's overloads (two opt levels, three modes)")
    return 0


if __name__ == "__main__":
    sys.exit(main())
