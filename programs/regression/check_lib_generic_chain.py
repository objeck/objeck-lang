#!/usr/bin/env python3
"""
A chain of calls on a library generic class keeps the class's type arguments.

Usage: check_lib_generic_chain.py <bin_dir>

<bin_dir> must contain obc and obr (with .exe on Windows); the libraries are
read from the sibling lib/ directory, as check_lib_named_args.py does.

A library class 'ObjectSource<T>' whose AddInt(name, reader : (T) ~ Int)
returns @self as an ObjectSource<T> is a fluent builder. Inside one program,
'source->AddString(..)->AddInt(..)' compiled; against the library, the second
call failed "Undefined class or interface: 'T'". The chained call has no
variable of its own, and its type arguments were read from the raw return type
the library records, 'ObjectSource<T>', instead of the receiver's,
ObjectSource<Person>, which the first call hands it. Covered: an unbounded and
a bounded class, a chain of three, and a chained call given a function of the
wrong type, which must still be refused. Each program runs three ways: the
default, the JIT on every call and the interpreter alone.
"""

import os
import subprocess
import sys
import tempfile

EXE = ".exe" if os.name == "nt" else ""
TIMEOUT = 180

LIBRARY = """use Collection;

bundle ChainLib {
  class ObjectSource<T> {
    @names : Vector<String>;

    New() {
      @names := Vector->New()<String>;
    }

    method : public : AddInt(name : String, reader : (T) ~ Int) ~ ObjectSource<T> {
      @names->AddBack(name);
      return @self;
    }

    method : public : AddString(name : String, reader : (T) ~ String) ~ ObjectSource<T> {
      @names->AddBack(name);
      return @self;
    }

    method : public : AddFloat(name : String, reader : (T) ~ Float) ~ ObjectSource<T> {
      @names->AddBack(name);
      return @self;
    }

    method : public : Names() ~ String {
      out := "";
      each(i : @names) {
        out += @names->Get(i);
      };
      return out;
    }
  }

  class Ranked<T : Compare> {
    @best : T;

    New() {}

    method : public : Offer(v : T) ~ Ranked<T> {
      if(@best = Nil | v->Compare(@best) > 0) {
        @best := v;
      };
      return @self;
    }

    method : public : Best() ~ T {
      return @best;
    }
  }
}
"""

PROGRAM = """use ChainLib;

class Person {
  @name : String;
  @age : Int;

  New(name : String, age : Int) {
    @name := name;
    @age := age;
  }

  method : public : GetName() ~ String {
    return @name;
  }

  method : public : GetAge() ~ Int {
    return @age;
  }
}

class User {
  function : Main(args : String[]) ~ Nil {
    source := ObjectSource->New()<Person>;
    source->AddString("n", \\(Person) ~ String : (p) => p->GetName())
      ->AddInt("a", \\(Person) ~ Int : (p) => p->GetAge())
      ->AddFloat("h", \\(Person) ~ Float : (p) => 1.5);
    source->Names()->PrintLine();

    ObjectSource->New()<Person>->AddInt("x", \\(Person) ~ Int : (p) => 0)->AddInt("y", \\(Person) ~ Int : (p) => 1)->Names()->PrintLine();

    r := Ranked->New()<IntRef>;
    r->Offer(IntRef->New(3))->Offer(IntRef->New(9))->Offer(IntRef->New(4));
    r->Best()->Get()->PrintLine();
  }
}
"""

EXPECTED = ["nah", "xy", "9"]

BAD_PROGRAM = """use ChainLib;

class Bad {
  function : Main(args : String[]) ~ Nil {
    source := ObjectSource->New()<String>;
    source->AddInt("a", \\(String) ~ Int : (s) => s->Size())->AddInt("b", \\(IntRef) ~ Int : (i) => i->Get());
  }
}
"""

BAD_MESSAGE = "Undefined function/method call: 'AddInt(..)'"

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

        sources = {"chainlib.obs": LIBRARY, "user.obs": PROGRAM, "bad.obs": BAD_PROGRAM}
        for name, text in sources.items():
            with open(os.path.join(work, name), "w") as handle:
                handle.write(text)

        lib_obl = os.path.join(private_lib, "chainlib.obl")
        prog_obe = os.path.join(work, "user.obe")

        run([obc, "-src", os.path.join(work, "chainlib.obs"), "-tar", "lib", "-lib", "collect", "-dest", lib_obl],
            work, env, "library build")
        non_empty(lib_obl, "library build")

        run([obc, "-src", os.path.join(work, "user.obs"), "-lib", "collect,chainlib", "-dest", prog_obe],
            work, env, "program build")
        non_empty(prog_obe, "program build")

        for mode, extra in MODES:
            mode_env = dict(env)
            mode_env.update(extra)
            out = run([obr, prog_obe], work, mode_env, "program run (%s)" % mode)
            lines = [l.strip() for l in out.splitlines() if l.strip()]
            if lines != EXPECTED:
                fail("wrong output (%s)\n  expected: %s\n  got:      %s" % (mode, EXPECTED, lines))

        out = run([obc, "-src", os.path.join(work, "bad.obs"), "-lib", "collect,chainlib",
                   "-dest", os.path.join(work, "bad.obe")], work, env, "a mistyped chained call", expect_ok=False)
        if BAD_MESSAGE not in out:
            fail("a mistyped chained call: expected '%s'\n%s" % (BAD_MESSAGE, out))

    print("PASS: a chain of calls on a library generic class keeps its type arguments (three modes)")
    return 0


if __name__ == "__main__":
    sys.exit(main())
