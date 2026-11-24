#!/usr/bin/env python3
"""
obc refuses to emit code when a jump label is lost or duplicated, instead of
reading a dangling map iterator (undefined behaviour in release builds) or
silently jumping to instruction 0.

Usage: check_label_fatal.py <bin_dir>

Source cannot produce a lost or doubled label, so the compiler has a test hook:
OBJECK_TEST_LABEL_FAULT=drop|dup makes the label pass lose/duplicate the first
label of each method. A program with a loop must then fail to compile with a
clear "Internal compiler error" message and a non-zero exit, and must compile
and run normally without the hook.
"""
import os, subprocess, sys, tempfile

EXE = ".exe" if os.name == "nt" else ""
SOURCE = """class LabelProbe {
\tfunction : Main(args : String[]) ~ Nil {
\t\ti := 0;
\t\twhile(i < 3) { i += 1; };
\t\t"PROBE RAN"->PrintLine();
\t}
}
"""

def run(cmd, env, cwd):
    p = subprocess.run(cmd, cwd=cwd, env=env, capture_output=True, timeout=120)
    return p.returncode, (p.stdout + p.stderr).decode("utf-8", "replace")

def main():
    if len(sys.argv) != 2:
        print(__doc__); return 2
    bin_dir = os.path.abspath(sys.argv[1])
    obc = os.path.join(bin_dir, "obc" + EXE)
    obr = os.path.join(bin_dir, "obr" + EXE)
    lib_dir = os.path.join(os.path.dirname(bin_dir), "lib")
    env = dict(os.environ)
    env["OBJECK_LIB_PATH"] = lib_dir
    failures = []
    def check(name, cond, detail=""):
        print("  [%s] %s%s" % ("PASS" if cond else "FAIL", name, "" if cond else "\n         " + detail))
        if not cond: failures.append(name)
    with tempfile.TemporaryDirectory(prefix="label-fatal-") as work:
        src = os.path.join(work, "probe.obs")
        with open(src, "w") as h: h.write(SOURCE)
        dest = os.path.join(work, "probe.obe")
        code, out = run([obc, "-src", src, "-dest", dest], env, work)
        check("control: compiles", code == 0 and os.path.isfile(dest), out)
        code, out = run([obr, dest], env, work)
        check("control: runs", code == 0 and "PROBE RAN" in out, out)
        for fault, text in (("drop", "unresolved jump label"), ("dup", "duplicate jump label")):
            dest = os.path.join(work, fault + ".obe")
            e = dict(env); e["OBJECK_TEST_LABEL_FAULT"] = fault
            code, out = run([obc, "-src", src, "-dest", dest], e, work)
            check("%s: non-zero exit, no crash" % fault, 0 < code < 128, "exit=%d out=%r" % (code, out))
            check("%s: message says '%s'" % (fault, text), text in out, out)
            check("%s: no .obe written" % fault, not os.path.isfile(dest))
    print("\n%d failure(s)" % len(failures))
    return 1 if failures else 0

if __name__ == "__main__":
    sys.exit(main())
