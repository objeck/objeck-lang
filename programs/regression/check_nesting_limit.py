#!/usr/bin/env python3
"""
Deeply nested source is compiled or refused, never a crash.

Usage: check_nesting_limit.py <bin_dir>

<bin_dir> holds obc and obr; the libraries are read from the sibling lib/ directory.

The parser is recursive, and nesting past what the stack can hold used to end obc with a stack overflow
(0xC00000FD on Windows, a segmentation fault elsewhere). It is now the error "Nesting is too deep". Where
that error lands is not a fixed depth: the limit is a stack budget, so the level at which it trips moves
with the platform, the compiler and the size of the parser's stack frames, which change whenever the parser
does. This test therefore pins no level and no column. For each shape of nesting (parentheses, blocks,
calls, array subscripts, unary operators, generic types, array literals) at 100, 250, 500 and 5000 levels,
and for array literals at 100,000 levels as well (each '[' of '[[[[...' is a call of the parser, and that
function was the one without the check: obc ended with a segmentation fault), it requires that

  * obc answers within the time limit,
  * it either compiles the program or refuses it (an exit status obc gives on purpose; never a signal or
    an access violation),
  * a refusal says "Nesting is too deep", on the line the nesting is on, once,
  * 100 levels of parentheses and of blocks compile (ordinary generated code nests that far),
  * an 'else if' ladder of 400 rungs compiles (it is not nesting, but each rung is a call of the parser),
  * and, for each shape, once a depth is refused every greater depth is refused too.

An array literal that obc does compile is an array of as many dimensions as it has levels, and an array has
at most 8: with 9, 100 and 500 levels obr must refuse the program when it loads it, saying so; with 8 the
program runs. ('Type->New[...]' with more than 8 sizes is obc's own error, checked to be still that.)

A string interpolation ("{$expression}") is read by a parser of its own, when the string is analyzed, and
that parser had no stack mark to measure from: the check did nothing in it. Parentheses were read to any
depth, and 5,000 levels of calls (50,000 of parentheses or array literals) ended obc with a segmentation
fault. The same shapes are therefore tried inside an interpolation, at 100, 250 and 5000 levels, alone and
with a format ("{$expression:8}", which is parsed twice): each is compiled or refused, a refusal for depth
says "Nesting is too deep" once, on the string's line, 100 levels of parentheses compile and 5000 levels of
every shape are refused. (An array is not a value an interpolation can print, so a shallow array literal is
refused for that, by another message.) With a second source file in the same compilation the error is
reported and the later file is still diagnosed. An interpolation written inside an interpolation does not
nest: an interpolation ends at its first "}", so a "{$" inside one is the error "A '{$' inside '{$...}'",
once, on the string's line, for 100, 250 and 5000 of them (they used to compile, each "{$" starting the
expression again and keeping the text before it a second time: 5000 made a 25 million character literal).

bad_nesting_too_deep.obs holds the same error in the regression suite for one shape; it also names no column.
"""
import os
import subprocess
import sys
import tempfile
import time

EXE = ".exe" if os.name == "nt" else ""
LEVELS = [100, 250, 500, 5000]
LADDER = 400
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


def literals(n):
    return "x := " + "[" * n + "1" + "]" * n + ";\n\t\t(x = Nil)->PrintLine();", 3


def generics(n):
    return "v : " + "Vector<" * n + "IntRef" + ">" * n + ";\n\t\t(v = Nil)->PrintLine();", 3


SHAPES = [("parentheses", parentheses, True), ("blocks", blocks, True), ("calls", calls, False), ("array subscripts", subscripts, False),
          ("unary operators", unary, False), ("generic types", generics, False), ("array literals", literals, False)]
# levels a shape is tried at beyond LEVELS
DEEPER = {"array literals": [100000]}


# What is written between "{$" and "}" for each shape, n levels deep; whether 100 levels must compile.
INTERPOLATED = [("parentheses", lambda n: "(" * n + "1" + ")" * n, True),
                ("calls", lambda n: "Id(" * n + "1" + ")" * n, False),
                ("array subscripts", lambda n: "a[" * n + "0" + "]" * n, False),
                ("unary operators", lambda n: "-(" * n + "1" + ")" * n, False),
                ("array literals", lambda n: "[" * n + "1" + "]" * n, False)]
INTERPOLATION_LEVELS = [100, 250, 5000]
# "{$" does not nest (see above): one inside another is refused, however many there are
NESTED_INTERPOLATIONS = [100, 250, 5000]
NESTED_MESSAGE = "A '{$' inside '{$...}'"


def interpolation(expression, spec=""):
    return "a := Int->New[2];\n\t\ts := \"v: {$" + expression + spec + "}\";\n\t\ts->Size()->PrintLine();\n\t\ta->Size()->PrintLine();", 4


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
            for levels in LEVELS + DEEPER.get(shape, []):
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

        # The same nesting inside a string interpolation, which a parser of its own reads (see above).
        def compile_sources(names):
            obe = os.path.join(work, "interp.obe")
            if os.path.exists(obe):
                os.remove(obe)
            start = time.time()
            p = subprocess.run([obc, "-src", ",".join(names), "-dest", obe, "-lib", "gen_collect"], cwd=work, env=env, capture_output=True, timeout=TIME_LIMIT)
            out = (p.stdout + p.stderr).decode("utf-8", "replace")
            errors = [l for l in out.splitlines() if ".obs:(" in l and "): Warning:" not in l]
            return p.returncode, out, errors, os.path.exists(obe), time.time() - start

        def ends(out):
            # an error quotes the interpolation, which is thousands of characters long
            return out if len(out) < 1200 else out[:400] + " ... " + out[-700:]

        for spec, spec_label in (("", ""), (":8", " with a format")):
            for shape, build, shallow_compiles in INTERPOLATED:
                refused_at = None
                for levels in INTERPOLATION_LEVELS:
                    body, line_in_main = interpolation(build(levels), spec)
                    with open(os.path.join(work, "interp.obs"), "w", encoding="utf-8") as f:
                        f.write(program(body))
                    label = "interpolated %s%s, %d levels" % (shape, spec_label, levels)
                    try:
                        code, out, errors, wrote, elapsed = compile_sources(["interp.obs"])
                    except subprocess.TimeoutExpired:
                        check("%s: answered within %d s" % (label, TIME_LIMIT), False)
                        continue
                    if not 0 <= code <= 9:
                        check("%s: obc answers (it ended with status %d)" % (label, code), False, ends(out))
                        continue
                    line = 8 + line_in_main - 2
                    # the reason is on the line after the error's own: "Invalid expression in string interpolation: '...'"
                    said = out.count(MESSAGE)
                    if code == 0:
                        check("%s: compiled (%.1f s)" % (label, elapsed), wrote and refused_at is None, "compiled although %s levels were refused" % refused_at)
                    elif said or refused_at is not None:
                        check("%s: refused with '%s' on line %d, once (%.1f s)" % (label, MESSAGE, line, elapsed),
                              said == 1 and len(errors) == 1 and (":(%d," % line) in errors[0] and not wrote, ends(out))
                        if refused_at is None:
                            refused_at = levels
                    else:
                        # not for its depth: the expression is one an interpolation cannot print (an array)
                        check("%s: refused on line %d for what it is, not for its depth (%.1f s)" % (label, line, elapsed),
                              any((":(%d," % line) in l for l in errors) and not wrote and not shallow_compiles, ends(out))
                check("interpolated %s%s: 5000 levels are refused for their depth" % (shape, spec_label), refused_at is not None)

        # the error does not end the compilation: a later file's own error is still reported
        body, line_in_main = interpolation("Id(" * 5000 + "1" + ")" * 5000)
        with open(os.path.join(work, "interp.obs"), "w", encoding="utf-8") as f:
            f.write(program(body))
        with open(os.path.join(work, "later.obs"), "w", encoding="utf-8") as f:
            f.write("class Later {\n\tfunction : Twice(v : Int) ~ Int {\n\t\treturn v + undefined_name;\n\t}\n}\n")
        try:
            code, out, errors, wrote, elapsed = compile_sources(["interp.obs", "later.obs"])
            check("an interpolation nested too deeply, then a second file with an error of its own: both are reported",
                  1 <= code <= 9 and not wrote and out.count(MESSAGE) == 1 and any("interp.obs:(%d," % (8 + line_in_main - 2) in l for l in errors) and
                  any("later.obs:(3," in l and "undefined_name" in l for l in errors), "status %d\n" % code + ends(out))
        except subprocess.TimeoutExpired:
            check("an interpolation nested too deeply, then a second file: answered within %d s" % TIME_LIMIT, False)

        for levels in NESTED_INTERPOLATIONS:
            for label, text in (("'{$' inside '{$'", "{$" * levels + "v" + "}" * levels),
                                ("a quoted interpolation inside an interpolation", "Text(\\\"t {$" * levels + "v" + "}\\\")" * levels)):
                with open(os.path.join(work, "interp.obs"), "w", encoding="utf-8") as f:
                    f.write("class Nest {\n\tfunction : Text(t : String) ~ String {\n\t\treturn t;\n\t}\n\n\tfunction : Main(args : String[]) ~ Nil {\n"
                            "\t\tv := \"core\";\n\t\ts := \"" + text + "\";\n\t\ts->Size()->PrintLine();\n\t}\n}\n")
                try:
                    code, out, errors, wrote, elapsed = compile_sources(["interp.obs"])
                    check("%s, %d times: refused with \"%s\" on line 8, once (%.1f s)" % (label, levels, NESTED_MESSAGE, elapsed),
                          1 <= code <= 9 and not wrote and len(errors) == 1 and NESTED_MESSAGE in errors[0] and ":(8," in errors[0], "status %d\n" % code + ends(out))
                except subprocess.TimeoutExpired:
                    check("%s, %d times: answered within %d s" % (label, levels, TIME_LIMIT), False)

        # An 'else if' ladder is read by one call of the parser per rung, so it is held to the same stack
        # budget though nothing in it is nested. A generated dispatcher is hundreds of rungs long: 400 must
        # compile. (The rung's stack frame doubled when functions called once from it were merged into it
        # by the optimizer, and the longest ladder obc read fell from 1,800 rungs to 899.)
        ladder = "x := args->Size();\n\t\ty := 0;\n\t\tif(x = -1) { y := 0; }"
        ladder += "".join("\n\t\telse if(x = %d) { y := %d; }" % (i, i) for i in range(LADDER)) + ";\n\t\ty->PrintLine();"
        src = os.path.join(work, "ladder.obs")
        with open(src, "w", encoding="utf-8") as f:
            f.write(program(ladder))
        obe = os.path.join(work, "ladder.obe")
        try:
            p = subprocess.run([obc, "-src", src, "-dest", obe, "-lib", "gen_collect"], cwd=work, env=env, capture_output=True, timeout=TIME_LIMIT)
            check("an 'else if' ladder of %d rungs compiles" % LADDER, p.returncode == 0 and os.path.exists(obe),
                  (p.stdout + p.stderr).decode("utf-8", "replace"))
        except subprocess.TimeoutExpired:
            check("an 'else if' ladder of %d rungs: answered within %d s" % (LADDER, TIME_LIMIT), False)

        # An array literal nested N deep is an array of N dimensions, and an array has at most 8: the VM pops
        # the sizes into a buffer of 8 without counting them. obc holds 'Type->New[...]' to 8 sizes but compiles
        # a literal of any depth; obr used to run it over the buffer (9 levels: in silence; 100: "Array
        # allocation size overflow"; 2,000: "the operand stack grows to 1999 words"). It is now refused when
        # the program is loaded, by its cause, with the count, the limit and what to do. 8 levels still run.
        obr = os.path.join(bin_dir, "obr" + EXE)

        def dimensions(label, body):
            src = os.path.join(work, "dims.obs")
            with open(src, "w", encoding="utf-8") as f:
                f.write(program(body + "\n\t\t(x <> Nil)->PrintLine();"))
            obe = os.path.join(work, "dims.obe")
            if os.path.exists(obe):
                os.remove(obe)
            try:
                p = subprocess.run([obc, "-src", src, "-dest", obe, "-lib", "gen_collect"], cwd=work, env=env, capture_output=True, timeout=TIME_LIMIT)
                if p.returncode != 0:
                    return None, (p.stdout + p.stderr).decode("utf-8", "replace")
                r = subprocess.run([obr, obe], cwd=work, env=env, capture_output=True, timeout=TIME_LIMIT)
            except subprocess.TimeoutExpired:
                check("%s: answered within %d s" % (label, TIME_LIMIT), False)
                return None, ""
            return r.returncode, (r.stdout + r.stderr).decode("utf-8", "replace")

        for levels in (8, 9, 100, 500):
            label = "an array literal %d levels deep" % levels
            code, out = dimensions(label, "x := " + "[" * levels + "7" + "]" * levels + ";")
            if levels == 8:
                check("%s runs" % label, code == 0 and out.strip() == "true", out)
            elif code is None:
                # deeper than the parser reads on this platform: held by the shapes above
                check("%s: obc compiles it or says '%s'" % (label, MESSAGE), MESSAGE in out, out)
            else:
                check("%s is refused by obr with the count, the limit and what to do" % label,
                      code == 1 and ("an array of %d dimensions" % levels) in out and "at most 8" in out and
                      "use fewer dimensions" in out and "overflow" not in out and "operand stack" not in out, out)
        code, out = dimensions("8 sizes", "x := Int->New[" + ", ".join(["1"] * 8) + "];")
        check("'Int->New[...]' with 8 sizes runs", code == 0 and out.strip() == "true", out)
        code, out = dimensions("9 sizes", "x := Int->New[" + ", ".join(["1"] * 9) + "];")
        check("'Int->New[...]' with 9 sizes is refused by obc, as before", code is None and "Array dimensions cannot exceed 8" in out, out)

    if failures:
        print("FAIL: %d check(s) failed" % len(failures))
        return 1
    print("PASS: deep nesting is compiled or refused, never a crash")
    return 0


if __name__ == "__main__":
    sys.exit(main())
