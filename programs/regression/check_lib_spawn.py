#!/usr/bin/env python3
"""
'spawn { ... }' in a library, called from a program that uses 'spawn' as well.

Usage: check_lib_spawn.py <bin_dir>

<bin_dir> must contain obc and obr (with .exe on Windows); the libraries are
read from the sibling lib/ directory, as check_lib_generic_chain.py does.

A 'spawn' is written out by the parser as an anonymous Thread class and a
lambda, both named for the source file and position they were written at.
In a library the class and the lambda are stored in the .obl and loaded by a
program that has classes of its own made the same way. This builds a library
whose methods and functions start threads, and a program that calls them and
starts threads itself, and checks:

  - a thread started by an instance method of a library class, sharing the
    object's state through captured locals, summed under a mutex
  - a thread started by a library function, returned to the program and joined
  - the program's own 'spawn' beside the library's
  - a library source file and a program source file of the same name
    ('work.obs'), each with a 'spawn' at the SAME line and column (in
    'Twin->Run' and 'Same->Run'): the classes made for the two are named for
    the class they are written in as well, so each thread runs its own block
  - the thread's name, 'spawn:<file>:<line>'
  - a 'spawn' stored as an Int is refused, on its line

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
use System.Concurrency;
bundle SpawnLib {
  class Twin {
    function : Run(out : Vector<String>) ~ Thread {
      t := spawn { out->AddBack("the library's block"); };
      return t;
    }
  }

  class Pool {
    @workers : Vector<Thread>;
    @total : IntRef;
    @mutex : ThreadMutex;

    New() {
      @workers := Vector->New()<Thread>;
      @total := IntRef->New(0);
      @mutex := ThreadMutex->New("pool");
    }

    method : public : Add(amount : Int) ~ Nil {
      total := @total;
      mutex := @mutex;
      @workers->AddBack(spawn {
        critical(mutex) {
          total->Set(total->Get() + amount);
        };
      });
    }

    method : public : Finish() ~ Int {
      each(worker in @workers) {
        worker->Join();
      };
      return @total->Get();
    }

    function : Start(text : String, out : Vector<String>) ~ Thread {
      return spawn {
        out->AddBack(text->ToUpper());
      };
    }
  }
}
"""

PROGRAM = """use Collection;
use System.Concurrency;
use SpawnLib;
  class Same {
    function : Run(out : Vector<String>) ~ Thread {
      t := spawn { out->AddBack("the program's block"); };
      return t;
    }
  }

class User {
  function : Main(args : String[]) ~ Nil {
    pool := Pool->New();
    for(i := 1; i <= 10; i += 1;) {
      pool->Add(i);
    };
    pool->Finish()->PrintLine();

    out := Vector->New()<String>;
    Pool->Start("from a library", out)->Join();
    out->Get(0)->PrintLine();

    mine := spawn { out->AddBack("and the program"); };
    mine->Join();
    out->Get(1)->PrintLine();

    Twin->Run(out)->Join();
    out->Get(2)->PrintLine();
    Same->Run(out)->Join();
    out->Get(3)->PrintLine();
    mine->GetName()->PrintLine();
  }
}
"""

EXPECTED = ["55", "FROM A LIBRARY", "and the program", "the library's block", "the program's block", "spawn:work.obs:23"]

BAD_PROGRAM = """class Bad {
  function : Main(args : String[]) ~ Nil {
    count : Int := spawn { "x"->PrintLine(); };
    count->PrintLine();
  }
}
"""

BAD_MESSAGE = "work.obs:(3,20): Invalid cast with classes: System.Int and System.Concurrency.Thread"

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

        # the library and the program are both 'work.obs', in directories of their own
        lib_src = os.path.join(work, "lib_src")
        prog_src = os.path.join(work, "prog_src")
        bad_src = os.path.join(work, "bad_src")
        for directory, text in ((lib_src, LIBRARY), (prog_src, PROGRAM), (bad_src, BAD_PROGRAM)):
            os.mkdir(directory)
            with open(os.path.join(directory, "work.obs"), "w") as handle:
                handle.write(text)

        lib_obl = os.path.join(private_lib, "spawnlib.obl")
        run([obc, "-src", "work.obs", "-tar", "lib", "-lib", "collect", "-opt", "s3", "-dest", lib_obl],
            lib_src, env, "library build")
        non_empty(lib_obl, "library build")

        for opt in ("s0", "s3"):
            prog_obe = os.path.join(work, "user_%s.obe" % opt)
            run([obc, "-src", "work.obs", "-lib", "collect,spawnlib", "-opt", opt, "-dest", prog_obe],
                prog_src, env, "program build (-opt %s)" % opt)
            non_empty(prog_obe, "program build (-opt %s)" % opt)

            for mode, extra in MODES:
                mode_env = dict(env)
                mode_env.update(extra)
                out = run([obr, prog_obe], work, mode_env, "program run (-opt %s, %s)" % (opt, mode))
                lines = [l.strip() for l in out.splitlines() if l.strip()]
                if lines != EXPECTED:
                    fail("wrong output (-opt %s, %s)\n  expected: %s\n  got:      %s" % (opt, mode, EXPECTED, lines))

        out = run([obc, "-src", "work.obs", "-lib", "collect,spawnlib", "-dest", os.path.join(work, "bad.obe")],
                  bad_src, env, "a spawn stored as an Int", expect_ok=False)
        if BAD_MESSAGE not in out:
            fail("a spawn stored as an Int: expected '%s'\n%s" % (BAD_MESSAGE, out))

    print("PASS: 'spawn' in a library and in the program that uses it (two opt levels, three modes)")
    return 0


if __name__ == "__main__":
    sys.exit(main())
