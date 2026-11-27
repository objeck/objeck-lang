#!/usr/bin/env python3
"""
obc answers large generated programs within a ceiling: no shape of source makes compile time explode.

Usage: check_compile_time_ceiling.py <bin_dir>

<bin_dir> holds obc; the libraries are read from the sibling lib/ directory.

Each case is a program of one repeated shape at a size no hand-written program reaches. obc must answer it
(compile it, or refuse it with an error where the shape runs into a documented limit) within CEILING seconds.
The ceilings are generous on purpose: this is not a benchmark and must not fail on a slow or busy machine.
On the machine the cases were sized on every one answers in under a second; the ceiling is 30 times that or
more. What the test catches is a change that makes a shape quadratic, which at these sizes means minutes.

The case that found a bug: a method with more interpolated strings than its local space allows is refused
("Local space has been exceeded"), and the refusal took time quadratic in the number of strings, because each
string's hidden local was named by probing upward from 0 (4,000 strings: 1.6 s; 16,000: 73 s, and 78 s for
16,000 '+' concatenations, which name their local the same way). The names are now found by bisection. The
test holds both shapes at 16,000.

Nested array subscripts are not here: check_nesting_limit.py holds the nesting shapes.
"""
import os
import subprocess
import sys
import tempfile
import time

EXE = ".exe" if os.name == "nt" else ""
CEILING = 30


def program(body, extra_classes=""):
    return ("use Collection;\n\n" + extra_classes + "class Gen {\n\tfunction : Main(args : String[]) ~ Nil {\n\t\t" + body + "\n\t}\n}\n")


def cases():
    out = []

    # (name, source, libraries, must compile)
    n = 16000
    body = "a := 1; b := \"two\";\n\t\tx := 0;\n\t\t" + "\n\t\t".join('x += "n%d a={$a} b={$b}"->Size();' % i for i in range(n)) + "\n\t\tx->PrintLine();"
    out.append(("16,000 interpolated strings in one method (refused: local space)", program(body), "", False))

    n = 16000
    body = "a := \"one\"; b := \"two\";\n\t\tx := 0;\n\t\t" + "\n\t\t".join("x += (a + b + \"%d\")->Size();" % i for i in range(n)) + "\n\t\tx->PrintLine();"
    out.append(("16,000 string concatenations in one method (refused: local space)", program(body), "", False))

    n = 400
    methods = "class Ip {\n" + "".join("\tfunction : F%d(a : Int, b : String) ~ Int {\n\t\tx := 0;\n\t\t%s\n\t\treturn x;\n\t}\n" % (
        i, " ".join('x += "n%d a={$a} b={$b}"->Size();' % j for j in range(20))) for i in range(n)) + "}\n\n"
    out.append(("8,000 interpolated strings over 400 methods", program('Ip->F0(1, "two")->PrintLine();', methods), "", True))

    n = 2000
    classes = "".join("class C%d {\n\t@v : Int;\n\tNew(v : Int) { @v := v; }\n\tmethod : public : Get() ~ Int { return @v + %d; }\n"
                      "\tmethod : public : Next() ~ C%d { return C%d->New(@v); }\n}\n\n" % (i, i, (i + 1) % n, (i + 1) % n) for i in range(n))
    out.append(("2,000 classes", program("C0->New(1)->Next()->Get()->PrintLine();", classes), "", True))

    n = 8000
    methods = "class M {\n" + "".join("\tfunction : F%d(a : Int) ~ Int { return a + %d; }\n" % (i, i) for i in range(n)) + "}\n\n"
    out.append(("8,000 methods in one class", program("M->F1(M->F0(1))->PrintLine();", methods), "", True))

    n = 20000
    out.append(("20,000 statements in one method", program("x := 0;\n\t\t" + "\n\t\t".join("x += %d;" % i for i in range(n)) + "\n\t\tx->PrintLine();"), "", True))

    n = 8000
    methods = "class O {\n"
    for count in range(1, 7):
        for kind in ("Int", "Float"):
            methods += "\tfunction : F(%s) ~ Int { return %d; }\n" % (", ".join("a%d : %s" % (j, kind) for j in range(count)), count)
    methods += "}\n\n"
    body = "x := 0;\n\t\t" + "\n\t\t".join("x += O->F(%s);" % ", ".join(["1.5" if i % 2 else "1"] * (1 + i % 6)) for i in range(n)) + "\n\t\tx->PrintLine();"
    out.append(("8,000 calls to a name with twelve overloads", program(body, methods), "", True))

    n = 4000
    body = "v := Vector->New()<String>;\n\t\t" + "\n\t\t".join('v->AddBack("s%d");' % i for i in range(n)) + "\n\t\tv->Size()->PrintLine();"
    out.append(("4,000 calls on a generic collection", program(body), "gen_collect", True))

    n = 40000
    out.append(("an Int array literal of 40,000 elements", program("x := [" + ", ".join(str(i) for i in range(n)) + "];\n\t\tx->Size()->PrintLine();"), "", True))

    n = 10000
    out.append(("a String array literal of 10,000 elements", program("x := [" + ", ".join('"s%d"' % i for i in range(n)) + "];\n\t\tx->Size()->PrintLine();"), "", True))

    n = 1000000
    out.append(("a string literal of 1,000,000 characters", program('x := "' + "abcdefghij" * (n // 10) + '";\n\t\tx->Size()->PrintLine();'), "", True))

    n = 10000
    out.append(("10,000 distinct string literals", program("x := 0;\n\t\t" + "\n\t\t".join('x += "string number %d"->Size();' % i for i in range(n)) + "\n\t\tx->PrintLine();"), "", True))

    n = 1600
    out.append(("one string with 1,600 interpolations", program('a := 1;\n\t\tx := "' + "v={$a};" * n + '";\n\t\tx->Size()->PrintLine();'), "", True))

    n = 2000
    sel = "x := args->Size();\n\t\ty := 0;\n\t\tselect(x) {" + "".join("\n\t\t\tlabel %d: { y := %d; }" % (i, i + 1) for i in range(n))
    sel += "\n\t\t};\n\t\ty->PrintLine();"
    out.append(("a select of 2,000 labels", program(sel), "", True))

    n = 1800
    out.append(("1,800 chained additions", program("a := 1;\n\t\tx := " + " + ".join(["a"] * n) + ";\n\t\tx->PrintLine();"), "", True))

    return out


def main():
    if len(sys.argv) != 2:
        print(__doc__)
        return 2
    bin_dir = os.path.abspath(sys.argv[1])
    obc = os.path.join(bin_dir, "obc" + EXE)
    env = dict(os.environ)
    env["OBJECK_LIB_PATH"] = os.path.join(os.path.dirname(bin_dir), "lib")
    failures = []

    def check(name, cond, detail=""):
        print("  [%s] %s%s" % ("PASS" if cond else "FAIL", name, "" if cond else "\n         " + str(detail)[-500:]))
        if not cond:
            failures.append(name)

    with tempfile.TemporaryDirectory(prefix="ceiling-") as work:
        for name, text, libs, must_compile in cases():
            src = os.path.join(work, "gen.obs")
            with open(src, "w", encoding="utf-8", newline="\n") as f:
                f.write(text)
            obe = os.path.join(work, "gen.obe")
            if os.path.exists(obe):
                os.remove(obe)
            cmd = [obc, "-src", src, "-dest", obe] + (["-lib", libs] if libs else [])
            start = time.time()
            try:
                p = subprocess.run(cmd, cwd=work, env=env, capture_output=True, timeout=CEILING)
            except subprocess.TimeoutExpired:
                check("%s: answered within %d s" % (name, CEILING), False)
                continue
            elapsed = time.time() - start
            out = (p.stdout + p.stderr).decode("utf-8", "replace")
            # obc exits 0 (compiled) or with a small status (refused); anything else is a crash
            if not 0 <= p.returncode <= 9:
                check("%s: obc answers (it ended with status %d)" % (name, p.returncode), False, out)
            elif must_compile:
                check("%s: compiled (%.1f s)" % (name, elapsed), p.returncode == 0 and os.path.exists(obe), out)
            else:
                check("%s: refused (%.1f s)" % (name, elapsed), p.returncode != 0 and not os.path.exists(obe), out)

    if failures:
        print("FAIL: %d check(s) failed" % len(failures))
        return 1
    print("PASS: every generated shape is answered within %d s" % CEILING)
    return 0


if __name__ == "__main__":
    sys.exit(main())
