#!/usr/bin/env python3
"""
Deeply nested source is compiled or refused, never a crash.

Usage: check_nesting_limit.py <bin_dir>

<bin_dir> holds obc; the libraries are read from the sibling lib/ directory.

The parser is recursive, and nesting past what the stack can hold used to end obc with a stack overflow
(0xC00000FD on Windows, a segmentation fault elsewhere). It is now the error "Nesting is too deep". Where
that error lands is not a fixed depth: the limit is a stack budget, so the level at which it trips moves
with the platform, the compiler and the size of the parser's stack frames, which change whenever the parser
does. This test therefore pins no level and no column. For each shape of nesting (parentheses, blocks,
calls, array subscripts, unary operators, generic types) at 100, 250, 500 and 5000 levels it requires that

  * obc answers within the time limit,
  * it either compiles the program or refuses it (an exit status obc gives on purpose; never a signal or
    an access violation),
  * a refusal says "Nesting is too deep", on the line the nesting is on, once,
  * 100 levels of parentheses and of blocks compile (ordinary generated code nests that far),
  * and, for each shape, once a depth is refused every greater depth is refused too.

bad_nesting_too_deep.obs holds the same error in the regression suite for one shape; it also names no column.
"""
import os
import subprocess
import sys
import tempfile
import time

EXE = ".exe" if os.name == "nt" else ""
LEVELS = [100, 250, 500, 5000]
TIME_LIMIT = 120
MESSAGE = "Nesting is too deep"


def parentheses(n):
    return "x := " + "(" * n + "1" + ")" * n + ";\n\t\tx->PrintLine();", 3


def blocks(n):
    return "x := 0;\n\t\t" + "if(x = 0) { " * n + "x += 1; " + "}; " * n + "\n\t\tx->PrintLine();", 4


def calls(n):
    return "x := " + "Id(" * n + "1" + ")" * n + ";\n\t\tx->PrintLine();", 3


def subscripts(n):
    return "a := Int->New[2];\n\t\tx := " + "a[" * n + "0" + "]" * n + ";\n\t\tx->PrintLine();", 4


def unary(n):
    return "x := " + "-(" * n + "1" + ")" * n + ";\n\t\tx->PrintLine();", 3


def generics(n):
    return "v : " + "Vector<" * n + "IntRef" + ">" * n + ";\n\t\t(v = Nil)->PrintLine();", 3


SHAPES = [("parentheses", parentheses, True), ("blocks", blocks, True), ("calls", calls, False), ("array subscripts", subscripts, False),
          ("unary operators", unary, False), ("generic types", generics, False)]


def program(body):
    return ("use Collection;\n\nclass Nest {\n\tfunction : Id(v : Int) ~ Int {\n\t\treturn v;\n\t}\n\n"
            "\tfunction : Main(args : String[]) ~ Nil {\n\t\t" + body + "\n\t}\n}\n")


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

    with tempfile.TemporaryDirectory(prefix="nesting-") as work:
        for shape, build, shallow_compiles in SHAPES:
            refused_at = None
            for levels in LEVELS:
                body, line_in_main = build(levels)
                src = os.path.join(work, "nest.obs")
                with open(src, "w", encoding="utf-8") as f:
                    f.write(program(body))
                obe = os.path.join(work, "nest.obe")
                if os.path.exists(obe):
                    os.remove(obe)
                label = "%s, %d levels" % (shape, levels)
                start = time.time()
                try:
                    p = subprocess.run([obc, "-src", src, "-dest", obe, "-lib", "gen_collect"], cwd=work, env=env, capture_output=True, timeout=TIME_LIMIT)
                except subprocess.TimeoutExpired:
                    check("%s: answered within %d s" % (label, TIME_LIMIT), False)
                    continue
                elapsed = time.time() - start
                out = (p.stdout + p.stderr).decode("utf-8", "replace")
                # obc exits 0 (compiled) or with a small status (refused); anything else is a crash
                if not 0 <= p.returncode <= 9:
                    check("%s: obc answers (it ended with status %d)" % (label, p.returncode), False, out)
                    continue
                if p.returncode == 0:
                    check("%s: compiled (%.1f s)" % (label, elapsed), os.path.exists(obe) and refused_at is None,
                          "compiled although %s levels were refused" % refused_at)
                else:
                    errors = [l for l in out.splitlines() if ".obs:(" in l and "): Warning:" not in l]
                    # the line of the first nested statement in Main, which starts on line 9 of the program
                    line = 8 + line_in_main - 2
                    said = [l for l in errors if MESSAGE in l]
                    check("%s: refused with '%s' on line %d, once (%.1f s)" % (label, MESSAGE, line, elapsed),
                          len(said) == 1 and len(errors) == 1 and (":(%d," % line) in said[0] and not os.path.exists(obe), out)
                    if refused_at is None:
                        refused_at = levels
                    if shallow_compiles and levels == 100:
                        check("%s: 100 levels compile" % shape, False, out)
            check("%s: 5000 levels are refused" % shape, refused_at is not None)

    if failures:
        print("FAIL: %d check(s) failed" % len(failures))
        return 1
    print("PASS: deep nesting is compiled or refused, never a crash")
    return 0


if __name__ == "__main__":
    sys.exit(main())
