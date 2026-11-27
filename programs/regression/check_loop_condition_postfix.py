#!/usr/bin/env python3
"""
A postfix update in a loop condition takes effect every time the condition is tested.

Usage: check_loop_condition_postfix.py <bin_dir> [--keep DIR]

<bin_dir> must contain obc and obr (with .exe on Windows); the libraries are
read from the sibling lib/ directory, as check_dead_store_chain.py does.

The rule (docs/FEATURES.md, "Increment and decrement"): in the condition of a
'while', a 'for' or a 'do/while', `i++` and `i--` are tested by the value from
before the change and the change is made right after that test, before the
branch. The body reads the new value, the test that fails and ends the loop
makes the change too, an operand that '&' or '|' skips makes none, and the
second operand of '&' or '|' reads what the first one changed. Until v2026.11.4
a 'while' or 'for' made the change in the loop body, after its first statement,
and not at all when the loop was left.

Two parts:

  1. postfix_update_loop_condition.obs, whose expected numbers are worked out
     by hand, compiled at -opt s0, s1, s2 and s3 and run with --jit=off, with
     the default JIT and with OBJECK_JIT_THRESHOLD=1: twelve runs, each must
     exit 0 and print its PASS line.

  2. Generated loops, each a method that prints its rounds, a trace of the
     values its body read, and the value after the loop. They cover:
       - the loop: while, for, do/while
       - the update: ++ or --, of a local, a field, an array element
       - the test: <, > or <>
       - where the update stands in the condition: alone, as the first or the
         second operand of '&' or '|' (the other operand stops holding after
         two rounds), and as the first operand of an '&' whose second operand
         reads the updated variable
       - the body: plain, with a 'continue', with a 'break'
       - two start/limit pairs
     The expected line of each comes from model() below, which is the rule
     written in Python and knows nothing of the compiler. Every program is
     compiled at the four -opt levels and run in the three modes; every run
     must print exactly the model's lines.

--keep DIR leaves the generated programs and their images in DIR.

Things that would let a broken check pass, each of which fails here:
  - a model that agrees with a wrong compiler: part 1's numbers are by hand,
    and the model must reproduce three of them (MODEL_ANCHORS),
  - loops that do not compile, or print nothing: every generated program must
    compile at every level and print one line per loop,
  - -opt levels that are all the same build: the s0 and s3 images of the first
    generated program must differ.
"""

import itertools
import os
import subprocess
import sys
import tempfile

EXE = ".exe" if os.name == "nt" else ""
HERE = os.path.dirname(os.path.abspath(__file__))
FIXTURE = os.path.join(HERE, "postfix_update_loop_condition.obs")
FIXTURE_PASS = "PASS: a postfix update in a loop condition takes effect at every test"
TIMEOUT = 300
OPTS = ("s0", "s1", "s2", "s3")
MODES = (("jitoff", ["--jit=off"], {}), ("default", [], {}), ("jit1", [], {"OBJECK_JIT_THRESHOLD": "1"}))
PER_PROGRAM = 240
# every generated loop stops here at the latest, whatever its condition
ROUND_CAP = 12
TRACE_MOD = 1000003

LOOPS = ("while", "for", "do")
UPDATES = ("++", "--")
TARGETS = ("local", "field", "element")
RELATIONS = ("<", ">", "<>")
# P is the test with the update, G the guard 'rounds < 2', R a read of the updated variable
FORMS = ("P", "G&P", "P&G", "G|P", "P|G", "P&R")
BODIES = ("plain", "continue", "break")
PAIRS = ((0, 3), (4, 1))

RELATION_OF = {"<": lambda a, b: a < b, ">": lambda a, b: a > b, "<>": lambda a, b: a != b}


def model(loop, update, relation, form, body, start, limit):
    """(rounds, trace, value after the loop, 'for' update count) by the documented rule."""
    state = {"v": start}
    step = 1 if update == "++" else -1
    rounds = 0
    trace = 0
    count = 0

    def test():
        def p():
            old = state["v"]
            state["v"] = old + step      # made right after the read, before anything else
            return RELATION_OF[relation](old, limit)

        def g():
            return rounds < 2

        def r():
            return state["v"] % 3 != 0

        if form == "P":
            return p()
        if form == "G&P":
            return g() and p()
        if form == "P&G":
            return p() and g()
        if form == "G|P":
            return g() or p()
        if form == "P|G":
            return p() or g()
        return p() and r()

    first = True
    while True:
        if (loop != "do" or not first) and not test():
            break
        first = False
        # the body
        rounds += 1
        if rounds > ROUND_CAP:
            break
        skip = body == "continue" and rounds == 2
        if body == "break" and rounds == 3:
            break
        if not skip:
            trace = (trace * 31 + state["v"] + 100) % TRACE_MOD
        if loop == "for":
            count += 1
    return rounds, trace, state["v"], count


# (arguments of model, its result): three results of postfix_update_loop_condition.obs, by hand
MODEL_ANCHORS = (
    # WhileUp(3): three rounds, c = 4
    (("while", "++", "<", "P", "plain", 0, 3), (3, None, 4, 0)),
    # DoWhileDown(3): four rounds, a = -1
    (("do", "--", ">", "P", "plain", 3, 0), (4, None, -1, 0)),
    # ForCondition(5): five rounds, k = 6, q = 5
    (("for", "++", "<", "P", "plain", 0, 5), (5, None, 6, 5)),
)


def render(name, shape):
    loop, update, target, relation, form, body, (start, limit) = shape
    var = {"local": "v", "field": "@f", "element": "a[1]"}[target]
    init = {"local": "    v := start;\n", "field": "    @f := start;\n",
            "element": "    a := Int->New[3];\n    a[1] := start;\n"}[target]
    p = "%s%s %s limit" % (var, update, relation)
    g = "rounds < 2"
    r = "(%s %% 3) <> 0" % var
    cond = {"P": p, "G&P": "%s & %s" % (g, p), "P&G": "%s & %s" % (p, g), "G|P": "%s | %s" % (g, p),
            "P|G": "%s | %s" % (p, g), "P&R": "%s & %s" % (p, r)}[form]
    inner = "      rounds += 1;\n      if(rounds > %d) {\n        break;\n      };\n" % ROUND_CAP
    if body == "continue":
        inner += "      if(rounds = 2) {\n        continue;\n      };\n"
    elif body == "break":
        inner += "      if(rounds = 3) {\n        break;\n      };\n"
    inner += "      trace := (trace * 31 + %s + 100) %% %d;\n" % (var, TRACE_MOD)
    if loop == "while":
        text = "    while(%s) {\n%s    };\n" % (cond, inner)
    elif loop == "for":
        text = "    for(count := 0; %s; count += 1;) {\n%s    };\n" % (cond, inner)
    else:
        text = "    do {\n%s    }\n    while(%s);\n" % (inner, cond)
    return ("  method : %s(start : Int, limit : Int) ~ Nil {\n%s    rounds := 0;\n    trace := 0;\n    count := 0;\n%s"
            "    last := %s;\n    \"%s {$rounds} {$trace} {$last} {$count}\"->PrintLine();\n  }\n\n"
            % (name, init, text, var, name))


def program(named):
    calls = "".join("    s->%s(%d, %d);\n" % (name, shape[6][0], shape[6][1]) for name, shape in named)
    return ("class Loops {\n  @f : Int;\n\n  New() {\n    @f := 0;\n  }\n\n" +
            "".join(render(name, shape) for name, shape in named) +
            "  function : Main(args : String[]) ~ Nil {\n    s := Loops->New();\n" + calls + "  }\n}\n")


def expected(named):
    lines = []
    for name, shape in named:
        loop, update, target, relation, form, body, (start, limit) = shape
        lines.append("%s %d %d %d %d" % ((name,) + model(loop, update, relation, form, body, start, limit)))
    return lines


class Tools:
    def __init__(self, bin_dir):
        self.obc = os.path.join(bin_dir, "obc" + EXE)
        self.obr = os.path.join(bin_dir, "obr" + EXE)
        self.env = dict(os.environ, OBJECK_LIB_PATH=os.path.join(os.path.dirname(os.path.abspath(bin_dir)), "lib"))
        for var in ("OBJECK_JIT_THRESHOLD", "OBJECK_VERIFY_ONLY", "OBJECK_VM_ARGS"):
            self.env.pop(var, None)

    def compile(self, src, dest, opt):
        if os.path.exists(dest):
            os.remove(dest)
        p = subprocess.run([self.obc, "-src", src, "-dest", dest, "-opt", opt], capture_output=True, env=self.env,
                           timeout=TIMEOUT)
        text = (p.stdout + p.stderr).decode("utf-8", "replace")
        return p.returncode == 0 and os.path.exists(dest) and os.path.getsize(dest) > 0, text

    def run(self, image, args, extra):
        p = subprocess.run([self.obr] + args + [image], capture_output=True, env=dict(self.env, **extra),
                           timeout=TIMEOUT, stdin=subprocess.DEVNULL)
        return p.returncode, (p.stdout + p.stderr).decode("utf-8", "replace").replace("\r\n", "\n")


def check_fixture(tools, work):
    fails = []
    for opt in OPTS:
        image = os.path.join(work, "fixture_%s.obe" % opt)
        ok, text = tools.compile(FIXTURE, image, opt)
        if not ok:
            fails.append("-opt %s: postfix_update_loop_condition.obs does not compile: %s" % (opt, text.strip()[:300]))
            continue
        for mode, args, extra in MODES:
            try:
                rc, out = tools.run(image, args, extra)
            except subprocess.TimeoutExpired:
                fails.append("-opt %s %s: postfix_update_loop_condition.obs did not end in %d s" % (opt, mode, TIMEOUT))
                continue
            if rc != 0 or FIXTURE_PASS not in out:
                fails.append("-opt %s %s: postfix_update_loop_condition.obs exit %d: %s" % (opt, mode, rc, out.strip()[:600]))
    return fails


def check_generated(tools, work, named, index):
    """Compiles one generated program at every level, runs it in every mode. Returns (fails, runs, s0 != s3)."""
    fails = []
    src = os.path.join(work, "loops_%d.obs" % index)
    with open(src, "w", encoding="utf-8", newline="\n") as h:
        h.write(program(named))
    want = expected(named)
    shape_of = dict(named)
    runs = 0
    images = {}
    for opt in OPTS:
        image = os.path.join(work, "loops_%d_%s.obe" % (index, opt))
        ok, text = tools.compile(src, image, opt)
        if not ok:
            fails.append("program %d -opt %s does not compile: %s" % (index, opt, text.strip()[:400]))
            continue
        with open(image, "rb") as h:
            images[opt] = h.read()
        for mode, args, extra in MODES:
            try:
                rc, out = tools.run(image, args, extra)
            except subprocess.TimeoutExpired:
                fails.append("program %d -opt %s %s did not end in %d s" % (index, opt, mode, TIMEOUT))
                continue
            runs += 1
            got = out.split("\n")
            if got and got[-1] == "":
                got.pop()
            if rc != 0:
                fails.append("program %d -opt %s %s: exit %d: %s" % (index, opt, mode, rc, out.strip()[-300:]))
            if got == want:
                continue
            if len(got) != len(want):
                fails.append("program %d -opt %s %s: %d lines, one per loop is %d" % (index, opt, mode, len(got), len(want)))
            shown = 0
            for a, b in zip(got, want):
                if a != b:
                    name = b.split(" ")[0]
                    fails.append("program %d -opt %s %s: got %r, the rule gives %r (name rounds trace value count); loop %s"
                                 % (index, opt, mode, a, b, (shape_of[name],)))
                    shown += 1
                    if shown >= 4:
                        break
    differs = "s0" in images and "s3" in images and images["s0"] != images["s3"]
    return fails, runs, differs


def main():
    args = sys.argv[1:]
    keep = None
    if "--keep" in args:
        at = args.index("--keep")
        if at + 1 >= len(args):
            print(__doc__)
            return 2
        keep = args[at + 1]
        del args[at:at + 2]
    if len(args) != 1:
        print(__doc__)
        return 2
    tools = Tools(os.path.abspath(args[0]))
    for tool in (tools.obc, tools.obr):
        if not os.path.isfile(tool):
            print("  [FAIL] %s is missing" % tool)
            return 2

    for model_args, want in MODEL_ANCHORS:
        got = model(*model_args)
        if (got[0], got[2], got[3]) != (want[0], want[2], want[3]):
            print("  [FAIL] the model gives %r for %r; by hand it is rounds %d, value %d, count %d"
                  % (got, model_args, want[0], want[2], want[3]))
            return 1

    shapes = list(itertools.product(LOOPS, UPDATES, TARGETS, RELATIONS, FORMS, BODIES, PAIRS))
    named = [("L%d" % i, shape) for i, shape in enumerate(shapes)]

    fails = []
    with tempfile.TemporaryDirectory(prefix="loop-condition-postfix-") as temp:
        work = keep or temp
        os.makedirs(work, exist_ok=True)

        fixture_fails = check_fixture(tools, work)
        fails += fixture_fails
        if not fixture_fails:
            print("  [PASS] postfix_update_loop_condition.obs at -opt s0..s3, interpreter and JIT (%d runs)"
                  % (len(OPTS) * len(MODES)))

        runs = 0
        levels_differ = None
        for index, at in enumerate(range(0, len(named), PER_PROGRAM)):
            part_fails, part_runs, differs = check_generated(tools, work, named[at:at + PER_PROGRAM], index)
            fails += part_fails
            runs += part_runs
            if index == 0:
                levels_differ = differs
        if levels_differ is False:
            fails.append("the -opt s0 and -opt s3 images of the first generated program are the same bytes: "
                         "the levels were not different builds")
        generated_fails = len(fails) - len(fixture_fails)
        if not generated_fails:
            print("  [PASS] %d generated loops print what the rule gives at -opt s0..s3, interpreter and JIT (%d runs)"
                  % (len(named), runs))

    for line in fails[:40]:
        print("  [FAIL] " + line)
    if len(fails) > 40:
        print("  ... and %d more" % (len(fails) - 40))
    if fails:
        print("  [FAIL] a postfix update in a loop condition does not follow the documented rule")
        return 1
    return 0


if __name__ == "__main__":
    sys.exit(main())
