#!/usr/bin/env python3
"""
Type arguments are inferred for a generic class that lives in a library.

Usage: check_lib_generic_inference.py <bin_dir>

<bin_dir> must contain obc and obr (with .exe on Windows); the libraries are
read from the sibling lib/ directory, as check_lib_generic_chain.py does.

core_generic_inference.obs covers classes compiled with the program and the
Collection classes. A library class is read back from its .obl: its
constructors' parameter types and its type parameters' bounds come from the
signatures the library records, and a second library can declare a class of
the same short name. This builds two libraries and checks, against them:

  - inference from a constructor's arguments: an unbounded parameter, a
    bounded one ('Ranked<T : Compare>'), a parameter inside another generic
    ('Wrapper<T>' built from a 'Vector<T>'), a primitive held by its class
  - inference from the declared target, for a library class
  - two libraries that each have a 'Holder<T>': the class the call names
    (by its bundle) is the one that is built, and its type argument is the
    argument's own class, not a class of the same short name elsewhere
  - a library class used without type arguments where nothing settles them,
    a conflict and a broken bound are still refused, each on its line

Each program runs three ways: the default, the JIT on every call and the
interpreter alone.
"""

import os
import subprocess
import sys
import tempfile

EXE = ".exe" if os.name == "nt" else ""
TIMEOUT = 180

LIBRARY_ONE = """use Collection;

bundle InferOne {
  class Item {
    @name : String;

    New(name : String) {
      @name := name;
    }

    method : public : Name() ~ String {
      return @name;
    }
  }

  class Holder<T> {
    @value : T;

    New(value : T) {
      @value := value;
    }

    method : public : Get() ~ T {
      return @value;
    }

    method : public : Where() ~ String {
      return "one";
    }
  }

  class Ranked<T : Compare> {
    @best : T;

    New(first : T) {
      @best := first;
    }

    method : public : Offer(v : T) ~ Ranked<T> {
      if(v->Compare(@best) > 0) {
        @best := v;
      };
      return @self;
    }

    method : public : Best() ~ T {
      return @best;
    }
  }

  class Wrapper<T> {
    @values : Vector<T>;

    New(values : Vector<T>) {
      @values := values;
    }

    method : public : First() ~ T {
      return @values->Get(0);
    }
  }

  class Registry<K : Compare, V> {
    @entries : Map<K, V>;

    New() {
      @entries := Map->New()<K, V>;
    }

    method : public : Put(k : K, v : V) ~ Nil {
      @entries->Insert(k, v);
    }

    method : public : Get(k : K) ~ V {
      return @entries->Find(k);
    }
  }
}
"""

LIBRARY_TWO = """bundle InferTwo {
  class Item {
    @id : Int;

    New(id : Int) {
      @id := id;
    }

    method : public : Id() ~ Int {
      return @id;
    }
  }

  class Holder<T> {
    @value : T;

    New(value : T) {
      @value := value;
    }

    method : public : Get() ~ T {
      return @value;
    }

    method : public : Where() ~ String {
      return "two";
    }
  }
}
"""

PROGRAM = """use Collection;

class User {
  function : MakeRegistry() ~ InferOne.Registry<String, IntRef> {
    return InferOne.Registry->New();
  }

  function : Main(args : String[]) ~ Nil {
    # from the arguments
    text := InferOne.Holder->New("text");
    text->Get()->PrintLine();

    number := InferOne.Holder->New(41);
    (number->Get() + 1)->PrintLine();

    ranked := InferOne.Ranked->New(IntRef->New(3));
    ranked->Offer(IntRef->New(9))->Offer(IntRef->New(4));
    ranked->Best()->Get()->PrintLine();

    words := Vector->New()<String>;
    words->AddBack("first");
    wrapper := InferOne.Wrapper->New(words);
    wrapper->First()->PrintLine();

    # from the declared target
    registry : InferOne.Registry<String, IntRef> := InferOne.Registry->New();
    registry->Put("k", 5);
    registry->Get("k")->Get()->PrintLine();

    made := MakeRegistry();
    made->Put("m", 6);
    made->Get("m")->Get()->PrintLine();

    # two libraries, one short name: the class and the type argument are the ones named
    one := InferOne.Holder->New(InferOne.Item->New("item one"));
    one->Where()->PrintLine();
    one->Get()->Name()->PrintLine();

    two := InferTwo.Holder->New(InferTwo.Item->New(22));
    two->Where()->PrintLine();
    two->Get()->Id()->PrintLine();

    crossed := InferOne.Holder->New(InferTwo.Item->New(33));
    crossed->Where()->PrintLine();
    crossed->Get()->Id()->PrintLine();
  }
}
"""

EXPECTED = ["text", "42", "9", "first", "5", "6", "one", "item one", "two", "22", "one", "33"]

# (name, source, the line and message the compiler must report)
BAD = [
    ("nothing settles the type arguments", """class Bad {
  function : Main(args : String[]) ~ Nil {
    registry := InferOne.Registry->New();
    registry->GetClassID()->PrintLine();
  }
}
""", "bad.obs:(3,17): Generic type mismatch for class 'InferOne.Registry': expected 2 type parameter(s), got 0"),
    ("a broken bound", """class Plain {
  New() { }
}

class Bad {
  function : Main(args : String[]) ~ Nil {
    ranked := InferOne.Ranked->New(Plain->New());
    ranked->GetClassID()->PrintLine();
  }
}
""", "bad.obs:(7,15): Concrete class: 'Plain' is incompatible with backing class/interface 'System.Compare'"),
    ("Nil for a type parameter", """class Bad {
  function : Main(args : String[]) ~ Nil {
    holder := InferOne.Holder->New(Nil);
    holder->GetClassID()->PrintLine();
  }
}
""", "bad.obs:(3,15): Cannot infer the type arguments of 'InferOne.Holder': no argument gives type parameter 'T' a class"),
]

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
        # obc resolves -lib against OBJECK_LIB_PATH, so the generated libraries
        # go into a private copy of lib/ rather than the real deploy tree.
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

        sources = {"inferone.obs": LIBRARY_ONE, "infertwo.obs": LIBRARY_TWO, "user.obs": PROGRAM}
        for name, text in sources.items():
            with open(os.path.join(work, name), "w") as handle:
                handle.write(text)

        for name in ("inferone", "infertwo"):
            lib_obl = os.path.join(private_lib, name + ".obl")
            run([obc, "-src", os.path.join(work, name + ".obs"), "-tar", "lib", "-lib", "collect", "-dest", lib_obl],
                work, env, "library build (%s)" % name)
            non_empty(lib_obl, "library build (%s)" % name)

        libs = "collect,inferone,infertwo"
        for opt in ("s0", "s3"):
            prog_obe = os.path.join(work, "user_%s.obe" % opt)
            run([obc, "-src", os.path.join(work, "user.obs"), "-lib", libs, "-opt", opt, "-dest", prog_obe],
                work, env, "program build (-opt %s)" % opt)
            non_empty(prog_obe, "program build (-opt %s)" % opt)

            for mode, extra in MODES:
                mode_env = dict(env)
                mode_env.update(extra)
                out = run([obr, prog_obe], work, mode_env, "program run (-opt %s, %s)" % (opt, mode))
                lines = [l.strip() for l in out.splitlines() if l.strip()]
                if lines != EXPECTED:
                    fail("wrong output (-opt %s, %s)\n  expected: %s\n  got:      %s" % (opt, mode, EXPECTED, lines))

        for what, text, message in BAD:
            with open(os.path.join(work, "bad.obs"), "w") as handle:
                handle.write(text)
            out = run([obc, "-src", "bad.obs", "-lib", libs, "-dest", os.path.join(work, "bad.obe")],
                      work, env, what, expect_ok=False)
            if message not in out:
                fail("%s: expected '%s'\n%s" % (what, message, out))

    print("PASS: type arguments are inferred for library generic classes (two opt levels, three modes, %d refusals)" % len(BAD))
    return 0


if __name__ == "__main__":
    sys.exit(main())
