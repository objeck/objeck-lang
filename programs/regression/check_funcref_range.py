#!/usr/bin/env python3
"""
A function reference to a method whose id is >= 32768 calls that method, and a
class or method id past 65535 is refused at compile time.

Usage: check_funcref_range.py <bin_dir>

A function value packs FUNC_VALUE_TAG | class id << 16 | method id. The compiler
used to take both ids through int16_t, so a method id >= 32768 sign-extended into
the class and tag bits and the call went to the wrong method (or crashed). The
VM already decodes the ids as unsigned 16-bit fields. The packing is now
unsigned, and an id that does not fit 16 bits is a compile error.

Case 1: 33000 methods in one class, a reference to the last one (id > 32767) is
        called and must return the right value.
Case 2: 66000 methods: a reference to id 65535 works, to id 65536 fails to compile
        with a clear message (not produce a wrong program).
"""
import os, subprocess, sys, tempfile

EXE = ".exe" if os.name == "nt" else ""

def source(count, target):
    lines = ["class FuncRefRange {"]
    for i in range(count):
        lines.append("  function : M%d(a : Int) ~ Int { return a + %d; }" % (i, i))
    lines.append("  function : Main(args : String[]) ~ Nil {")
    lines.append("    f := M%d(Int) ~ Int;" % target)
    lines.append("    r := f(1);")
    lines.append("    r->PrintLine();")
    lines.append("    if(r <> %d) { Runtime->Exit(1); };" % (target + 1))
    lines.append("  }")
    lines.append("}")
    return "\n".join(lines) + "\n"

def run(cmd, env, cwd):
    p = subprocess.run(cmd, cwd=cwd, env=env, capture_output=True, timeout=600)
    return p.returncode, (p.stdout + p.stderr).decode("utf-8", "replace")

def main():
    if len(sys.argv) != 2:
        print(__doc__); return 2
    bin_dir = os.path.abspath(sys.argv[1])
    obc = os.path.join(bin_dir, "obc" + EXE)
    obr = os.path.join(bin_dir, "obr" + EXE)
    env = dict(os.environ)
    env["OBJECK_LIB_PATH"] = os.path.join(os.path.dirname(bin_dir), "lib")
    failures = []
    def check(name, cond, detail=""):
        print("  [%s] %s%s" % ("PASS" if cond else "FAIL", name, "" if cond else "\n         " + detail[-600:]))
        if not cond: failures.append(name)
    with tempfile.TemporaryDirectory(prefix="funcref-range-") as work:
        src = os.path.join(work, "big.obs")
        with open(src, "w") as h: h.write(source(33000, 32999))
        dest = os.path.join(work, "big.obe")
        code, out = run([obc, "-src", src, "-dest", dest], env, work)
        check("33000 methods compile", code == 0 and os.path.isfile(dest), out)
        if code == 0:
            code, out = run([obr, dest], env, work)
            check("reference to method id 32999 calls it (prints 33000)", code == 0 and out.strip().endswith("33000"), "exit=%d out=%r" % (code, out))
        # the largest id that fits: method 65535 of 66000 is still callable
        with open(src, "w") as h: h.write(source(66000, 65535))
        dest = os.path.join(work, "edge.obe")
        code, out = run([obc, "-src", src, "-dest", dest], env, work)
        check("66000 methods: a reference to method id 65535 compiles", code == 0 and os.path.isfile(dest), out)
        if code == 0:
            code, out = run([obr, dest], env, work)
            check("reference to method id 65535 calls it (prints 65536)", code == 0 and out.strip().endswith("65536"), "exit=%d out=%r" % (code, out))
        # one past it cannot be encoded: a clear compile error, not a wrong program
        with open(src, "w") as h: h.write(source(66000, 65536))
        dest = os.path.join(work, "huge.obe")
        code, out = run([obc, "-src", src, "-dest", dest], env, work)
        check("reference to method id 65536: refused, non-zero exit, no crash", 0 < code < 128, "exit=%d %s" % (code, out))
        check("the message says the function reference cannot be encoded", "cannot encode" in out, out)
        check("no .obe written", not os.path.isfile(dest))
    print("\n%d failure(s)" % len(failures))
    return 1 if failures else 0

if __name__ == "__main__":
    sys.exit(main())
