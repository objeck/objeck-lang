#!/usr/bin/env python3
"""
A postfix update in an operand that only one path evaluates is made at the end
of its statement, and only if that operand was evaluated.

Usage: check_conditional_operand_postfix.py <bin_dir> [--keep DIR]

<bin_dir> must contain obc and obr (with .exe on Windows); the libraries are
read from the sibling lib/ directory, as check_loop_condition_postfix.py does.

The rule (docs/FEATURES.md, "Increment and decrement"): outside the condition
of an 'if' or a loop, `i++` and `i--` yield the value from before the change
and the change is made when the statement ends, after its assignment. So every
read in the statement, and every method it calls, sees the values the statement
started with. An update written in a branch of '?:', in the default of '??', in
the second operand of '&' or '|', or in the arguments or the subscript of a
'?->' call is made only when that operand was evaluated.

postfix_update_conditional_statement_end.obs, nil_safe_call_argument_postfix.obs
and nil_safe_call_index_postfix.obs pin cases worked out by hand. This
generates statements from those operators at random, nested in one another:

    @x := E;   @i := E;   @j := E;   @flag := B;

    E  a literal, @i, @j, @i++, @i--, @j++, @j--, (E + E), (E - E),
       (B ? E : E), (r?->Take(E) ?? E), (r?->Items()[@a++ % 4] ?? E)
    B  (E < E), (B & B), (B | B)

The fields belong to the object the statements run in. 'r' is that object or
Nil, and Take(n) returns `n * 2 + @i`, so a call made by the statement reads a
field the statement updates. The fields are printed after every statement. The
expected lines come from model() below, which is the rule written in Python
and knows nothing of the compiler. Every program is compiled at -opt s0, s1,
s2 and s3 and run with --jit=off, with the default JIT and with
OBJECK_JIT_THRESHOLD=1; every run must print exactly the model's lines.

The generator is seeded, so the programs are the same on every run.
"""

import os
import random
import subprocess
import sys
import tempfile

EXE = ".exe" if os.name == "nt" else ""
TIMEOUT = 180
OPTS = ("s0", "s1", "s2", "s3")
MODES = (("interpreter", ["--jit=off"], {}), ("default JIT", [], {}), ("JIT on every call", [], {"OBJECK_JIT_THRESHOLD": "1"}))

PROGRAMS = 20
METHODS = 16
STATEMENTS = 5
ITEMS = (10, 20, 30, 40)
START = {"i": 3, "j": 5, "a": 0, "x": 0, "flag": False}


# ---- the generator: an expression is a tuple, (kind, operands..)

def gen_int(rng, depth):
    if depth <= 0 or rng.random() < 0.25:
        kind = rng.choice(("lit", "var", "var", "post", "post", "post"))
        if kind == "lit":
            return ("lit", rng.randint(0, 9))
        if kind == "var":
            return ("var", rng.choice("ij"))
        return ("post", rng.choice("ij"), rng.choice((1, -1)))

    kind = rng.choice(("add", "sub", "cond", "cond", "take", "take", "item"))
    if kind in ("add", "sub"):
        return (kind, gen_int(rng, depth - 1), gen_int(rng, depth - 1))
    if kind == "cond":
        return ("cond", gen_bool(rng, depth - 1), gen_int(rng, depth - 1), gen_int(rng, depth - 1))
    if kind == "take":
        return ("take", gen_int(rng, depth - 1), gen_int(rng, depth - 1))
    # the subscript updates @a, or only reads it
    return ("item", rng.random() < 0.7, gen_int(rng, depth - 1))


def gen_bool(rng, depth):
    if depth <= 0 or rng.random() < 0.4:
        return ("less", gen_int(rng, depth - 1), gen_int(rng, depth - 1))
    return (rng.choice(("and", "or")), gen_bool(rng, depth - 1), gen_bool(rng, depth - 1))


def gen_statement(rng):
    if rng.random() < 0.25:
        return ("flag", gen_bool(rng, 3))
    return (rng.choice(("x", "x", "i", "j")), gen_int(rng, 3))


# ---- as Objeck

def render(node):
    kind = node[0]
    if kind == "lit":
        return str(node[1])
    if kind == "var":
        return "@" + node[1]
    if kind == "post":
        return "@%s%s" % (node[1], "++" if node[2] > 0 else "--")
    if kind == "add":
        return "(%s + %s)" % (render(node[1]), render(node[2]))
    if kind == "sub":
        return "(%s - %s)" % (render(node[1]), render(node[2]))
    if kind == "cond":
        return "(%s ? %s : %s)" % (render(node[1]), render(node[2]), render(node[3]))
    if kind == "take":
        return "(r?->Take(%s) ?? %s)" % (render(node[1]), render(node[2]))
    if kind == "item":
        return "(r?->Items()[%s %% 4] ?? %s)" % ("@a++" if node[1] else "@a", render(node[2]))
    if kind == "less":
        return "(%s < %s)" % (render(node[1]), render(node[2]))
    if kind == "and":
        return "(%s & %s)" % (render(node[1]), render(node[2]))
    if kind == "or":
        return "(%s | %s)" % (render(node[1]), render(node[2]))
    raise ValueError(kind)


def program(methods):
    lines = ["class Cell {",
             "\t@i : Int;", "\t@j : Int;", "\t@a : Int;", "\t@x : Int;", "\t@flag : Bool;", "\t@items : Int[];", "",
             "\tNew() {",
             "\t\t@items := Int->New[%d];" % len(ITEMS)]
    lines += ["\t\t@items[%d] := %d;" % (k, v) for k, v in enumerate(ITEMS)]
    lines += ["\t}", "",
              "\tmethod : public : Take(n : Int) ~ Int {", "\t\treturn n * 2 + @i;", "\t}", "",
              "\tmethod : public : Items() ~ Int[] {", "\t\treturn @items;", "\t}", "",
              "\tmethod : Show() ~ Nil {",
              "\t\ti := @i; j := @j; a := @a; x := @x; flag := @flag;",
              "\t\t\"{$i} {$j} {$a} {$x} {$flag}\"->PrintLine();",
              "\t}", ""]
    for index, statements in enumerate(methods):
        lines.append("\tmethod : public : Run%d(r : Cell) ~ Nil {" % index)
        lines.append("\t\t@i := %d; @j := %d; @a := %d; @x := %d; @flag := false;" %
                     (START["i"], START["j"], START["a"], START["x"]))
        for target, expression in statements:
            lines.append("\t\t@%s := %s;" % (target, render(expression)))
            lines.append("\t\tShow();")
        lines += ["\t}", ""]
    lines += ["}", "",
              "class Generated {",
              "\tfunction : Main(args : String[]) ~ Nil {",
              "\t\tcell := Cell->New();",
              "\t\tnone : Cell;"]
    for index in range(len(methods)):
        lines.append("\t\tcell->Run%d(cell);" % index)
        lines.append("\t\tcell->Run%d(none);" % index)
    lines += ["\t}", "}", ""]
    return "\n".join(lines)


# ---- the rule

def model(node, start, has_receiver, updates):
    """The value of an expression. Every read is of 'start', the fields as the
    statement found them; 'updates' collects the (field, change) of each
    postfix update in an operand that is evaluated."""
    kind = node[0]
    if kind == "lit":
        return node[1]
    if kind == "var":
        return start[node[1]]
    if kind == "post":
        updates.append((node[1], node[2]))
        return start[node[1]]
    if kind == "add":
        return model(node[1], start, has_receiver, updates) + model(node[2], start, has_receiver, updates)
    if kind == "sub":
        return model(node[1], start, has_receiver, updates) - model(node[2], start, has_receiver, updates)
    if kind == "cond":
        taken = node[2] if model(node[1], start, has_receiver, updates) else node[3]
        return model(taken, start, has_receiver, updates)
    if kind == "take":
        if not has_receiver:
            return model(node[2], start, has_receiver, updates)
        # the callee reads @i while the statement is still running
        return model(node[1], start, has_receiver, updates) * 2 + start["i"]
    if kind == "item":
        if not has_receiver:
            return model(node[2], start, has_receiver, updates)
        if node[1]:
            updates.append(("a", 1))
        return ITEMS[start["a"] % 4]
    if kind == "less":
        left = model(node[1], start, has_receiver, updates)
        return left < model(node[2], start, has_receiver, updates)
    if kind == "and":
        return model(node[1], start, has_receiver, updates) and model(node[2], start, has_receiver, updates)
    if kind == "or":
        return model(node[1], start, has_receiver, updates) or model(node[2], start, has_receiver, updates)
    raise ValueError(kind)


def expected(methods):
    lines = []
    for statements in methods:
        for has_receiver in (True, False):
            fields = dict(START)
            for target, expression in statements:
                updates = []
                value = model(expression, dict(fields), has_receiver, updates)
                # the assignment, then the updates
                fields[target] = value
                for field, change in updates:
                    fields[field] += change
                lines.append("%d %d %d %d %s" % (fields["i"], fields["j"], fields["a"], fields["x"],
                                                 "true" if fields["flag"] else "false"))
    return lines


# ---- running

def fail(msg):
    print("FAIL: %s" % msg)
    sys.exit(1)


def main():
    args = sys.argv[1:]
    keep = None
    if "--keep" in args:
        at = args.index("--keep")
        keep = os.path.abspath(args[at + 1])
        del args[at:at + 2]
    if len(args) != 1:
        print(__doc__.strip())
        return 2

    bin_dir = os.path.abspath(args[0])
    obc = os.path.join(bin_dir, "obc" + EXE)
    obr = os.path.join(bin_dir, "obr" + EXE)
    lib_dir = os.path.join(os.path.dirname(bin_dir), "lib")
    for path in (obc, obr):
        if not os.path.isfile(path):
            fail("no %s" % path)
    if not os.path.isdir(lib_dir):
        fail("no library directory at %s" % lib_dir)

    env = dict(os.environ, OBJECK_LIB_PATH=lib_dir)
    env.pop("OBJECK_JIT_THRESHOLD", None)
    env.pop("OBJECK_JIT_DISABLE", None)

    holder = None
    if keep:
        os.makedirs(keep, exist_ok=True)
        work = keep
    else:
        holder = tempfile.TemporaryDirectory()
        work = holder.name

    rng = random.Random(20261104)
    statements = 0
    runs = 0
    for index in range(PROGRAMS):
        methods = [[gen_statement(rng) for _ in range(STATEMENTS)] for _ in range(METHODS)]
        want = expected(methods)
        statements += len(want)
        src = os.path.join(work, "generated_%02d.obs" % index)
        with open(src, "w") as handle:
            handle.write(program(methods))

        for opt in OPTS:
            image = os.path.join(work, "generated_%02d_%s.obe" % (index, opt))
            try:
                built = subprocess.run([obc, "-src", src, "-dest", image, "-opt", opt], capture_output=True, env=env,
                                       cwd=work, timeout=TIMEOUT, stdin=subprocess.DEVNULL)
            except subprocess.TimeoutExpired:
                fail("program %d: obc -opt %s timed out" % (index, opt))
            if built.returncode != 0 or not os.path.isfile(image):
                fail("program %d does not compile at -opt %s (%s)\n%s" %
                     (index, opt, src, (built.stdout + built.stderr).decode("utf-8", "replace")))

            for mode, flags, extra in MODES:
                try:
                    ran = subprocess.run([obr] + flags + [image], capture_output=True, env=dict(env, **extra),
                                         cwd=work, timeout=TIMEOUT, stdin=subprocess.DEVNULL)
                except subprocess.TimeoutExpired:
                    fail("program %d: -opt %s, %s timed out" % (index, opt, mode))
                got = ran.stdout.decode("utf-8", "replace").split("\n")
                got = [line.strip() for line in got if line.strip()]
                runs += 1
                if ran.returncode != 0 or got != want:
                    at = next((k for k in range(min(len(got), len(want))) if got[k] != want[k]), min(len(got), len(want)))
                    method, rest = divmod(at, STATEMENTS * 2)
                    receiver, statement = divmod(rest, STATEMENTS)
                    target, expression = methods[method][statement] if method < len(methods) else ("?", ("lit", 0))
                    fail("program %d (-opt %s, %s, exit %d): line %d differs\n"
                         "  method Run%d, receiver %s, statement %d: @%s := %s;\n"
                         "  the rule gives: %s\n  the program printed: %s\n  source: %s (use --keep DIR to keep it)" %
                         (index, opt, mode, ran.returncode, at + 1, method, "Nil" if receiver else "set", statement + 1,
                          target, render(expression), want[at] if at < len(want) else "(nothing)",
                          got[at] if at < len(got) else "(nothing)", src))

    if holder:
        holder.cleanup()
    print("PASS: %d generated statements print what the rule gives at -opt s0..s3, interpreter and JIT (%d runs)" %
          (statements, runs))
    return 0


if __name__ == "__main__":
    sys.exit(main())
