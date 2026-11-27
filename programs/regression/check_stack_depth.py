#!/usr/bin/env python3
"""
An expression that needs more operand stack than a call has is obc's error, at its line.

Usage: check_stack_depth.py <bin_dir>

<bin_dir> holds obc and obr; the libraries are read from the sibling lib/ directory.

The VM gives a call a fixed room on the operand stack, and the loader refuses a method whose stack can
stand more than 1,000 words above its arguments ("the operand stack grows to N words past the arguments,
more than a call's room"). The compiler did not count those words: 33 nested calls of 32 arguments each
compiled, and the program was refused whole when it was run, naming a method and no line. obc now counts
them over the code it is about to write, the way the loader does (core/compiler/stack_depth.h), and says

    file:(line,pos): This expression needs an operand stack of N words, more than the limit of 1000: split
    it into statements or use fewer arguments

The worst this check could do is refuse a program the loader accepts, so the test is of the boundary and
of the agreement, in every place an expression is compiled differently. For each shape below it builds the
expression that needs exactly 1,000 words and the one that needs 1,001 (a pad of nested two-argument calls
is added one word at a time until obc's own count says so), and requires that

  * at 1,000 words obc compiles the program, obr loads it and runs it to the value the expression has,
    and obc's count of every method of the image equals obr's (OBJECK_STACK_DEPTH_OUT, under which each
    writes its counts);
  * at 1,001 words obc refuses it with the message above, once, on the expression's line, with a status
    it gives on purpose, and writes no image.

The shapes: nested calls of 32 Int arguments (at -opt s0 and s3, and as a debug build), of 61 arguments,
of Float and function arguments (a function value is two words), a mix of calls, arithmetic, an instance
call and an array subscript, the same inside a lambda, inside a 'try' block and inside its 'on' handler,
inside a string interpolation, in an instance method with parameters (the limit is above the arguments),
and in a library ('-tar lib', where calls are still written by name): the library that needs 1,001 words
is refused when it is compiled, and the one that needs 1,000 links into a program that loads and runs.

The cost: the count is one pass over the code, so a method of 20,000 statements with a deep expression
in it compiles within the time limit.

Last, the one construct whose form the count decides. A String literal of up to six strings is copied from
the operand stack (a word for each string and one more), and under some 996 waiting arguments that alone
took a method past the limit: a program that fits the stack's 1,024 words, compiled, and was refused by the
loader. Where a method is over the limit obc now compiles again with that method's String literals built an
element at a time (two words), and reports the expression only if it is still over. For 1 to 6 strings under
992 to 1,000 waiting Ints, at -opt s0 and s3, and for some of them in a lambda and in a library: the stack
form is kept where it fits, the element form is used where it does not, the program loads, obc and obr count
it alike, every element and the sum of every waiting argument are right interpreted and under the JIT, a
literal in the method beside it keeps its stack form, and only what fits in neither form is refused, once.

tools/fuzz/adhoc/obe_corpus_gate.py holds the same equality of the two counts over every program of the
tree and every library.
"""
import os
import re
import subprocess
import sys
import tempfile
import time

EXE = ".exe" if os.name == "nt" else ""
LIMIT = 1000
TIME_LIMIT = 120
MESSAGE = re.compile(r"This expression needs an operand stack of (\d+) words, more than the limit of %d: "
                     r"split it into statements or use fewer arguments" % LIMIT)
LOADER_MESSAGE = "the operand stack grows to"


def ints(n):
    return ", ".join("a%d : Int" % i for i in range(n))


# (source, value) pairs: an expression and what it evaluates to
def chain(levels, args, inner=("0", 0)):
    """F<args>(1, ..., 1, <the next level>): each level leaves args - 1 words waiting; its value is 1 + the last argument"""
    src, value = inner
    for _ in range(levels):
        src = "F%d(%s%s)" % (args, "1, " * (args - 1), src)
        value += 1
    return src, value


def pad(n, inner):
    """P(1, P(1, ...)): one more word waiting for each level; adds 1 to the value"""
    src, value = inner
    return "P(1, " * n + src + ")" * n, value + n


def functions(sizes):
    out = ""
    for n in sizes:
        out += "  function : F%d(%s) ~ Int {\n    return a0 + a%d;\n  }\n" % (n, ints(n), n - 1)
    out += "  function : P(a : Int, b : Int) ~ Int {\n    return a + b;\n  }\n"
    return out


# Each shape: build(n) -> (program text, the expression's text, the value the program prints or None).
# 'n' is the pad: one more nested two-argument call for each.
def plain(args, levels):
    def build(n):
        src, value = pad(n, chain(levels, args))
        return ("class Deep {\n" + functions([args]) +
                "  function : Main(args : String[]) ~ Nil {\n    x := %s;\n    x->PrintLine();\n  }\n}\n" % src), src, value
    return build


def float_and_function(n):
    # G takes six Floats, two function values (two words each) and an Int: eleven words for nine arguments
    src, value = pad(n, chain(18, 32))
    for _ in range(40):
        src = "G(1.5, 2.5, 3.5, 4.5, 5.5, 6.5, Twice(Int) ~ Int, Twice(Int) ~ Int, %s)" % src
        value = value + 2 + 4 + 1
    return ("class Deep {\n" + functions([32]) +
            "  function : Twice(v : Int) ~ Int {\n    return v * 2;\n  }\n"
            "  function : G(f : Float, g : Float, h : Float, i : Float, j : Float, k : Float, t : (Int) ~ Int, u : (Int) ~ Int, v : Int) ~ Int {\n"
            "    return v + t(1) + u(2) + (f + g + h + i + j + k)->As(Int) - 23;\n  }\n"
            "  function : Main(args : String[]) ~ Nil {\n    x := %s;\n    x->PrintLine();\n  }\n}\n" % src), src, value


def mixed_expression(n):
    """calls, arithmetic that waits on an operand, an instance call and an array subscript"""
    src, _ = pad(n, chain(24, 32))
    # (the right operand is evaluated first, so the one on the left is what the other waits for)
    for i in range(20):
        src = "(%s) * 2 + %d" % (src, i)
    src, _ = chain(6, 32, ("o->Add(3, 4, %s)" % src, 0))
    # v[...] of a one-element array that holds 41: the index is the expression, made 0
    return "v[(%s) * 0] + 5" % src, 46


def mixed(n):
    src, value = mixed_expression(n)
    return ("class Deep {\n  @k : Int;\n  New() {\n    @k := 0;\n  }\n  method : public : Add(a : Int, b : Int, c : Int) ~ Int {\n"
            "    return a + b + c + @k;\n  }\n" + functions([32]) +
            "  function : Main(args : String[]) ~ Nil {\n    o := Deep->New();\n    v := Int->New[1];\n    v[0] := 41;\n"
            "    x := %s;\n    x->PrintLine();\n  }\n}\n" % src), src, value


def in_lambda(n):
    src, value = pad(n, chain(31, 32, ("d", 5)))
    return ("class Deep {\n" + functions([32]) +
            "  function : Main(args : String[]) ~ Nil {\n    f := \\(Int) ~ Int : (d) =>\n      %s;\n"
            "    f(5)->PrintLine();\n  }\n}\n" % src), src, value


def in_try(n):
    src, value = pad(n, chain(31, 32))
    return ("class Deep {\n" + functions([32]) +
            "  function : Main(args : String[]) ~ Nil {\n    x := 0;\n    try {\n      x := %s;\n    }\n"
            "    on(e : Error) {\n      x := -1;\n    };\n    x->PrintLine();\n  }\n}\n" % src), src, value


def in_handler(n):
    src, value = pad(n, chain(31, 32))
    return ("class Deep {\n" + functions([32]) +
            "  function : Fail(v : Int) ~ Int {\n    values := Int->New[1];\n    return values[v];\n  }\n"
            "  function : Main(args : String[]) ~ Nil {\n    x := 0;\n    try {\n      x := Fail(7);\n    }\n"
            "    on(e : Error) {\n      x := %s;\n    };\n    x->PrintLine();\n  }\n}\n" % src), src, value


def in_interpolation(n):
    src, value = pad(n, chain(31, 32))
    return ("class Deep {\n" + functions([32]) +
            "  function : Main(args : String[]) ~ Nil {\n    s := \"v={$%s}\";\n    s->PrintLine();\n  }\n}\n" % src), src, "v=%d" % value


def in_method(n):
    # an instance method with parameters: the limit is counted above them
    src, value = pad(n, chain(31, 32, ("p + q + r", 6)))
    return ("class Deep {\n  New() {\n  }\n" + functions([32]) +
            "  method : public : Run(p : Int, q : Int, r : Int) ~ Int {\n    return %s;\n  }\n"
            "  function : Main(args : String[]) ~ Nil {\n    Deep->New()->Run(1, 2, 3)->PrintLine();\n  }\n}\n" % src), src, value


def library(n):
    src, value = pad(n, chain(31, 32))
    return ("bundle Depth.Test {\n  class Deep {\n" + functions([32]) +
            "    function : public : Run() ~ Int {\n      return %s;\n    }\n  }\n}\n" % src), src, value


LIBRARY_USER = "use Depth.Test;\n\nclass User {\n  function : Main(args : String[]) ~ Nil {\n    Deep->Run()->PrintLine();\n  }\n}\n"


LITERAL_ARGS = 31
LITERAL_LEVELS = 32


def literal_program(pads, strings, where="main"):
    """A String literal of 'strings' elements under 31 * 32 + pads waiting Int arguments.

    Returns the program, the literal's text and the lines the program prints: the literal's size, every
    element, and the sum of what every level was passed (a waiting word that was overwritten shows there).
    """
    literal = "[" + ", ".join('"e%d-%d"' % (strings, i) for i in range(strings)) + "]"
    src = literal
    total = 0
    for i in range(pads):
        src = "Q(%d, %s)" % (i + 2, src)
        total += i + 2
    for level in range(LITERAL_LEVELS):
        values = [level * LITERAL_ARGS + k + 1 for k in range(LITERAL_ARGS)]
        src = "Pass(" + ", ".join(str(v) for v in values) + ", " + src + ")"
        total += sum(values)
    params = ", ".join("p%d : Int" % i for i in range(LITERAL_ARGS))
    summed = " + ".join("p%d" % i for i in range(LITERAL_ARGS))
    helpers = ("  @sum : static : Int;\n  @kept : static : String[];\n"
               "  function : Keep(a : String[]) ~ Int {\n    @kept := a;\n    return a->Size();\n  }\n"
               "  function : Pass(%s, a : String[]) ~ String[] {\n    @sum += %s;\n    return a;\n  }\n"
               "  function : Q(x : Int, a : String[]) ~ String[] {\n    @sum += x;\n    return a;\n  }\n"
               # a literal of six strings in another method: it keeps the stack form (eight words)
               "  function : Other() ~ String[] {\n    return [\"o0\", \"o1\", \"o2\", \"o3\", \"o4\", \"o5\"];\n  }\n"
               "  function : Show(a : String[]) ~ Nil {\n    a->Size()->PrintLine();\n    each(i : a) {\n      a[i]->PrintLine();\n    };\n"
               "    @sum->PrintLine();\n    Other()->Size()->PrintLine();\n  }\n" % (params, summed))
    printed = [str(strings)] + ["e%d-%d" % (strings, i) for i in range(strings)] + [str(total), "6"]
    if where == "lambda":
        body = "    f := \\() ~ Int : () =>\n      Keep(%s);\n    f();\n    Show(@kept);\n" % src
    else:
        body = "    a := %s;\n    Show(a);\n" % src
    if where == "library":
        return ("bundle Depth.Test {\n  class Lit {\n" + helpers +
                "    function : public : Run() ~ Nil {\n" + body + "    }\n  }\n}\n"), literal, printed
    return ("class Lit {\n" + helpers + "  function : Main(args : String[]) ~ Nil {\n" + body + "  }\n}\n"), literal, printed


LITERAL_USER = "use Depth.Test;\n\nclass User {\n  function : Main(args : String[]) ~ Nil {\n    Lit->Run();\n  }\n}\n"


def long_method(n):
    """20,000 statements, one of them deep: the count is one pass, whatever the method's length"""
    src, value = pad(n, chain(31, 32))
    body = "".join("    x += %d;\n" % (i % 7) for i in range(20000))
    total = value + sum(i % 7 for i in range(20000))
    return ("class Deep {\n" + functions([32]) +
            "  function : Main(args : String[]) ~ Nil {\n    x := %s;\n%s    x->PrintLine();\n  }\n}\n" % (src, body)), src, total


def main():
    if len(sys.argv) != 2:
        print(__doc__)
        return 2
    bin_dir = os.path.abspath(sys.argv[1])
    obc = os.path.join(bin_dir, "obc" + EXE)
    obr = os.path.join(bin_dir, "obr" + EXE)
    lib_dir = os.path.join(os.path.dirname(bin_dir), "lib")
    env = dict(os.environ)
    env.pop("OBJECK_STACK_DEPTH_OUT", None)
    failures = []

    def check(name, cond, detail=""):
        print("  [%s] %s%s" % ("PASS" if cond else "FAIL", name, "" if cond else "\n         " + str(detail)[-700:]))
        if not cond:
            failures.append(name)

    with tempfile.TemporaryDirectory(prefix="stack-depth-") as work:
        # obc resolves -lib against OBJECK_LIB_PATH, so the library made here goes into a private lib/
        private_lib = os.path.join(work, "lib")
        os.mkdir(private_lib)
        for name in os.listdir(lib_dir):
            if name.endswith(".obl"):
                with open(os.path.join(lib_dir, name), "rb") as src:
                    with open(os.path.join(private_lib, name), "wb") as dst:
                        dst.write(src.read())
        env["OBJECK_LIB_PATH"] = private_lib

        def depths(path):
            try:
                with open(path, "rb") as f:
                    return sorted(f.read().decode("utf-8", "replace").splitlines())
            except OSError:
                return None

        def compile_program(text, flags, dest):
            """(status, output, error lines, obc's depths, seconds)"""
            src = os.path.join(work, "deep.obs")
            with open(src, "w", encoding="utf-8", newline="\n") as f:
                f.write(text)
            counted = dest + ".cdepth"
            for stale in (dest, counted):
                if os.path.exists(stale):
                    os.remove(stale)
            start = time.time()
            p = subprocess.run([obc, "-src", src, "-dest", dest] + flags, cwd=work, env=dict(env, OBJECK_STACK_DEPTH_OUT=counted),
                               capture_output=True, timeout=TIME_LIMIT)
            out = (p.stdout + p.stderr).decode("utf-8", "replace")
            errors = [l for l in out.splitlines() if ".obs:(" in l and "): Warning:" not in l]
            return p.returncode, out, errors, depths(counted), time.time() - start

        def deepest(counted):
            best = -1
            for line in counted or []:
                name, _, words = line.rpartition("\t")
                if name.startswith("Deep:") or name.startswith("Depth.Test.Deep:"):
                    best = max(best, int(words) if words.isdigit() else -1)
            return best

        def needs(build, flags, dest, target):
            """the pad at which obc counts 'target' words for the shape, or None"""
            n = 4
            for _ in range(4):
                text, _, _ = build(n)
                code, out, errors, counted, _ = compile_program(text, flags, dest)
                said = [MESSAGE.search(l) for l in errors]
                words = int(said[0].group(1)) if said and said[0] else deepest(counted)
                if words == target:
                    return n
                if words < 0:
                    return None
                n += target - words
                if n < 0:
                    return None
            return None

        def boundary(label, build, flags=None, lib_user=None, limit_seconds=None):
            flags = flags or []
            dest = os.path.join(private_lib, "deep.obl") if lib_user else os.path.join(work, "deep.obe")
            try:
                at_limit = needs(build, flags, dest, LIMIT)
                if at_limit is None:
                    check("%s: an expression of %d words can be built" % (label, LIMIT), False, compile_program(build(4)[0], flags, dest)[1])
                    return

                # ---- one word over: refused by obc, at the line, once ----
                text, expression, _ = build(at_limit + 1)
                code, out, errors, _, elapsed = compile_program(text, flags, dest)
                line = text[:text.index(expression)].count("\n") + 1
                said = [l for l in errors if MESSAGE.search(l)]
                words = int(MESSAGE.search(said[0]).group(1)) if said else -1
                check("%s: %d words are refused by obc on line %d, once (%.1f s)" % (label, LIMIT + 1, line, elapsed),
                      1 <= code <= 9 and len(said) == 1 and len(errors) == 1 and (".obs:(%d," % line) in said[0] and
                      words == LIMIT + 1 and not os.path.exists(dest), out)

                # ---- at the limit: compiled, loaded, run, and counted alike ----
                text, expression, value = build(at_limit)
                code, out, errors, counted, elapsed = compile_program(text, flags, dest)
                check("%s: %d words compile (%.1f s)" % (label, LIMIT, elapsed),
                      code == 0 and os.path.exists(dest) and deepest(counted) == LIMIT, out)
                if limit_seconds is not None:
                    check("%s: compiled within %d s" % (label, limit_seconds), elapsed < limit_seconds, "%.1f s" % elapsed)
                if code != 0:
                    return
                image = dest
                if lib_user:
                    # a library is loaded as part of a program
                    check("%s: no method of the library is left uncounted" % label, counted and not [l for l in counted if l.endswith("\t?")],
                          [l for l in counted or [] if l.endswith("\t?")][:5])
                    image = os.path.join(work, "user.obe")
                    text = lib_user
                    code, out, errors, counted, _ = compile_program(text, ["-lib", "deep"], image)
                    check("%s: a program that uses the library compiles" % label, code == 0 and os.path.exists(image), out)
                    if code != 0:
                        return
                loaded = image + ".rdepth"
                if os.path.exists(loaded):
                    os.remove(loaded)
                p = subprocess.run([obr, image], cwd=work, env=dict(env, OBJECK_VERIFY_ONLY="1", OBJECK_STACK_DEPTH_OUT=loaded),
                                   capture_output=True, timeout=TIME_LIMIT, stdin=subprocess.DEVNULL)
                ran = depths(loaded)
                load_out = (p.stdout + p.stderr).decode("utf-8", "replace")
                check("%s: obr loads the image" % label, p.returncode == 0 and LOADER_MESSAGE not in load_out, load_out)
                differ = sorted(set(counted or []) ^ set(ran or []))
                check("%s: obc and obr count every method alike (%d methods)" % (label, len(ran or [])),
                      bool(counted) and bool(ran) and not differ and len(counted) == len(ran) and
                      not [l for l in counted if l.endswith("\t?")], differ[:6])
                p = subprocess.run([obr, image], cwd=work, env=env, capture_output=True, timeout=TIME_LIMIT, stdin=subprocess.DEVNULL)
                run_out = (p.stdout + p.stderr).decode("utf-8", "replace").strip()
                check("%s: the program runs to %s" % (label, value), p.returncode == 0 and run_out == str(value), run_out)
            except subprocess.TimeoutExpired:
                check("%s: answered within %d s" % (label, TIME_LIMIT), False)

        # the two programs the limit was found with: over it as they stand
        for label, args, levels, words in (("33 nested calls of 32 Int arguments", 32, 33, 1024),
                                           ("17 nested calls of 61 Int arguments", 61, 17, 1021)):
            dest = os.path.join(work, "deep.obe")
            text, expression, _ = plain(args, levels)(0)
            try:
                code, out, errors, _, elapsed = compile_program(text, [], dest)
            except subprocess.TimeoutExpired:
                check("%s: answered within %d s" % (label, TIME_LIMIT), False)
                continue
            line = text[:text.index(expression)].count("\n") + 1
            said = [l for l in errors if MESSAGE.search(l)]
            check("%s (%d words): refused by obc on line %d, once (%.1f s)" % (label, words, line, elapsed),
                  1 <= code <= 9 and len(said) == 1 and len(errors) == 1 and (".obs:(%d," % line) in said[0] and
                  int(MESSAGE.search(said[0]).group(1)) == words and not os.path.exists(dest), out)

        boundary("nested calls of 32 Int arguments", plain(32, 32))
        boundary("nested calls of 32 Int arguments, -opt s0", plain(32, 32), ["-opt", "s0"])
        boundary("nested calls of 32 Int arguments, -opt s3", plain(32, 32), ["-opt", "s3"])
        boundary("nested calls of 32 Int arguments, -debug", plain(32, 32), ["-debug"])
        boundary("nested calls of 61 Int arguments", plain(61, 16))
        boundary("Float and function arguments", float_and_function)
        boundary("a mix of calls, arithmetic, an instance call and a subscript", mixed)
        boundary("inside a lambda", in_lambda)
        boundary("inside a 'try' block", in_try)
        boundary("inside an 'on' handler", in_handler)
        boundary("inside a string interpolation", in_interpolation)
        boundary("in an instance method with parameters", in_method)
        boundary("in a library", library, ["-tar", "lib"], lib_user=LIBRARY_USER)
        boundary("a method of 20,000 statements", long_method, limit_seconds=60)

        # ---- a short String literal under waiting arguments ----
        # Up to six strings are copied from the operand stack: a word for each and one more on top of the
        # words counted here as waiting (the arguments and the call's own two), where the element form adds
        # two. With 996 words waiting, six strings in the stack form make 1,003, a program that fits the
        # stack and that the loader refused. obc counts the method
        # and compiles it again with the element form where the stack form is over the limit. So, for 1 to
        # 6 strings under 992 to 1,000 waiting Ints, at -opt s0 and s3 (in Main; a method without arguments
        # counts a word more for each form):
        #   * the stack form is kept where it fits (waiting + strings + 1 words at most 1,000),
        #   * the element form is used where it does not and that form fits (waiting + 2 at most 1,000),
        #   * the program then loads, is counted alike by obc and obr, and prints every element and the sum
        #     of every waiting argument, interpreted and compiled by the JIT,
        #   * and only an expression that fits in neither form (999 waiting words or more) is refused,
        #     with the message, once.
        # The same in a lambda (its literal is analyzed under the method it is written in, which is the
        # case the third attempt of the compilation is for) and in a library.
        def literal_case(label, pads, strings, flags, where="main"):
            waiting = LITERAL_ARGS * LITERAL_LEVELS + pads
            # The limit is above the method's arguments, which it pops as it starts: Main has one, so
            # it holds a word more than a function or a lambda without any.
            stack_form = waiting + strings + (1 if where == "main" else 2)
            element_form = waiting + (2 if where == "main" else 3)
            text, literal, printed = literal_program(pads, strings, where)
            is_lib = where == "library"
            dest = os.path.join(private_lib, "deep.obl") if is_lib else os.path.join(work, "deep.obe")
            method = "Depth.Test.Lit:Run:" if is_lib else "Lit:Main:"
            try:
                code, out, errors, counted, _ = compile_program(text, flags + (["-tar", "lib"] if is_lib else []), dest)
                if element_form > LIMIT:
                    said = [l for l in errors if MESSAGE.search(l)]
                    return check("%s: refused by obc, once (it fits in neither form)" % label,
                                 1 <= code <= 9 and len(said) == 1 and len(errors) == 1 and not os.path.exists(dest), out)
                if code != 0 or not os.path.exists(dest):
                    return check("%s: compiles" % label, False, out)
                depth = dict(l.rsplit("\t", 1) for l in counted or [])
                found = [int(v) for k, v in depth.items() if k.startswith(method) and v.isdigit()]
                if where == "lambda":
                    # (the literal is in the lambda's own method)
                    found = [max(int(v) for k, v in depth.items() if k.startswith("Lit:") and v.isdigit())]
                want = stack_form if stack_form <= LIMIT else element_form
                form = "the stack form" if stack_form <= LIMIT else "the element form"
                ok = found == [want]
                # the literal of the method beside it is not touched by what this one needed
                if where == "main":
                    ok = ok and depth.get("Lit:Other:") == "8"
                detail = "depth %s, wanted %d; Other %s" % (found, want, depth.get("Lit:Other:"))
                image = dest
                if is_lib:
                    image = os.path.join(work, "user.obe")
                    code, out, errors, counted, _ = compile_program(LITERAL_USER, ["-lib", "deep"] + flags, image)
                    if code != 0:
                        return check("%s: a program that uses the library compiles" % label, False, out)
                loaded = image + ".rdepth"
                if os.path.exists(loaded):
                    os.remove(loaded)
                p = subprocess.run([obr, image], cwd=work, env=dict(env, OBJECK_VERIFY_ONLY="1", OBJECK_STACK_DEPTH_OUT=loaded),
                                   capture_output=True, timeout=TIME_LIMIT, stdin=subprocess.DEVNULL)
                ran = depths(loaded)
                if p.returncode != 0 or sorted(counted or []) != (ran or []) or [l for l in counted if l.endswith("\t?")]:
                    ok = False
                    detail += "; load status %d %s; counts differ: %s" % (
                        p.returncode, (p.stdout + p.stderr).decode("utf-8", "replace")[-200:], sorted(set(counted or []) ^ set(ran or []))[:4])
                for mode, args, extra in (("interpreted", ["--jit=off"], {}), ("JIT", [], {"OBJECK_JIT_THRESHOLD": "1"})):
                    p = subprocess.run([obr] + args + [image], cwd=work, env=dict(env, **extra), capture_output=True,
                                       timeout=TIME_LIMIT, stdin=subprocess.DEVNULL)
                    lines = (p.stdout + p.stderr).decode("utf-8", "replace").split()
                    if p.returncode != 0 or lines != printed:
                        ok = False
                        detail += "; %s: status %d, printed %s" % (mode, p.returncode, lines[-9:])
                return check("%s: %s (%d words), loads, counted alike, every element and waiting word right" % (label, form, want), ok, detail)
            except subprocess.TimeoutExpired:
                return check("%s: answered within %d s" % (label, TIME_LIMIT), False)

        for opt in ("s0", "s3"):
            for strings in range(1, 7):
                for pads in range(0, 9):
                    literal_case("%d string(s) under %d waiting words, -opt %s" % (strings, LITERAL_ARGS * LITERAL_LEVELS + pads, opt),
                                 pads, strings, ["-opt", opt])
        for where in ("lambda", "library"):
            for strings, pads in ((6, 0), (6, 1), (6, 2), (6, 4), (6, 6), (3, 5), (1, 6), (6, 7)):
                literal_case("%d string(s) under %d waiting words, in a %s" % (strings, LITERAL_ARGS * LITERAL_LEVELS + pads, where),
                             pads, strings, [], where)

    if failures:
        print("\n%d check(s) failed" % len(failures))
        return 1
    print("\nAll checks passed")
    return 0


if __name__ == "__main__":
    sys.exit(main())
