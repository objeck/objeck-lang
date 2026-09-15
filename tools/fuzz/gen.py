"""Typed-AST program generator for the Objeck differential fuzzer.

Every decision the generator makes is one draw from a Choices stream. Run from
a seed, the draws come from random.Random(seed) and are recorded; replayed, they
come from a recorded list. The reducer shrinks that list (delete, zero, lower),
and because the generator can only ever build valid programs, every shrunk
list is still a valid program. Draw value 0 is always the simplest option: the
fewest statements, a leaf instead of an operator, a feature switched off.

Programs are deterministic and self-contained: no time, no random numbers, no
arguments, no files. Each top-level function Fi prints one Int digest line, and
Main calls it 12 times so the default JIT threshold (10 calls) is crossed.

Undefined or unspecified integer behaviour is avoided by construction, until
integer semantics are settled (plan: "Build the fuzzer, then settle integer
semantics"):
  * every Int variable holds |v| <= 2^20; each expression carries a magnitude
    bound, operands are wrapped (`% 1048573`, `and 1048575`) before any
    operator whose result could pass 2^60, and a stored value is wrapped back
    under 2^20;
  * shift counts are masked or literal and within 0-63, left shifts small;
  * divisors are non-zero literals or `((e and 255) + 1)`-shaped;
  * array indexes are masked into power-of-two sizes; object reads are
    Nil-checked; SubString is bounds-checked; loops have literal bounds and
    while/do-while counters are incremented first, so `continue` cannot hang.
  * Floats stay finite: no division by a value that can be zero, Sqrt of an
    absolute value, magnitudes wrapped under 1e6 through an Int conversion.

JIT limits are respected: at most ~60 local slots per method (LOCAL_SIZE 768
bytes; 96 Int locals fails) and flat expressions of at most 8 operands.

Feature layers (swarm testing: each program samples a subset, F1 always on):
  F1 ints, locals, if, for/while/do-while, break/continue, 1-D and 2-D Int
     arrays, Size()
  F2 floats
  F3 static functions calling each other, recursion, many-argument calls,
     virtual dispatch over a small class hierarchy
  F4 select (dense, sparse, negative labels) and strings (concat,
     interpolation, SubString, Size, Get)
  F5 static function references and zero-argument closures (the explicit
     FuncRef->New(\\() ~ IntRef : () => ...)<IntRef> form and bare
     \\() => ...; lambdas with parameters crash obc 9.4, C1)
  F6 allocation-heavy object graphs: linked lists, trees, object arrays

Knobs for known bugs: basic_lambdas=True also emits `\() ~ Int : () => e`
(a zero-argument lambda returning a basic type), which v2026.9.4 compiles but
whose call crashes the JIT (findings/jit1_basic_lambda.obs). It is off by
default because a native crash carries no message, so known.json could not
tell it from any other JIT access violation. Its draw is consumed either way,
so recorded choices replay the same with the knob on or off.
"""

import random

FEATURES = ("F1", "F2", "F3", "F4", "F5", "F6")
MAIN_CLASS = "FuzzProgram"
CLASS_PREFIXES = (MAIN_CLASS + ":", "Fz")

VB = 1 << 20            # bound every Int variable keeps
LIMIT = 1 << 60         # no intermediate Int result may pass this
VF = 1.0e6              # bound every Float variable keeps
FLIMIT = 1.0e15         # no intermediate Float may pass this
MAX_LEAVES = 8          # operands in one flat expression
MAX_DEPTH = 3           # operator nesting
CALLS = 12              # calls per top-level function from Main
COST_BUDGET = 4000      # rough work units per function body


class Choices:
    """A recorded stream of bounded integer draws."""

    def __init__(self, seed=None, replay=None):
        self.replay = list(replay) if replay is not None else None
        self.rng = random.Random(seed)
        self.record = []

    def draw(self, n):
        """An int in [0, n). Replayed values are reduced modulo n; past the end
        of a replayed list every draw is 0 (the simplest option)."""
        if n <= 1:
            v = 0
        elif self.replay is not None:
            i = len(self.record)
            v = self.replay[i] % n if i < len(self.replay) else 0
        else:
            v = self.rng.randrange(n)
        self.record.append(v)
        return v

    def chance(self, pct):
        """True with probability pct%; a 0 draw is False."""
        return self.draw(100) >= 100 - pct

    def count(self, lo, hi):
        return lo + self.draw(hi - lo + 1)

    def pick(self, items):
        return items[self.draw(len(items))]

    def weighted(self, pairs):
        """pairs: [(weight, value)], simplest first; weights <= 0 are skipped."""
        pairs = [(w, v) for w, v in pairs if w > 0]
        total = sum(w for w, _ in pairs)
        x = self.draw(total)
        for w, v in pairs:
            if x < w:
                return v
            x -= w
        return pairs[-1][1]


class Program:
    def __init__(self, text, choices, features, methods, seed):
        self.text = text
        self.choices = choices
        self.features = features
        self.methods = methods
        self.seed = seed


class E:
    """A generated expression: text, magnitude bound, operand count."""

    __slots__ = ("t", "b", "n")

    def __init__(self, t, b, n=1):
        self.t = t
        self.b = b
        self.n = n


def lit(v):
    return str(v) if v >= 0 else "(0 - %d)" % -v


def flit(v):
    s = repr(float(v))
    return s if v >= 0 else "(0.0 - %s)" % repr(float(-v))


def next_pow2(b):
    p = 1
    while p <= b:
        p <<= 1
    return p


class Func:
    def __init__(self, name, nparams, cost):
        self.name = name
        self.nparams = nparams
        self.cost = cost


class Ctx:
    """What a function body can see while it is being generated."""

    def __init__(self, params):
        self.params = params
        self.ints = list(params)          # readable Int names
        self.assignable = []              # Int locals a statement may write
        self.counters = []                # loop counters currently in scope (read-only)
        self.free_counters = []           # (kind, name) available for new loops
        self.arrays = []                  # (name, size)
        self.grids = []                   # (name, rows, cols)
        self.floats = []
        self.farrays = []                 # (name, size)
        self.strings = []
        self.funcref = None
        self.shapes = None
        self.lists = False
        self.trees = False
        self.objarray = False
        self.loop_depth = 0
        self.mult = 1
        self.cost = 0
        self.callees = []                 # Func values callable from here
        self.closure_n = 0
        self.each_n = 0
        self.in_lambda = False


class Generator:
    def __init__(self, choices, features=None, seed=None, basic_lambdas=False):
        self.c = choices
        self.seed = seed
        self.allow_basic_lambdas = basic_lambdas
        self.forced = features
        self.methods = []
        self.used = set()

    # -- features ---------------------------------------------------------
    def pick_features(self):
        on = ["F1"]
        for f in FEATURES[1:]:
            drawn = self.c.chance(50)
            if self.forced is not None:
                drawn = f in self.forced
            if drawn:
                on.append(f)
        return on

    def has(self, f):
        return f in self.features

    # -- program ----------------------------------------------------------
    def program(self):
        self.features = self.pick_features()
        drawn = self.c.chance(15)
        self.basic_lambdas = self.allow_basic_lambdas and self.has("F5") and drawn
        out = []
        out.append("#~")
        out.append("Generated by tools/fuzz/gen.py (seed=%s, features=%s)." %
                   (self.seed if self.seed is not None else "replay", ",".join(self.features)))
        out.append("Deterministic; prints one digest line per top-level function.")
        out.append("~#")
        out.append("")
        if self.has("F3"):
            out.extend(self.shape_classes())
        if self.has("F6"):
            out.extend(self.graph_classes())

        body = []
        self.funcs = []
        if self.has("F5"):
            body.extend(self.funcref_helpers())
        if self.has("F3"):
            body.extend(self.recursive_helpers())
        nfuncs = self.c.count(1, 6)
        for i in range(nfuncs):
            body.extend(self.top_function(i))
        body.extend(self.main(nfuncs))
        out.append("class %s {" % MAIN_CLASS)
        out.extend(body)
        out.append("}")
        out.append("")
        methods = sorted(m for m in self.methods if m in self.used or m.startswith(MAIN_CLASS + ":F"))
        return "\n".join(out), methods

    def main(self, nfuncs):
        lines = ["  function : Main(args : String[]) ~ Nil {", "    d := 0;", "    r := 0;"]
        for i in range(nfuncs):
            f = self.top[i]
            args = []
            for k in range(f.nparams):
                args.append(self.c.pick(["r", "(r * %d - %d)" % (k + 3, k + 7), "(d and 65535)",
                                         "(%d - r)" % (k * 11 + 1)]))
            lines.append("    d := 0;")
            lines.append("    for(r := 0; r < %d; r += 1;) {" % CALLS)
            lines.append("      d := (d * 31 + %s(%s)) %% 1000000007;" % (f.name, ", ".join(args)))
            lines.append("    };")
            lines.append('    "%s="->Print(); d->PrintLine();' % f.name)
        lines.append("  }")
        return lines

    # -- classes ----------------------------------------------------------
    def shape_classes(self):
        n = self.c.count(2, 4)
        self.shape_subs = []
        a_k = self.c.count(1, 9)
        lines = [
            "class FzShape {",
            "  @a : Int;",
            "  New(a : Int) { @a := a; }",
            "  method : virtual : public : V(x : Int) ~ Int;",
            "  method : public : M(x : Int) ~ Int { @a := (@a * %d + x) %% 1048573; return @a; }" % a_k,
            "  method : public : Twice(x : Int) ~ Int { return (V(x) + V(x + 1)) % 1048573; }",
            "}",
            "",
        ]
        self.methods += ["FzShape:M", "FzShape:Twice"]
        for i in range(n):
            name = "FzSub%d" % i
            parent = "FzShape"
            inherit = i > 0 and self.c.chance(25)
            if inherit:
                parent = "FzSub%d" % self.c.draw(i)
            lines.append("class %s from %s {" % (name, parent))
            if parent == "FzShape":
                lines.append("  @b : Int;")
                lines.append("  New(a : Int, b : Int) { Parent(a); @b := b; }")
            else:
                lines.append("  New(a : Int, b : Int) { Parent(a, b); }")
            if not inherit:
                ctx = Ctx(["x"])
                ctx.ints = ["x", "@a", "@b"]
                ctx.assignable = ["@b"]
                body = self.block(ctx, 2, 2, allow_calls=False)
                lines.append("  method : public : V(x : Int) ~ Int {")
                lines += ["  " + l for l in body]
                lines.append("    return %s;" % self.wrap(self.int_expr(ctx, 2, 4)).t)
                lines.append("  }")
                self.methods.append("%s:V" % name)
            lines.append("}")
            lines.append("")
            self.shape_subs.append(name)
        return lines

    def graph_classes(self):
        k1 = self.c.count(1, 7)
        k2 = self.c.count(1, 13)
        extra = self.c.chance(50)
        lines = [
            "class FzNode {",
            "  @val : Int;",
            "  @next : FzNode;",
        ]
        if extra:
            lines += ["  @pad : Float;", "  @tag : Int[];"]
        lines.append("  New(v : Int, n : FzNode) {")
        lines.append("    @val := v; @next := n;")
        if extra:
            lines.append("    @pad := v->As(Float) * 0.5; @tag := Int->New[(v and 3) + 1]; @tag[0] := v;")
        lines += [
            "  }",
            "  method : public : GetVal() ~ Int { return @val; }",
            "  method : public : SetVal(v : Int) ~ Nil { @val := v; }",
            "  method : public : GetNext() ~ FzNode { return @next; }",
            "  method : public : SetNext(n : FzNode) ~ Nil { @next := n; }",
            "}",
            "",
            "class FzTree {",
            "  @v : Int; @l : FzTree; @r : FzTree;",
            "  New(v : Int, l : FzTree, r : FzTree) { @v := v; @l := l; @r := r; }",
            "  function : Build(d : Int, v : Int) ~ FzTree {",
            "    if(d <= 0) { return FzTree->New(v, Nil, Nil); };",
            "    return FzTree->New(v, Build(d - 1, (v * 2 + %d) %% 1009), Build(d - 1, (v * 3 + %d) %% 1013));" % (k1, k2),
            "  }",
            "  method : public : Check() ~ Int {",
            "    s := @v;",
            "    if(@l <> Nil) { s := (s + @l->Check() * 3) % 1048573; };",
            "    if(@r <> Nil) { s := (s * 5 + @r->Check()) % 1048573; };",
            "    return s;",
            "  }",
            "}",
            "",
        ]
        self.methods += ["FzNode:GetVal", "FzNode:SetVal", "FzNode:GetNext", "FzNode:SetNext",
                         "FzTree:Build", "FzTree:Check"]
        return lines

    # -- helpers in the main class ---------------------------------------
    def funcref_helpers(self):
        lines = []
        self.gfuncs = []
        for k in range(self.c.count(1, 3)):
            name = "G%d" % k
            ctx = Ctx(["x"])
            lines.append("  function : %s(x : Int) ~ Int {" % name)
            lines.append("    return %s;" % self.wrap(self.int_expr(ctx, 2, 5)).t)
            lines.append("  }")
            lines.append("")
            self.gfuncs.append(name)
            self.methods.append("%s:%s" % (MAIN_CLASS, name))
        lines.append("  function : Apply(f : (Int) ~ Int, x : Int) ~ Int {")
        lines.append("    return (f(x) + f(x + %d)) %% 1048573;" % self.c.count(1, 5))
        lines.append("  }")
        lines.append("")
        self.methods.append("%s:Apply" % MAIN_CLASS)
        return lines

    def recursive_helpers(self):
        lines = []
        self.rfuncs = []
        for k in range(self.c.count(1, 2)):
            name = "R%d" % k
            binary = self.c.chance(40)
            ctx = Ctx(["n", "x"])
            step = self.wrap(self.int_expr(ctx, 2, 4)).t
            extra = self.wrap(self.int_expr(ctx, 1, 3)).t
            lines.append("  function : %s(n : Int, x : Int) ~ Int {" % name)
            lines.append("    if(n <= 0) { return x; };")
            if binary:
                lines.append("    return (%s(n - 1, %s) + %s(n - 2, x) + %s) %% 1048573;" % (name, step, name, extra))
            else:
                lines.append("    return (%s(n - 1, %s) * 3 + %s) %% 1048573;" % (name, step, extra))
            lines.append("  }")
            lines.append("")
            # depth is masked to 0-7 at every call site
            self.rfuncs.append(Func(name, 2, 256 if binary else 8))
            self.methods.append("%s:%s" % (MAIN_CLASS, name))
        return lines

    # -- top-level functions ---------------------------------------------
    def top_function(self, i):
        if not hasattr(self, "top"):
            self.top = []
        many = self.has("F3") and self.c.chance(20)
        nparams = self.c.count(5, 10) if many else self.c.count(1, 3)
        params = ["p%d" % k for k in range(nparams)]
        ctx = Ctx(params)
        ctx.callees = list(self.top) if self.has("F3") else []
        if self.has("F3"):
            ctx.callees += getattr(self, "rfuncs", [])
        lines = ["  function : F%d(%s) ~ Int {" % (i, ", ".join("%s : Int" % p for p in params))]
        decl = []
        nints = self.c.count(1, 5)
        for k in range(nints):
            name = "v%d" % k
            init = self.c.pick(params + [lit(self.c.count(0, 99))])
            decl.append("%s := %s;" % (name, init))
            ctx.ints.append(name)
            ctx.assignable.append(name)
        for k in range(3):
            decl.append("i%d := 0; w%d := 0;" % (k, k))
            ctx.free_counters.append(("i%d" % k, "w%d" % k))
        for k in range(self.c.count(0, 2)):
            size = self.c.pick([16, 32, 8, 64])
            name = "a%d" % k
            decl.append("%s := Int->New[%d];" % (name, size))
            decl.append("for(i0 := 0; i0 < %s->Size(); i0 += 1;) { %s[i0] := (i0 * %d + %s) %% 1009; };" %
                        (name, name, self.c.count(1, 97), self.c.pick(params)))
            ctx.arrays.append((name, size))
        if self.c.chance(40):
            rows, cols = self.c.pick([(4, 8), (2, 2), (8, 4), (16, 16)])
            decl.append("g0 := Int->New[%d, %d];" % (rows, cols))
            decl.append("for(i0 := 0; i0 < %d; i0 += 1;) { for(i1 := 0; i1 < %d; i1 += 1;) { g0[i0, i1] := (i0 * %d + i1) %% 257; }; };" %
                        (rows, cols, self.c.count(1, 31)))
            ctx.grids.append(("g0", rows, cols))
        if self.has("F2"):
            for k in range(self.c.count(1, 3)):
                decl.append("f%d := %s;" % (k, flit(self.c.pick([0.5, 1.25, 2.0, 3.75, 0.001, 100.0]))))
                ctx.floats.append("f%d" % k)
            if self.c.chance(50):
                decl.append("fa0 := Float->New[8];")
                ctx.farrays.append(("fa0", 8))
        if self.has("F4"):
            for k in range(self.c.count(1, 2)):
                decl.append('s%d := "%s";' % (k, self.c.pick(["ab", "xyz", "", "0123456789", "q"])))
                ctx.strings.append("s%d" % k)
        if self.has("F5"):
            decl.append("fr0 : (Int) ~ Int := %s(Int) ~ Int;" % self.c.pick(self.gfuncs))
            ctx.funcref = "fr0"
            self.used.add("%s:Apply" % MAIN_CLASS)
        if self.has("F3") and self.c.chance(60):
            decl.append("sh0 := FzShape->New[4];")
            for k in range(4):
                decl.append("sh0[%d] := %s->New(%d, %d);" % (k, self.c.pick(self.shape_subs),
                                                            self.c.count(0, 50), self.c.count(1, 20)))
            ctx.shapes = "sh0"
        if self.has("F6"):
            decl.append("nl0 : FzNode := Nil; np0 : FzNode := Nil; nq0 : FzNode := Nil;")
            decl.append("oa0 := FzNode->New[8]; ta0 := Int->New[1];")
            decl.append("t0 : FzTree := Nil;")
            ctx.lists = True
            ctx.trees = True
            ctx.objarray = True

        body = self.block(ctx, self.c.count(1, 8), 3, allow_calls=True)
        ret = self.wrap(self.int_expr(ctx, MAX_DEPTH, MAX_LEAVES))
        lines += ["    " + d for d in decl]
        lines += ["  " + l for l in body]
        lines.append("    return %s;" % ret.t)
        lines.append("  }")
        lines.append("")
        f = Func("F%d" % i, nparams, max(1, ctx.cost))
        self.top.append(f)
        self.methods.append("%s:F%d" % (MAIN_CLASS, i))
        return lines

    # -- statements -------------------------------------------------------
    def block(self, ctx, nstmts, depth, allow_calls):
        """Lines (indented two spaces per level below the caller) of a block."""
        lines = []
        for _ in range(nstmts):
            if ctx.cost * ctx.mult > COST_BUDGET * 4:
                break
            lines += self.statement(ctx, depth, allow_calls)
        return ["  " + l for l in lines]

    def statement(self, ctx, depth, allow_calls):
        c = self.c
        ctx.cost += 1 * ctx.mult
        nested = depth > 0
        loops_ok = nested and ctx.free_counters and ctx.mult < 400
        kinds = [
            (6, "assign"),
            (3 if ctx.assignable else 0, "compound"),
            (3 if ctx.arrays else 0, "astore"),
            (2 if ctx.grids else 0, "gstore"),
            (4 if nested else 0, "if"),
            (3 if loops_ok else 0, "for"),
            (2 if loops_ok else 0, "while"),
            (2 if loops_ok else 0, "do"),
            (2 if ctx.loop_depth > 0 else 0, "break"),
            (2 if ctx.loop_depth > 0 else 0, "continue"),
            (1 if not ctx.in_lambda and ctx.params != ["x"] else 0, "return"),
            (3 if self.has("F2") and ctx.floats else 0, "float"),
            (3 if self.has("F4") and nested else 0, "select"),
            (4 if self.has("F4") and ctx.strings else 0, "string"),
            (3 if self.has("F5") and ctx.funcref and allow_calls else 0, "funcref"),
            (3 if self.has("F5") and allow_calls and ctx.closure_n < 4 else 0, "closure"),
            (4 if self.has("F6") and ctx.lists and allow_calls else 0, "graph"),
            (2 if self.has("F3") and allow_calls and ctx.callees else 0, "widecall"),
        ]
        if not ctx.assignable:
            return []
        kind = c.weighted(kinds)
        return getattr(self, "st_" + kind)(ctx, depth, allow_calls)

    def target(self, ctx):
        return self.c.pick(ctx.assignable)

    def st_assign(self, ctx, depth, allow_calls):
        t = self.target(ctx)
        e = self.int_expr(ctx, MAX_DEPTH, MAX_LEAVES, allow_calls)
        return ["%s := %s;" % (t, self.wrap(e).t)]

    def st_compound(self, ctx, depth, allow_calls):
        t = self.target(ctx)
        op = self.c.pick(["+=", "-=", "*="])
        e = self.wrap(self.int_expr(ctx, 2, 6, allow_calls))
        m = self.c.pick(["%s %% 1048573", "%s and 1048575", "%s %% 65521"])
        return ["%s %s %s; %s := %s;" % (t, op, e.t, t, m % t)]

    def index(self, ctx, size, allow_calls, leaves=3):
        e = self.int_expr(ctx, 1, leaves, allow_calls)
        return "(%s and %d)" % (e.t, size - 1)

    def st_astore(self, ctx, depth, allow_calls):
        name, size = self.c.pick(ctx.arrays)
        e = self.wrap(self.int_expr(ctx, 2, 5, allow_calls))
        return ["%s[%s] := %s;" % (name, self.index(ctx, size, allow_calls), e.t)]

    def st_gstore(self, ctx, depth, allow_calls):
        name, rows, cols = self.c.pick(ctx.grids)
        e = self.wrap(self.int_expr(ctx, 2, 4, allow_calls))
        return ["%s[%s, %s] := %s;" % (name, self.index(ctx, rows, allow_calls, 2),
                                      self.index(ctx, cols, allow_calls, 2), e.t)]

    def cond(self, ctx, allow_calls, leaves=6):
        c = self.c
        if self.has("F2") and ctx.floats and c.chance(20):
            a = self.float_expr(ctx, 1, leaves // 2)
            b = self.float_expr(ctx, 1, leaves // 2)
        else:
            a = self.int_expr(ctx, 1, leaves // 2, allow_calls)
            b = self.int_expr(ctx, 1, leaves // 2, allow_calls)
        op = c.pick(["<", "=", "<>", ">", "<=", ">="])
        text = "(%s %s %s)" % (a.t, op, b.t)
        if c.chance(20):
            x = self.int_expr(ctx, 0, 1)
            text = "(%s %s (%s %s %s))" % (text, c.pick(["&", "|"]), x.t, c.pick(["<", ">", "<>"]),
                                           lit(c.count(-5, 50)))
        return text

    def st_if(self, ctx, depth, allow_calls):
        c = self.c
        lines = ["if(%s) {" % self.cond(ctx, allow_calls)]
        lines += self.block(ctx, c.count(1, 3), depth - 1, allow_calls)
        if c.chance(30):
            lines.append("} else if(%s) {" % self.cond(ctx, allow_calls))
            lines += self.block(ctx, c.count(1, 2), depth - 1, allow_calls)
        if c.chance(50):
            lines.append("} else {")
            lines += self.block(ctx, c.count(1, 2), depth - 1, allow_calls)
        lines.append("};")
        return lines

    def loop(self, ctx, depth, allow_calls, iters, header, footer, counter, first=None):
        ctx.free_counters.pop(0)
        ctx.counters.append(counter)
        ctx.ints.append(counter)
        ctx.loop_depth += 1
        saved = ctx.mult
        ctx.mult *= max(1, iters)
        lines = [header]
        if first:
            lines.append("  " + first)
        lines += self.block(ctx, self.c.count(1, 4), depth - 1, allow_calls)
        lines.append(footer)
        ctx.mult = saved
        ctx.loop_depth -= 1
        ctx.ints.remove(counter)
        ctx.counters.pop()
        return lines

    def st_for(self, ctx, depth, allow_calls):
        c = self.c
        iname, wname = ctx.free_counters[0]
        shape = c.draw(4)
        if shape == 1 and ctx.arrays:
            name, size = c.pick(ctx.arrays)
            step = c.pick([1, 2, 3])
            header = "for(%s := 0; %s < %s->Size(); %s += %d;) {" % (iname, iname, name, iname, step)
            iters = size // step + 1
        elif shape == 2:
            n = c.count(1, 12)
            header = "for(%s := %d; %s > 0; %s -= 1;) {" % (iname, n, iname, iname)
            iters = n
        else:
            start = c.count(-3, 2)
            n = c.count(1, 12)
            header = "for(%s := %s; %s < %d; %s += 1;) {" % (iname, lit(start), iname, n, iname)
            iters = n - start
        lines = self.loop(ctx, depth, allow_calls, iters, header, "};", iname)
        ctx.free_counters.insert(0, (iname, wname))
        return lines

    def st_while(self, ctx, depth, allow_calls):
        iname, wname = ctx.free_counters[0]
        n = self.c.count(1, 12)
        lines = ["%s := 0;" % wname]
        lines += self.loop(ctx, depth, allow_calls, n, "while(%s < %d) {" % (wname, n), "};", wname,
                           first="%s += 1;" % wname)
        ctx.free_counters.insert(0, (iname, wname))
        return lines

    def st_do(self, ctx, depth, allow_calls):
        iname, wname = ctx.free_counters[0]
        n = self.c.count(1, 12)
        lines = ["%s := 0;" % wname]
        lines += self.loop(ctx, depth, allow_calls, n, "do {", "} while(%s < %d);" % (wname, n), wname,
                           first="%s += 1;" % wname)
        ctx.free_counters.insert(0, (iname, wname))
        return lines

    def st_break(self, ctx, depth, allow_calls):
        return ["if(%s) { break; };" % self.cond(ctx, allow_calls, 4)]

    def st_continue(self, ctx, depth, allow_calls):
        return ["if(%s) { continue; };" % self.cond(ctx, allow_calls, 4)]

    def st_return(self, ctx, depth, allow_calls):
        e = self.wrap(self.int_expr(ctx, 2, 5, allow_calls))
        return ["if(%s) { return %s; };" % (self.cond(ctx, allow_calls, 4), e.t)]

    def st_float(self, ctx, depth, allow_calls):
        c = self.c
        kind = c.draw(4)
        if kind == 1:
            t = self.target(ctx)
            fe = self.fwrap(self.float_expr(ctx, 2, 5))
            return ["%s := %s;" % (t, self.wrap(E("(%s * 1000.0)->As(Int)" % fe.t, int(fe.b * 1000) + 1, fe.n)).t)]
        if kind == 2 and ctx.farrays:
            name, size = c.pick(ctx.farrays)
            fe = self.fwrap(self.float_expr(ctx, 2, 5))
            return ["%s[%s] := %s;" % (name, self.index(ctx, size, allow_calls, 2), fe.t)]
        t = c.pick(ctx.floats)
        fe = self.fwrap(self.float_expr(ctx, MAX_DEPTH, MAX_LEAVES))
        return ["%s := %s;" % (t, fe.t)]

    def st_select(self, ctx, depth, allow_calls):
        c = self.c
        shape = c.draw(3)
        if shape == 0:
            n = c.count(2, 8)
            labels = list(range(n))
            sel = "(%s and %d)" % (self.int_expr(ctx, 1, 4, allow_calls).t, next_pow2(n - 1) - 1)
        elif shape == 1:
            labels = sorted(set(c.count(-40, 40) for _ in range(c.count(2, 7))))
            sel = "(%s %% 41)" % self.int_expr(ctx, 1, 4, allow_calls).t
        else:
            labels = sorted(set(c.count(-9, 2) for _ in range(c.count(2, 6))))
            sel = "(%s %% 10)" % self.int_expr(ctx, 1, 4, allow_calls).t
        if c.chance(20):
            labels = labels[1:] + labels[:1]
        lines = ["select(%s) {" % sel]
        for lab in labels:
            lines.append("  label %d: {" % lab)
            lines += ["  " + l for l in self.block(ctx, c.count(1, 2), depth - 1, allow_calls)]
            lines.append("  }")
        if c.chance(60):
            lines.append("  other: {")
            lines += ["  " + l for l in self.block(ctx, 1, depth - 1, allow_calls)]
            lines.append("  }")
        lines.append("};")
        return lines

    def st_string(self, ctx, depth, allow_calls):
        c = self.c
        s = c.pick(ctx.strings)
        cap = "if(%s->Size() > 48) { %s := %s->SubString(0, 16); };" % (s, s, s)
        v = c.pick(ctx.ints)
        kind = c.draw(8)
        if kind == 1:
            return ['%s += "%s{$%s}%s";' % (s, c.pick(["<", "", "ab"]), v, c.pick([">", "", "-"])), cap]
        if kind == 2:
            return ["%s := %s + %s;" % (s, c.pick(ctx.strings), v), cap]
        if kind == 3:
            return ["%s := %s->ToString();" % (s, v)]
        if kind == 4:
            return ["if(%s->Size() > 8) { %s := %s->SubString(%s, 4); };" %
                    (s, s, s, self.index(ctx, 4, allow_calls, 2))]
        if kind == 5:
            t = self.target(ctx)
            return ["if(%s->Size() > 8) { %s := (%s * 31 + %s->Get(%s)->As(Int)) %% 1048573; };" %
                    (s, t, t, s, self.index(ctx, 8, allow_calls, 2))]
        if kind == 6 and ctx.each_n < 3 and ctx.mult < 200:
            t = self.target(ctx)
            ch = "c%d" % ctx.each_n
            ctx.each_n += 1
            ctx.cost += 64 * ctx.mult
            return ["each(%s : %s) { %s := (%s * 31 + %s->Get(%s)->As(Int)) %% 1000003; };" % (ch, s, t, t, s, ch)]
        if kind == 7:
            t = self.target(ctx)
            return ["%s := (%s + (%s->ToInt() %% 1048573)) %% 1048573;" % (t, t, s)]
        return ['%s := %s + "%s";' % (s, s, c.pick(["x", "yz", "0", "-1"])), cap]

    def st_funcref(self, ctx, depth, allow_calls):
        c = self.c
        g = c.pick(self.gfuncs)
        self.used.add("%s:%s" % (MAIN_CLASS, g))
        if c.chance(50):
            return ["%s := %s(Int) ~ Int;" % (ctx.funcref, g)]
        t = self.target(ctx)
        e = self.wrap(self.int_expr(ctx, 1, 3))
        ctx.cost += 4 * ctx.mult
        return ["%s := (%s + Apply(%s(Int) ~ Int, %s)) %% 1048573;" % (t, t, g, e.t)]

    def st_closure(self, ctx, depth, allow_calls):
        c = self.c
        k = ctx.closure_n
        ctx.closure_n += 1
        saved = ctx.in_lambda
        ctx.in_lambda = True
        # captures: Int locals, parameters and loop counters only -- no calls,
        # arrays or fields inside a lambda body
        body = self.wrap(self.int_expr(ctx, 2, 4, allow_calls=False, simple=True))
        ctx.in_lambda = saved
        t = self.target(ctx)
        if self.basic_lambdas and c.chance(40):
            return ["b%d := \\() ~ Int : () => %s;" % (k, body.t),
                    "%s := (%s + b%d()) %% 1048573;" % (t, t, k)]
        if c.chance(50):
            decl = "k%d := FuncRef->New(\\() ~ IntRef : () => IntRef->New(%s))<IntRef>;" % (k, body.t)
        else:
            decl = "k%d : FuncRef<IntRef> := \\() => IntRef->New(%s);" % (k, body.t)
        return [decl, "kr%d := k%d();" % (k, k), "%s := (%s + kr%d->Get()) %% 1048573;" % (t, t, k)]

    def st_graph(self, ctx, depth, allow_calls):
        c = self.c
        kind = c.draw(7)
        t = self.target(ctx)
        self.used.update(["FzNode:GetVal", "FzNode:GetNext"])
        if kind == 1 and ctx.free_counters:
            iname = ctx.free_counters[0][0]
            n = c.count(1, 64)
            ctx.cost += n * 2 * ctx.mult
            e = self.wrap(self.int_expr(ctx, 1, 3))
            return ["nl0 := Nil;",
                    "for(%s := 0; %s < %d; %s += 1;) { nl0 := FzNode->New((%s + %s) %% 1048573, nl0); };" %
                    (iname, iname, n, iname, e.t, iname)]
        if kind == 2:
            ctx.cost += 64 * ctx.mult
            return ["np0 := nl0;",
                    "while(np0 <> Nil) { %s := (%s * 7 + np0->GetVal()) %% 1048573; np0 := np0->GetNext(); };" % (t, t)]
        if kind == 3:
            self.used.add("FzNode:SetNext")
            ctx.cost += 64 * ctx.mult
            return ["nq0 := Nil;",
                    "while(nl0 <> Nil) { np0 := nl0->GetNext(); nl0->SetNext(nq0); nq0 := nl0; nl0 := np0; };",
                    "nl0 := nq0;"]
        if kind == 4:
            self.used.update(["FzTree:Build", "FzTree:Check"])
            ctx.cost += 256 * ctx.mult
            d = self.index(ctx, 8, False, 2)
            e = self.wrap(self.int_expr(ctx, 1, 2)).t
            return ["t0 := FzTree->Build(%s, %s);" % (d, e),
                    "if(t0 <> Nil) { %s := (%s + t0->Check()) %% 1048573; };" % (t, t)]
        if kind == 5:
            e = self.wrap(self.int_expr(ctx, 1, 3)).t
            return ["oa0[%s] := FzNode->New(%s, nl0);" % (self.index(ctx, 8, False, 2), e)]
        if kind == 6:
            e = self.wrap(self.int_expr(ctx, 1, 2)).t
            return ["ta0 := Int->New[%s + 1]; ta0[0] := %s;" % (self.index(ctx, 64, False, 2), e),
                    "%s := (%s + ta0->Size() + ta0[0]) %% 1048573;" % (t, t)]
        self.used.add("FzNode:SetVal")
        return ["np0 := oa0[%s];" % self.index(ctx, 8, False, 2),
                "if(np0 <> Nil) { %s := (%s + np0->GetVal()) %% 1048573; np0->SetVal(%s); };" % (t, t, t)]

    def st_widecall(self, ctx, depth, allow_calls):
        f = self.callee(ctx)
        if f is None:
            return self.st_assign(ctx, depth, allow_calls)
        args = [self.leaf_simple(ctx).t for _ in range(f.nparams)]
        if f.name.startswith("R"):
            args[0] = "(%s and 7)" % args[0]
        t = self.target(ctx)
        return ["%s := (%s + %s(%s)) %% 1048573;" % (t, t, f.name, ", ".join(args))]

    def callee(self, ctx):
        room = COST_BUDGET - ctx.cost
        ok = [f for f in ctx.callees if f.cost * ctx.mult * 2 <= room]
        if not ok:
            return None
        f = self.c.pick(ok)
        ctx.cost += f.cost * ctx.mult
        if f.name.startswith("R"):
            self.used.add("%s:%s" % (MAIN_CLASS, f.name))
        return f

    # -- expressions ------------------------------------------------------
    def wrap(self, e):
        if e.b <= VB:
            return e
        form = self.c.draw(3)
        if form == 1:
            return E("(%s and 1048575)" % e.t, VB, e.n)
        if form == 2:
            return E("(%s %% 1000003)" % e.t, VB, e.n)
        return E("(%s %% 1048573)" % e.t, VB, e.n)

    def leaf_simple(self, ctx):
        c = self.c
        if ctx.ints and c.chance(70):
            return E(c.pick(ctx.ints), VB)
        return E(lit(c.count(-20, 200)), 200)

    def int_leaf(self, ctx, leaves, allow_calls, simple):
        c = self.c
        if simple:
            return self.leaf_simple(ctx)
        k = c.weighted([
            (5, "var"),
            (4, "lit"),
            (1, "big"),
            (3 if ctx.arrays and leaves >= 2 else 0, "aread"),
            (1 if ctx.arrays else 0, "asize"),
            (2 if ctx.grids and leaves >= 3 else 0, "gread"),
            (2 if ctx.floats and leaves >= 2 else 0, "fdigest"),
            (2 if ctx.strings else 0, "ssize"),
            (3 if allow_calls and ctx.callees and leaves >= 2 else 0, "call"),
            (2 if allow_calls and ctx.shapes and leaves >= 3 else 0, "virtual"),
            (2 if allow_calls and ctx.funcref and leaves >= 2 else 0, "fref"),
        ])
        if k == "var":
            return E(c.pick(ctx.ints), VB)
        if k == "big":
            v = c.pick([1048575, 65536, 2147483647, 4294967311, 1 << 40, 3000000000, 1000000007])
            return E(lit(v), v)
        if k == "aread":
            name, size = c.pick(ctx.arrays)
            return E("%s[%s]" % (name, self.index(ctx, size, allow_calls, min(3, leaves - 1))), VB, 2)
        if k == "asize":
            name, size = c.pick(ctx.arrays)
            return E("%s->Size()" % name, size)
        if k == "gread":
            name, rows, cols = c.pick(ctx.grids)
            return E("%s[%s, %s]" % (name, self.index(ctx, rows, allow_calls, 1),
                                     self.index(ctx, cols, allow_calls, 1)), VB, 3)
        if k == "fdigest":
            fe = self.fwrap(self.float_expr(ctx, 1, leaves - 1))
            return E("(%s * 1000.0)->As(Int)" % fe.t, int(VF * 1000) + 1, fe.n + 1)
        if k == "ssize":
            return E("%s->Size()" % c.pick(ctx.strings), 256)
        if k == "call":
            f = self.callee(ctx)
            if f is not None and f.nparams <= leaves:
                args = [self.wrap(self.int_expr(ctx, 0, 1, False)).t for _ in range(f.nparams)]
                if f.name.startswith("R"):
                    # recursion depth: 0-7, whatever the argument
                    args[0] = "(%s and 7)" % args[0]
                return E("%s(%s)" % (f.name, ", ".join(args)), VB, f.nparams)
        if k == "virtual":
            m = c.pick(["V", "M", "Twice"])
            ctx.cost += 4 * ctx.mult
            self.used.add("FzShape:%s" % m if m != "V" else "FzShape:Twice")
            if m == "V":
                self.used.update("%s:V" % s for s in self.shape_subs)
            if m == "Twice":
                self.used.update("%s:V" % s for s in self.shape_subs)
            arg = self.wrap(self.int_expr(ctx, 0, 1, False)).t
            return E("%s[%s]->%s(%s)" % (ctx.shapes, self.index(ctx, 4, False, 1), m, arg), VB, 3)
        if k == "fref":
            arg = self.wrap(self.int_expr(ctx, 0, 1, False)).t
            ctx.cost += 2 * ctx.mult
            return E("%s(%s)" % (ctx.funcref, arg), VB, 2)
        return E(lit(c.count(-9, 99)), 99)

    def int_expr(self, ctx, depth, leaves, allow_calls=True, simple=False):
        c = self.c
        if depth <= 0 or leaves < 2 or not c.chance(60):
            return self.int_leaf(ctx, leaves, allow_calls, simple)
        op = c.weighted([(5, "+"), (4, "-"), (4, "*"), (2, "%"), (2, "/"), (2, "and"), (1, "or"),
                         (1, "xor"), (1, "<<"), (1, ">>"), (1, "neg")])
        if op == "neg":
            a = self.int_expr(ctx, depth - 1, leaves - 1, allow_calls, simple)
            return E("(0 - %s)" % a.t, a.b, a.n)
        left_budget = max(1, leaves // 2 if c.chance(50) else leaves - 1)
        a = self.int_expr(ctx, depth - 1, left_budget, allow_calls, simple)
        rest = leaves - a.n
        if op in ("%", "/"):
            if rest >= 2 and c.chance(40):
                d = self.int_expr(ctx, 0, 1, allow_calls, simple)
                mask = c.pick([255, 15, 1, 1023])
                div = E("((%s and %d) + 1)" % (d.t, mask), mask + 1, d.n)
            else:
                v = c.pick([2, 3, 7, 10, 16, -7, -4, 1024, 1000003, 6, -1, 4294967311, 1])
                div = E(lit(v), abs(v))
            a = self.cap(a)
            bound = a.b if op == "/" else min(a.b, div.b)
            return E("(%s %s %s)" % (a.t, op, div.t), bound, a.n + div.n)
        if op in ("<<", ">>"):
            a = self.cap(a)
            if rest >= 2 and c.chance(40):
                k = self.int_expr(ctx, 0, 1, allow_calls, simple)
                mask = 15 if op == "<<" else 63
                cnt = E("(%s and %d)" % (k.t, mask), mask, k.n)
            else:
                cnt = E(str(c.count(0, 20 if op == "<<" else 63)), 0, 1)
                cnt.b = int(cnt.t)
            if op == "<<":
                return E("(%s << %s)" % (a.t, cnt.t), a.b << cnt.b, a.n + cnt.n)
            return E("(%s >> %s)" % (a.t, cnt.t), a.b, a.n + cnt.n)
        b = self.int_expr(ctx, depth - 1, max(1, rest), allow_calls, simple)
        if op in ("and", "or", "xor"):
            if op == "and" and c.chance(30):
                mask = c.pick([1, 7, 255, 65535, 1048575])
                return E("(%s and %d)" % (self.cap(a).t, mask), mask, a.n + 1)
            a, b = self.cap(a), self.cap(b)
            return E("(%s %s %s)" % (a.t, op, b.t), next_pow2(max(a.b, b.b)), a.n + b.n)
        if op == "*":
            if a.b * b.b > LIMIT:
                a, b = self.cap(a, VB), self.cap(b, VB)
            return E("(%s * %s)" % (a.t, b.t), a.b * b.b, a.n + b.n)
        if a.b + b.b > LIMIT:
            a, b = self.cap(a, VB), self.cap(b, VB)
        return E("(%s %s %s)" % (a.t, op, b.t), a.b + b.b, a.n + b.n)

    def cap(self, e, bound=VB):
        """Wrap an operand whose bound is too large for the operator ahead."""
        if e.b <= bound:
            return e
        return E("(%s %% 1048573)" % e.t, VB, e.n)

    def float_leaf(self, ctx, leaves):
        c = self.c
        k = c.weighted([(4, "var"), (3, "lit"), (2, "int"), (2 if ctx.farrays and leaves >= 2 else 0, "aread")])
        if k == "var" and ctx.floats:
            return E(c.pick(ctx.floats), VF)
        if k == "int":
            return E("%s->As(Float)" % c.pick(ctx.ints), float(VB))
        if k == "aread":
            name, size = c.pick(ctx.farrays)
            return E("%s[%s]" % (name, self.index(ctx, size, False, 1)), VF, 2)
        v = c.pick([0.5, 1.25, 2.0, -0.75, 3.0, 0.001, 1000.0, -2.5, 0.1])
        return E(flit(v), abs(v))

    def float_expr(self, ctx, depth, leaves):
        c = self.c
        if depth <= 0 or leaves < 2 or not c.chance(60):
            return self.float_leaf(ctx, leaves)
        op = c.weighted([(4, "+"), (3, "-"), (3, "*"), (2, "/"), (1, "sqrt"), (1, "sin"), (1, "floor"), (1, "abs")])
        if op in ("sqrt", "sin", "floor", "abs"):
            a = self.float_expr(ctx, depth - 1, leaves - 1)
            if op == "sqrt":
                return E("Float->Sqrt(%s->Abs())" % a.t, a.b ** 0.5 + 1.0, a.n)
            if op == "sin":
                return E("Float->Sin(%s)" % a.t, 1.0, a.n)
            if op == "floor":
                return E("Float->Floor(%s)" % a.t, a.b + 1.0, a.n)
            return E("%s->Abs()" % a.t, a.b, a.n)
        a = self.float_expr(ctx, depth - 1, max(1, leaves // 2))
        b = self.float_expr(ctx, depth - 1, max(1, leaves - a.n))
        if op == "/":
            return E("(%s / (%s->Abs() + 1.0))" % (a.t, b.t), a.b, a.n + b.n)
        if op == "*":
            if a.b * b.b > FLIMIT:
                a, b = self.fwrap(a, force=True), self.fwrap(b, force=True)
            return E("(%s * %s)" % (a.t, b.t), a.b * b.b, a.n + b.n)
        return E("(%s %s %s)" % (a.t, op, b.t), a.b + b.b, a.n + b.n)

    def fwrap(self, e, force=False):
        if e.b <= VF and not force:
            return e
        if e.b <= VF:
            return e
        return E("((%s)->As(Int) %% 1000003)->As(Float)" % e.t, VF, e.n)


def generate(seed=None, choices=None, features=None, basic_lambdas=False):
    """Build one program. `seed` draws fresh choices; `choices` replays a
    recorded list. `features` (an iterable of 'F2'..'F6') overrides the swarm
    draw while still consuming it, so a replay stays aligned."""
    ch = Choices(seed=seed, replay=choices)
    g = Generator(ch, features=set(features) if features is not None else None, seed=seed,
                  basic_lambdas=basic_lambdas)
    text, methods = g.program()
    return Program(text, ch.record, g.features, methods, seed)


if __name__ == "__main__":
    import argparse
    ap = argparse.ArgumentParser(description="print one generated Objeck program")
    ap.add_argument("--seed", type=int, default=1)
    ap.add_argument("--features", default=None, help="comma list, e.g. F2,F4 (F1 is always on)")
    ap.add_argument("--basic-lambdas", action="store_true", help="also emit the 9.4 JIT-crashing lambda form")
    args = ap.parse_args()
    feats = args.features.split(",") if args.features else None
    print(generate(seed=args.seed, features=feats, basic_lambdas=args.basic_lambdas).text)
