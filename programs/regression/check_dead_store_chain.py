#!/usr/bin/env python3
"""
A dead store never leaves its value on the operand stack, whatever the shape.

Usage: check_dead_store_chain.py <bin_dir> [--all] [--jobs N] [--keep DIR]

<bin_dir> must contain obc and obr (with .exe on Windows); the libraries are
read from the sibling lib/ directory, as check_lib_array_copy.py does.

The optimizer's dead-store pass removes a store nobody reads, together with the
instructions that computed its value. It found those instructions by the
statement they are tagged with, and in a chained assignment the tags do not
line up with the values: in `a := b := E` the store to `b` carries the inner
assignment while E carries the outer statement. A dead `b` therefore lost its
store and kept E: one stray word on the operand stack. Where that path joins
another the load-time verifier refuses the method ("stack heights differ where
control flow joins"); in straight-line code the word stays on the stack for the
rest of the method. `-opt s1` and above; `-opt s0` does not run the pass.

    seen := false;
    each(i : 3) { if(i = 1) { seen := unused := true; }; };

This check generates assignments, each a method of one class, over:
  - the chain length (1 to 3 targets) and, per target, what it is: a local read
    afterwards, a local never read, a local overwritten before it is read, a
    local first named by the chain itself (read or not), a field, an array
    element
  - the value's type (Int, Float, Bool, Char, Byte, String, an object, a boxed
    Int, an Int array, and a Float target fed from an Int link)
  - the value: a literal, an expression over a parameter, a call (counted, so a
    value evaluated twice or not at all shows), a field
  - where the statement sits: straight-line, in an if, an else, a while, a for,
    an each, a do/while, a select, before an early return, and as a method's
    very first statement
and requires of every program, compiled at -opt s0, s1, s2 and s3:
  1. obc compiles it at every level or at none (a shape the language does not
     accept is dropped and counted; it is not a pass),
  2. the VM's load-time verifier accepts the image (OBJECK_VERIFY_ONLY=1),
  3. the output and exit code are the same across the four levels, each run with
     --jit=off, with the default JIT and with OBJECK_JIT_THRESHOLD=1.

Without --all a fixed sample runs (a stride through the full list, so every
target kind, type, value and context appears); --all runs every combination.

Things that would let a broken check pass, each of which fails here:
  - the verifier not running: a control image must load silently under
    OBJECK_VERIFY_ONLY=1 and print when run without it,
  - no shape surviving: fewer than 90% of the generated shapes compiling is an
    error, and so is a run whose reference output has no line for a shape,
  - -opt levels that are all the same build: the s0 and s3 images of the first
    program must differ.
"""

import itertools
import os
import subprocess
import sys
import tempfile
from concurrent.futures import ThreadPoolExecutor

EXE = ".exe" if os.name == "nt" else ""
TIMEOUT = 300
OPTS = ("s0", "s1", "s2", "s3")
MODES = (("jitoff", ["--jit=off"], {}), ("default", [], {}), ("jit1", [], {"OBJECK_JIT_THRESHOLD": "1"}))
PER_PROGRAM = 60

# type: declared type per chain position, zero, values (literal, expression, call, field),
# the overwrite value, how to print, whether an array of it is a target kind
TYPES = {
    "Int": dict(decl="Int", vals=["41", "(k * 3 + 1)", "NextI()", "@gi"], alt="-9", show="{}", ary="Int->New[4]"),
    "Float": dict(decl="Float", vals=["2.5", "(k->As(Float) * 1.5)", "NextF()", "@gf"], alt="-9.5", show="{}",
                  ary="Float->New[4]"),
    "Bool": dict(decl="Bool", vals=["true", "(k > 1)", "NextB()", "@gb"], alt="false", show="{}", ary="Bool->New[4]"),
    "Char": dict(decl="Char", vals=["'x'", "NextC()", "@gc"], alt="'q'", show="{}", ary="Char->New[4]"),
    "Byte": dict(decl="Byte", vals=["7->As(Byte)", "(k + 300)->As(Byte)", "NextY()", "@gy"], alt="3->As(Byte)",
                 show="{}->As(Int)", ary="Byte->New[4]"),
    "String": dict(decl="String", vals=['"lit"', "NextS()", "@gs"], alt='"alt"', show="{}", ary="String->New[4]"),
    "Box": dict(decl="Box", vals=["Box->New(5)", "NextO()", "@go"], alt="Box->New(-9)", show="{}->Get()",
                ary="Box->New[4]"),
    "IntRef": dict(decl="IntRef", vals=["IntRef->New(6)", "NextR()", "@gr"], alt="IntRef->New(-9)", show="{}->Get()",
                   ary=None),
    "IntAry": dict(decl="Int[]", vals=["Int->New[3]", "NextA()", "@ga"], alt="Int->New[9]", show="{}->Size()",
                   ary=None),
    # the first target is a Float, the links after it Int: the chain converts on the way
    "IntToFloat": dict(decl="Int", decl0="Float", vals=["41", "(k * 3 + 1)", "NextI()", "@gi"], alt="-9", alt0="-9.5",
                       show="{}", ary=None),
}
FIELD = {"Int": "@fi{}", "Float": "@ff{}", "Bool": "@fb{}", "Char": "@fc{}", "Byte": "@fy{}", "String": "@fs{}",
         "Box": "@fo{}", "IntRef": "@fr{}", "IntAry": "@fa{}"}

# L read afterwards; D never read; O overwritten, then read; N first named here, never read;
# M first named here, read in the same block; F a field; A an array element
KINDS = "LDONMFA"
CONTEXTS = ("plain", "if", "else", "while", "for", "each", "do", "select", "return", "first")

PRELUDE = """class Box {
  @v : Int;
  New(v : Int) { @v := v; }
  method : public : Get() ~ Int { return @v; }
}

class Shapes {
  @calls : Int;
  @gi : Int; @gf : Float; @gb : Bool; @gc : Char; @gy : Byte; @gs : String; @go : Box; @gr : IntRef; @ga : Int[];
  @fi0 : Int; @fi1 : Int; @fi2 : Int;
  @ff0 : Float; @ff1 : Float; @ff2 : Float;
  @fb0 : Bool; @fb1 : Bool; @fb2 : Bool;
  @fc0 : Char; @fc1 : Char; @fc2 : Char;
  @fy0 : Byte; @fy1 : Byte; @fy2 : Byte;
  @fs0 : String; @fs1 : String; @fs2 : String;
  @fo0 : Box; @fo1 : Box; @fo2 : Box;
  @fr0 : IntRef; @fr1 : IntRef; @fr2 : IntRef;
  @fa0 : Int[]; @fa1 : Int[]; @fa2 : Int[];

  New() {
    @gi := 17; @gf := 1.25; @gb := true; @gc := 'g'; @gy := 9->As(Byte); @gs := "fld"; @go := Box->New(8);
    @gr := IntRef->New(4); @ga := Int->New[5];
  }

  method : Tick() ~ Nil { @calls += 1; }
  method : NextI() ~ Int { @calls += 1; return @calls * 7; }
  method : NextF() ~ Float { @calls += 1; return @calls->As(Float) * 0.5; }
  method : NextB() ~ Bool { @calls += 1; return @calls % 2 = 1; }
  method : NextC() ~ Char { @calls += 1; return 'c'; }
  method : NextY() ~ Byte { @calls += 1; return (@calls + 250)->As(Byte); }
  method : NextS() ~ String { @calls += 1; return "call"; }
  method : NextO() ~ Box { @calls += 1; return Box->New(@calls); }
  method : NextR() ~ IntRef { @calls += 1; return IntRef->New(@calls); }
  method : NextA() ~ Int[] { @calls += 1; return Int->New[@calls % 5 + 1]; }
"""


def all_shapes():
    """(type, kinds, value index, context) for every combination that can be written"""
    for tname, t in TYPES.items():
        for n in (1, 2, 3):
            for kinds in itertools.product(KINDS, repeat=n):
                if "A" in kinds and not t["ary"]:
                    continue
                if "F" in kinds and tname == "IntToFloat":
                    continue
                # at most one target named by the chain itself: two would be the same shape twice
                if sum(1 for c in kinds if c in "NM") > 1:
                    continue
                if n == 3 and sum(1 for c in kinds if c in "FA") > 1:
                    continue
                for vi in range(len(t["vals"])):
                    for ctx in CONTEXTS:
                        if ctx == "first" and ("N" not in kinds and "M" not in kinds and "F" not in kinds):
                            # a method's first statement cannot follow a declaration
                            continue
                        if ctx == "first" and any(c in "LDOA" for c in kinds):
                            continue
                        yield tname, "".join(kinds), vi, ctx


def render(idx, shape):
    tname, kinds, vi, ctx = shape
    t = TYPES[tname]
    name = "S%d" % idx
    decls, targets, after, inside = [], [], [], []
    for j, kind in enumerate(kinds):
        decl = t.get("decl0", t["decl"]) if j == 0 else t["decl"]
        alt = t.get("alt0", t["alt"]) if j == 0 else t["alt"]
        v = "v%d" % j
        if kind in "LDO":
            decls.append("%s : %s;" % (v, decl))
            targets.append(v)
            if kind == "O":
                after.append("%s := %s;" % (v, alt))
            if kind in "LO":
                after.append(("' '->Print(); " + t["show"] + "->Print();").format(v))
        elif kind in "NM":
            targets.append(v)
            if kind == "M":
                inside.append(("' '->Print(); " + t["show"] + "->Print();").format(v))
        elif kind == "F":
            f = FIELD[tname].format(j)
            targets.append(f)
            after.append(("' '->Print(); " + t["show"] + "->Print();").format(f))
        else:
            decls.append("a%d := %s;" % (j, t["ary"]))
            targets.append("a%d[%d]" % (j, j + 1))
            after.append(("' '->Print(); " + t["show"] + "->Print();").format("a%d[%d]" % (j, j + 1)))
    assign = " := ".join(targets) + " := " + t["vals"][vi] + ";"
    chain = assign + " " + " ".join(inside)
    head = '"%s"->Print();' % name
    tail = " ".join(after) + " ' '->Print(); @calls->PrintLine();"
    d = " ".join(decls)
    if ctx == "plain":
        body = "%s %s %s %s" % (d, head, chain, tail)
    elif ctx == "first":
        body = "%s %s %s %s" % (assign, head, " ".join(inside), tail)
    elif ctx == "if":
        body = "%s %s if(k > 1) { %s }; %s" % (d, head, chain, tail)
    elif ctx == "else":
        body = "%s %s if(k > 5) { Tick(); } else { %s }; %s" % (d, head, chain, tail)
    elif ctx == "while":
        body = "%s %s i := 0; while(i < 3) { %s i += 1; }; %s" % (d, head, chain, tail)
    elif ctx == "for":
        body = "%s %s for(i := 0; i < 3; i += 1;) { %s }; %s" % (d, head, chain, tail)
    elif ctx == "each":
        body = "%s %s each(i : 3) { if(i = 1) { %s }; }; %s" % (d, head, chain, tail)
    elif ctx == "do":
        body = "%s %s i := 0; do { %s i += 1; } while(i < 2); %s" % (d, head, chain, tail)
    elif ctx == "select":
        body = "%s %s select(k) { label 2: { %s } other: { Tick(); } }; %s" % (d, head, chain, tail)
    else:
        body = "%s %s if(k > 1) { %s %s return; }; ' '->Print(); \"not reached\"->PrintLine();" % (d, head, chain, tail)
    return name, "  # %s %s value %d, %s\n  method : %s(k : Int) ~ Nil { %s }\n" % (tname, kinds, vi, ctx, name, body)


def program(methods):
    calls = "".join("    s->%s(2);\n" % name for name, _ in methods)
    return (PRELUDE + "\n" + "".join(text for _, text in methods) +
            "\n  function : Main(args : String[]) ~ Nil {\n    s := Shapes->New();\n" + calls + "  }\n}\n")


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
        if p.returncode < 0:
            raise RuntimeError("obc died of signal %d on %s at -opt %s" % (-p.returncode, src, opt))
        return p.returncode == 0 and os.path.exists(dest) and os.path.getsize(dest) > 0, text

    def verify(self, image):
        p = subprocess.run([self.obr, image], capture_output=True, env=dict(self.env, OBJECK_VERIFY_ONLY="1"),
                           timeout=TIMEOUT, stdin=subprocess.DEVNULL)
        return p.returncode, (p.stdout + p.stderr).decode("utf-8", "replace").strip()

    def run(self, image, args, extra):
        p = subprocess.run([self.obr] + args + [image], capture_output=True, env=dict(self.env, **extra),
                           timeout=TIMEOUT, stdin=subprocess.DEVNULL)
        return p.returncode, (p.stdout + p.stderr).decode("utf-8", "replace").replace("\r\n", "\n")


def bad_lines(text):
    """line numbers obc reported, from 'file.obs:(12,3): ...'"""
    import re
    return {int(m) for m in re.findall(r"\.obs:\((\d+),", text)}


def settle(tools, work, tag, shapes, first_idx):
    """Drops the shapes the language does not accept, using the -opt s0 compiler's line numbers.
    Returns (source path, surviving [(name, shape)], dropped count)."""
    methods = [render(first_idx + i, s) for i, s in enumerate(shapes)]
    keep = list(zip(methods, shapes))
    src = os.path.join(work, "%s.obs" % tag)
    dropped = 0
    for _ in range(len(shapes) + 1):
        text = program([m for m, _ in keep])
        with open(src, "w", encoding="utf-8", newline="\n") as f:
            f.write(text)
        ok, out = tools.compile(src, os.path.join(work, "%s__probe.obe" % tag), "s0")
        if ok:
            return src, [(m[0], s) for m, s in keep], dropped
        lines = text.split("\n")
        hit = set()
        for ln in bad_lines(out):
            # the method a reported line belongs to: one method per line, after its comment
            if 0 < ln <= len(lines):
                for k, (m, _) in enumerate(keep):
                    if ("method : %s(" % m[0]) in lines[ln - 1]:
                        hit.add(k)
        if not hit:
            raise RuntimeError("obc refused %s without naming a shape:\n%s" % (src, out[:2000]))
        dropped += len(hit)
        keep = [x for k, x in enumerate(keep) if k not in hit]
    raise RuntimeError("no shape of %s compiles" % src)


def one_program(tools, work, tag, shapes, first_idx):
    """Returns (failures, shapes kept, shapes dropped)."""
    src, kept, dropped = settle(tools, work, tag, shapes, first_idx)
    fails = []
    if not kept:
        return fails, 0, dropped
    names = {n: s for n, s in kept}
    ref = None
    for opt in OPTS:
        image = os.path.join(work, "%s__%s.obe" % (tag, opt))
        ok, out = tools.compile(src, image, opt)
        if not ok:
            fails.append("%s: compiles at -opt s0 but not at -opt %s: %s" % (src, opt, out.strip()[:300]))
            continue
        rc, text = tools.verify(image)
        if rc != 0 or text:
            fails.append("%s -opt %s: the load-time verifier refuses the image (exit %d): %s" % (src, opt, rc, text[:300]))
            continue
        for mode, args, extra in MODES:
            rc, text = tools.run(image, args, extra)
            got = (rc, text)
            if ref is None:
                ref = got
                seen = {line.split(" ")[0] for line in text.split("\n")}
                missing = [n for n in names if n not in seen]
                if rc != 0 or missing:
                    fails.append("%s -opt %s %s: the reference run exits %d and prints no line for %d shape(s): %s" %
                                 (src, opt, mode, rc, len(missing), text[-300:]))
            elif got != ref:
                a, b = ref[1].split("\n"), text.split("\n")
                where = next((i for i in range(min(len(a), len(b))) if a[i] != b[i]), min(len(a), len(b)))
                want = a[where] if where < len(a) else "<nothing>"
                have = b[where] if where < len(b) else "<nothing>"
                shape = names.get(want.split(" ")[0]) or names.get(have.split(" ")[0])
                fails.append("%s -opt %s %s: differs from -opt s0 --jit=off (exit %d, reference %d) at %r, reference %r; shape %s" %
                             (src, opt, mode, rc, ref[0], have[:120], want[:120], shape))
    return fails, len(kept), dropped


def control(tools, work):
    src = os.path.join(work, "control.obs")
    with open(src, "w", encoding="utf-8", newline="\n") as f:
        f.write('class Control { function : Main(args : String[]) ~ Nil { "ran"->PrintLine(); } }\n')
    image = os.path.join(work, "control.obe")
    ok, out = tools.compile(src, image, "s3")
    if not ok:
        return "the control program does not compile: " + out[:300]
    rc, text = tools.verify(image)
    if rc != 0 or text:
        return "the control image does not load silently under OBJECK_VERIFY_ONLY=1 (exit %d): %s" % (rc, text[:200])
    rc, text = tools.run(image, [], {})
    if rc != 0 or text.strip() != "ran":
        return "the control image does not run (exit %d): %s" % (rc, text[:200])
    return None


def main():
    args = sys.argv[1:]
    if not args or args[0].startswith("-"):
        print(__doc__)
        return 2
    bin_dir = args[0]
    full = "--all" in args
    jobs = int(args[args.index("--jobs") + 1]) if "--jobs" in args else min(8, os.cpu_count() or 2)
    keep = args[args.index("--keep") + 1] if "--keep" in args else None
    tools = Tools(bin_dir)
    for tool in (tools.obc, tools.obr):
        if not os.path.exists(tool):
            print("FAIL: %s not found" % tool)
            return 2

    shapes = list(all_shapes())
    total = len(shapes)
    if not full:
        # a stride that is coprime to the number of values and of contexts, so both rotate
        shapes = shapes[::37]
    tmp = None
    if keep:
        os.makedirs(keep, exist_ok=True)
        work = os.path.abspath(keep)
    else:
        tmp = tempfile.TemporaryDirectory(prefix="dead_store_chain_")
        work = tmp.name
    try:
        problem = control(tools, work)
        if problem:
            print("FAIL: " + problem)
            return 1
        chunks = [(i // PER_PROGRAM, shapes[i:i + PER_PROGRAM], i) for i in range(0, len(shapes), PER_PROGRAM)]
        fails, kept, dropped = [], 0, 0
        with ThreadPoolExecutor(jobs) as ex:
            for f, k, d in ex.map(lambda c: one_program(tools, work, "shapes_%04d" % c[0], c[1], c[2]), chunks):
                fails += f
                kept += k
                dropped += d
        a = open(os.path.join(work, "shapes_0000__s0.obe"), "rb").read() if os.path.exists(
            os.path.join(work, "shapes_0000__s0.obe")) else b""
        b = open(os.path.join(work, "shapes_0000__s3.obe"), "rb").read() if os.path.exists(
            os.path.join(work, "shapes_0000__s3.obe")) else b""
        if a and a == b:
            fails.append("the -opt s0 and -opt s3 images of the first program are the same file: -opt is not honoured")
        if kept < 0.9 * len(shapes):
            fails.append("only %d of %d generated shapes compile: the generator no longer matches the language" %
                         (kept, len(shapes)))
        print("dead store shapes: %d of %d combinations generated, %d compiled, %d not accepted by the language; "
              "%d programs x %d -opt levels x %d run modes" % (len(shapes), total, kept, dropped, len(chunks), len(OPTS),
                                                               len(MODES)))
        for f in fails[:40]:
            print("FAIL: " + f)
        if len(fails) > 40:
            print("... and %d more" % (len(fails) - 40))
        print("FAILED (%d)" % len(fails) if fails else "PASS")
        return 1 if fails else 0
    finally:
        if tmp:
            tmp.cleanup()


if __name__ == "__main__":
    sys.exit(main())
