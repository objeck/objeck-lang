#!/usr/bin/env python3
"""
obc and obr find their libraries relative to the executable, not the working
directory, when OBJECK_LIB_PATH is not set.

Usage: check_lib_path_anchor.py <bin_dir>

GetLibraryPath() used to answer "../lib/" when OBJECK_LIB_PATH was unset, which
the OS resolves against the process's CURRENT directory: obc or obr started from
anywhere but <install>/bin could not find lang.obl, the native libraries or the
CA bundle. It now resolves <exe dir>/../lib/ once at startup.

Checks, all with OBJECK_LIB_PATH removed from the environment and the working
directory set to an unrelated empty directory:
  - obc compiles a program (it must find lang.obl)
  - obr runs it and Runtime->GetLibraryDir() is this install's lib directory
  - with OBJECK_LIB_PATH set, that value still wins (the override is kept)
"""
import os, subprocess, sys, tempfile

EXE = ".exe" if os.name == "nt" else ""
SOURCE = """class LibPathProbe {
\tfunction : Main(args : String[]) ~ Nil {
\t\tRuntime->GetLibraryDir()->PrintLine();
\t}
}
"""


def run(cmd, env, cwd):
    p = subprocess.run(cmd, cwd=cwd, env=env, capture_output=True, timeout=120)
    return p.returncode, (p.stdout + p.stderr).decode("utf-8", "replace")


def norm(path):
    return os.path.normcase(os.path.realpath(path.strip())).rstrip(chr(92) + "/")


def main():
    if len(sys.argv) != 2:
        print(__doc__)
        return 2
    bin_dir = os.path.abspath(sys.argv[1])
    obc = os.path.join(bin_dir, "obc" + EXE)
    obr = os.path.join(bin_dir, "obr" + EXE)
    lib_dir = os.path.join(os.path.dirname(bin_dir), "lib")
    failures = []

    def check(name, cond, detail=""):
        print("  [%s] %s%s" % ("PASS" if cond else "FAIL", name, "" if cond else "\n         " + detail[-500:]))
        if not cond:
            failures.append(name)

    env = dict(os.environ)
    env.pop("OBJECK_LIB_PATH", None)
    with tempfile.TemporaryDirectory(prefix="lib-anchor-") as top:
        elsewhere = os.path.join(top, "elsewhere", "deeper")
        os.makedirs(elsewhere)
        check("the probe directory has no ../lib of its own", not os.path.exists(os.path.join(elsewhere, "..", "lib")))
        src = os.path.join(elsewhere, "probe.obs")
        with open(src, "w") as h:
            h.write(SOURCE)
        dest = os.path.join(elsewhere, "probe.obe")
        code, out = run([obc, "-src", src, "-dest", dest], env, elsewhere)
        check("obc started from another directory finds its libraries", code == 0 and os.path.isfile(dest), out)
        if code == 0:
            code, out = run([obr, dest], env, elsewhere)
            check("obr started from another directory runs the program", code == 0, out)
            check("the library directory is this install's lib (got %r)" % out.strip(),
                  norm(out) == norm(lib_dir), "expected %s" % lib_dir)
            override = os.path.join(top, "override")
            os.makedirs(override)
            e2 = dict(env)
            e2["OBJECK_LIB_PATH"] = override
            code, out = run([obr, dest], e2, elsewhere)
            check("OBJECK_LIB_PATH still overrides (got %r)" % out.strip(), norm(out) == norm(override), out)
    print("\n%d failure(s)" % len(failures))
    return 1 if failures else 0


if __name__ == "__main__":
    sys.exit(main())
