#!/usr/bin/env python3
"""Randomized GC-root coverage program generator (docs/GC_ROOTS_DESIGN.md section 9).

    python tools/gc_roots/gen_roots.py --seed 42 > prog.obs
    python tools/gc_roots/gen_roots.py --seed 42 --info      # features of the program, as JSON

A program is a pure function of its seed. It holds references (String, Int[],
Box[], String[], class instances with a virtual method and a link, FuncRef
closures, typed function references) beside Int, Float and address-valued Int
locals, and keeps them live across everything the JIT's frame-layout constants
and RefMaps have to get right:

  * locals across for/while/each loops, with break/continue and early return
  * pending expression temporaries: a value computed BEFORE a call that
    allocates (call arguments, receivers, array bases, string interpolation,
    binary operands, ternary and and/or joins)
  * method calls, recursion, and small callees the optimizer (-opt s3) inlines
  * if / select / loops with early exit
  * ?-> regions: runtime faults and Runtime->Raise recovered several frames up
  * instance methods whose references are fields (self is a root, write barrier)
  * threads: workers run generated scenarios while another thread forces
    collections

Every kept value is folded into a digest that is printed, so a reference the
collector lost or failed to relocate changes the output. The programs never
print an address, a time or anything else that depends on the configuration;
`runner.py` runs each program under the interpreter, the JIT and GC stress
settings and requires identical output.

Draw order is stable: the same seed always gives the same program.
"""

import argparse
import json
import random
import sys

REPS = 14                 # calls of each scenario from Main: crosses the default JIT threshold (10)
MAX_DEPTH = 2             # loop / if nesting
U = "RcU->"               # the helper class's prefix, as written in generated code

REF_KINDS = "SABTOFG"
ALL_KINDS = "SABTOFGID"
FIELD_KINDS = "SABTOID"   # a lambda body cannot read a field, so no F or G fields

# parameter and field type of each kind
TYPES = {"S": "String", "A": "Int[]", "B": "Box[]", "T": "String[]", "O": "Box",
         "F": "FuncRef<IntRef>", "G": "() ~ IntRef", "I": "Int", "D": "Float"}


class Var:
    __slots__ = ("name", "kind", "field", "ro")

    def __init__(self, name, kind, field=False, ro=False):
        self.name = name
        self.kind = kind
        self.field = field
        self.ro = ro   # loop counters: read, never written


def ind(lines, n=1):
    pad = "  " * n
    return [pad + l for l in lines]


class Gen:
    def __init__(self, seed, scale=1.0, addr="off"):
        self.seed = seed
        # address-valued Ints are reported by the roots audit on purpose, and a rewrite of one in
        # an interpreted frame that sits under compiled code is a documented limit
        # (docs/GC_ROOTS_DESIGN.md section 12), so they are opt-in: off by default, "auto" =
        # seeds divisible by 3
        self.addr = (seed % 3 == 0) if addr == "auto" else (addr == "on")
        self.rng = random.Random(seed)
        self.scale = scale
        self.uid = 0
        self.helpers = {}        # signature -> (name, text)
        self.features = set()
        self.churn_unit = self.rng.choice([6, 10, 16, 24])
        self.big_every = self.rng.choice([0, 0, 5, 9])
        self.uses_addr = False
        self.sc_count = 0

    # ---- small random helpers ------------------------------------------------
    def u(self, p):
        self.uid += 1
        return "%s%d" % (p, self.uid)

    def chance(self, pct):
        return self.rng.randrange(100) < pct

    def pick(self, seq):
        return seq[self.rng.randrange(len(seq))]

    def weighted(self, pairs):
        total = sum(w for w, _ in pairs)
        x = self.rng.randrange(total)
        for w, v in pairs:
            if x < w:
                return v
            x -= w
        return pairs[-1][1]

    def feat(self, name):
        self.features.add(name)

    # ---- expressions -----------------------------------------------------------
    def churn(self):
        return "%sChurn(%d)" % (U, self.weighted([(5, 1), (3, 2), (1, 3)]))

    def ints(self, env):
        return [v for v in env if v.kind == "I"]

    def salt(self, env):
        """A small non-negative Int expression."""
        iv = self.ints(env)
        c = self.rng.randrange(1, 40)
        opts = [(4, "n"), (3, "(n + %d)" % c), (2, "(d %% %d)" % self.pick([997, 101, 31]))]
        if iv:
            x = self.pick(iv).name
            opts += [(4, "(%s %% 9973)" % x), (2, "((%s + n) %% 9973)" % x)]
        opts.append((1, "(%s %% 97)" % self.churn()))
        return self.weighted(opts)

    def dig(self, v):
        n = v.name
        k = v.kind
        if k == "S":
            return "%sDigS(%s)" % (U, n)
        if k == "A":
            return "%sDigA(%s)" % (U, n)
        if k == "B":
            return "%sDigB(%s)" % (U, n)
        if k == "T":
            return "%sDigT(%s)" % (U, n)
        if k == "O":
            return "%sDigO(%s)" % (U, n)
        if k in "FG":
            return "%s()->Get()" % n
        if k == "I":
            return n
        return "(%s * 4.0)->As(Int)" % n   # D

    def fresh(self, kind, s):
        """An expression that allocates a new value of this kind."""
        if kind == "S":
            return self.pick(['"t{$(%s)}"' % s, 'String->New("u{$(%s)}")' % s])
        if kind == "A":
            return "%sMkarr(%s)" % (U, s)
        if kind == "B":
            return "%sMkb(%s)" % (U, s)
        if kind == "T":
            return "%sMkt(%s)" % (U, s)
        if kind == "O":
            return self.pick(["%sMk(%s)" % (U, s), "%sMkBa(%s)" % (U, s),
                              "%sChain(%s, %d)" % (U, s, self.rng.randrange(2, 5))])
        if kind == "F":
            return "%sMkf(%s)" % (U, s)
        if kind == "I":
            return s
        if kind == "D":
            return "1.5 + (%s %% 50)->As(Float) * 0.25" % s
        raise ValueError(kind)

    # ---- declarations ------------------------------------------------------------
    def decl(self, env, kind, name=None, field=False, s=None):
        """Lines declaring a new variable of this kind; env is extended in place."""
        name = name or self.u("v")
        s = s or self.salt(env)
        v = Var(name, kind, field)
        L = []
        if kind == "S":
            L.append("%s := %s;" % (name, self.fresh("S", s)))
            if self.chance(25) and not field:
                L.append("%s := %s + \"-{$(%s)}\";" % (name, name, s))   # concatenation
        elif kind == "A":
            ln = self.rng.randrange(2, 10)
            j = self.u("j")
            L.append("%s := Int->New[%d];" % (name, ln))
            L.append("each(%s : %d) { %s[%s] := (%s + %s) %% 1000; };" % (j, ln, name, j, s, j))
        elif kind == "B":
            L.append("%s := Box->New[3];" % name)
            L.append("%s[0] := %s;" % (name, self.fresh("O", s)))
            L.append("%s[1] := %s;" % (name, self.fresh("O", s)))
        elif kind == "T":
            L.append("%s := String->New[3];" % name)
            L.append("%s[0] := \"x{$(%s)}\";" % (name, s))
            L.append("%s[2] := \"y\";" % name)
        elif kind == "O":
            L.append("%s := %s;" % (name, self.fresh("O", s)))
        elif kind in "FG":
            cap = [x for x in env if x.kind == "O" and not x.field]
            if not cap or self.chance(40):
                cn = self.u("c")
                L.append("%s := %s;" % (cn, self.fresh("O", s)))
                cap_v = Var(cn, "O")
                env.append(cap_v)
            else:
                cap_v = self.pick(cap)
            extra = self.pick(["", " + 1", " + %d" % self.rng.randrange(2, 9)])
            loc_ints = [x for x in self.ints(env) if not x.field]
            if loc_ints and self.chance(40):
                extra = " + %s" % self.pick(loc_ints).name
            body = "IntRef->New(%s->Get()%s)" % (cap_v.name, extra)
            if kind == "F":
                L.append("%s := FuncRef->New(\\() ~ IntRef : () => %s)<IntRef>;" % (name, body))
                self.feat("closure")
            else:
                L.append("%s := \\() ~ IntRef : () => %s;" % (name, body))
                self.feat("funcref")
        elif kind == "I":
            L.append("%s := %s;" % (name, s))
        elif kind == "D":
            L.append("%s := %s;" % (name, self.fresh("D", s)))
        env.append(v)
        return L

    # ---- helper functions for pending calls ----------------------------------------
    def helper(self, kinds):
        """A function over parameters of these kinds: digests them, allocates, digests again."""
        sig = tuple(kinds)
        if sig in self.helpers:
            return self.helpers[sig][0]
        name = "P%d" % (len(self.helpers) + 1)
        params = ["p%d : %s" % (i, TYPES[k]) for i, k in enumerate(kinds)]
        ps = [Var("p%d" % i, k) for i, k in enumerate(kinds)]
        small = self.chance(45)   # small ones are inlined by -opt s3
        body = ["r := 0;"]
        for p in ps:
            body.append("r := %sFold(r, %s);" % (U, self.dig(p)))
        body.append("r := %sFold(r, %s);" % (U, self.churn()))
        if not small:
            for p in ps:
                if p.kind == "O" and self.chance(50):
                    body.append("%s->Set(r %% 100);" % p.name)
                elif p.kind == "A" and self.chance(50):
                    body.append("%s[0] := r %% 100;" % p.name)
                body.append("r := %sFold(r, %s);" % (U, self.dig(p)))
            body.append("t := Ba->New(r);")
            body.append("r := %sFold(r, t->Get() + %s);" % (U, self.churn()))
        body.append("return r;")
        text = ["  function : %s(%s) ~ Int {" % (name, ", ".join(params))] + ind(body, 2) + ["  }", ""]
        self.helpers[sig] = (name, text)
        if small:
            self.feat("inline_candidate")
        return name

    # ---- statements ----------------------------------------------------------------
    def stmt_churn(self, env, ctx):
        return ["d := %sFold(d, %s);" % (U, self.churn())]

    def stmt_decl(self, env, ctx):
        kinds = FIELD_KINDS if False else ALL_KINDS
        k = self.weighted([(3, "S"), (2, "A"), (2, "B"), (2, "T"), (4, "O"), (2, "F"), (2, "G"), (3, "I"), (2, "D")])
        return self.decl(env, k)

    def writable(self, env, kinds=ALL_KINDS):
        return [v for v in env if v.kind in kinds and not v.ro]

    def stmt_mutate(self, env, ctx):
        vs = self.writable(env)
        if not vs:
            return self.stmt_churn(env, ctx)
        v = self.pick(vs)
        s = self.salt(env)
        n = v.name
        k = v.kind
        if k == "S":
            return [self.pick([
                "%s := \"{$(%s->Size())}x{$(%s)}\";" % (n, n, s),
                "%s := String->New(\"{$(%s)}k\");" % (n, s),
                "%s := \"w{$(%s)}\" + \"{$(%s)}\";" % (n, s, self.churn()),   # s pending across Churn
            ])]
        if k == "A":
            return ["%s[%s %% %s->Size()] := (%s + %s) %% 1000;" % (n, s, n, s, self.churn())]
        if k == "B":
            return ["%s[%s %% 3] := %s;" % (n, s, self.fresh("O", s))]
        if k == "T":
            return ["%s[%s %% 3] := \"z{$(%s)}\";" % (n, s, self.churn())]
        if k == "O":
            return [self.pick([
                "%s->Set(%s + %s);" % (n, self.churn(), s),            # receiver pending across Churn
                "%s := %s;" % (n, self.fresh("O", s)),
                "%s->SetLink(%s);" % (n, self.fresh("O", s)),          # old-to-young store
                "%s->SetLink(Ba->New(%s));" % (n, self.churn()),
            ])]
        if k in "FG":
            cap = [x for x in env if x.kind == "O" and not x.field]
            if not cap:
                return self.stmt_churn(env, ctx)
            c = self.pick(cap).name
            body = "IntRef->New(%s->Get() + %d)" % (c, self.rng.randrange(1, 9))
            if k == "F":
                return ["%s := FuncRef->New(\\() ~ IntRef : () => %s)<IntRef>;" % (n, body)]
            return ["%s := \\() ~ IntRef : () => %s;" % (n, body)]
        if k == "I":
            return [self.pick([
                "%s := (%s * 3 + %s) %% 100003;" % (n, n, s),
                "%s := (%s + %s) %% 100003;" % (n, n, self.churn()),
                "%s += %s %% 17;" % (n, s),
            ]).replace("+= %s %% 17;" % s, "+= %s %% 17;" % s)]
        # D
        return [self.pick([
            "%s := %s * 0.75 + 2.0;" % (n, n),
            "%s := %s * 0.5 + (%s %% 10)->As(Float);" % (n, n, self.churn()),
        ])]

    def stmt_pending(self, env, ctx):
        """A call whose earlier arguments are pending while a later one allocates."""
        nparams = self.rng.randrange(2, 6)
        kinds = [self.weighted([(3, "O"), (2, "S"), (2, "A"), (1, "B"), (1, "T"), (2, "F"), (1, "G"), (2, "I"), (1, "D")])
                 for _ in range(nparams)]
        if "I" not in kinds:
            kinds[self.rng.randrange(nparams)] = "I"
        name = self.helper(kinds)
        s = self.salt(env)
        args = []
        for k in kinds:
            have = [v for v in env if v.kind == k]
            if k == "G":
                if not have:
                    return self.stmt_churn(env, ctx)
                args.append(self.pick(have).name)
            elif k == "I":
                args.append(self.pick([self.churn(), self.churn(), s]))
            elif have and self.chance(65):
                args.append(self.pick(have).name)
            else:
                args.append(self.fresh(k, s))
        call = "%s%s(%s)" % (U, name, ", ".join(args))
        form = self.rng.randrange(4)
        iv = [v for v in env if v.kind == "I" and not v.ro]
        if form == 0 and iv:
            return ["%s := (%s) %% 100003;" % (self.pick(iv).name, call)]
        if form == 1:
            return ["d := %sFold(d, %s + %s);" % (U, call, self.churn())]
        return ["d := %sFold(d, %s);" % (U, call)]

    def stmt_expr_pending(self, env, ctx):
        """Pending temporaries that are not call arguments."""
        os_ = [v for v in env if v.kind == "O"]
        ss = [v for v in env if v.kind == "S"]
        arrs = [v for v in env if v.kind == "A"]
        form = self.rng.randrange(6)
        if form == 0 and os_:
            o = self.pick(os_).name
            return ["d := %sFold(d, %s->Mix(%s, %s));" % (U, o, self.churn(), self.salt(env))]
        if form == 1 and os_ and ss:
            return ["d := %sFold(d, %sDigO(%s) + %s + %sDigS(%s));" % (U, U, self.pick(os_).name, self.churn(), U, self.pick(ss).name)]
        if form == 2 and arrs:
            a = self.pick(arrs).name
            return ["%s[%s %% %s->Size()] := (%s + %s) %% 1000;" % (a, self.churn(), a, self.salt(env), self.churn())]
        if form == 3 and os_ and ss:
            return ["d := %sFold(d, \"{$(%sDigO(%s))}{$(%s)}{$(%sDigS(%s))}\"->Size());" % (
                U, U, self.pick(os_).name, self.churn(), U, self.pick(ss).name)]
        if form == 4 and os_:
            # a freshly allocated object pending as an argument, while the next argument allocates
            return ["d := %sFold(d, %sPair(Ba->New(%s), %s));" % (U, U, self.salt(env), self.churn())]
        if form == 5 and os_:
            o = self.pick(os_).name
            return ["d := %sFold(d, %s->Get() + %s * 2 + %s->Get());" % (U, o, self.churn(), o)]
        return self.stmt_churn(env, ctx)

    def stmt_address(self, env, ctx):
        """An address-valued Int pending across a collection (needs OBJECK_GC_TEST_HOOKS=1)."""
        os_ = [v for v in env if v.kind == "O"]
        if not os_ or not self.addr:
            return self.stmt_churn(env, ctx)
        self.uses_addr = True
        self.feat("address_int")
        o = self.pick(os_).name
        b = self.u("ad")
        return ["%s := Runtime->TestAddressOf(%s);" % (b, o),
                "d := %sFold(d, %sEq3(Runtime->TestAddressOf(%s), %s, %s));" % (U, U, o, self.churn(), b)]

    def stmt_call(self, env, ctx):
        os_ = [v for v in env if v.kind == "O"]
        ss = [v for v in env if v.kind == "S"]
        arrs = [v for v in env if v.kind == "A"]
        form = self.rng.randrange(4)
        if form == 0 and os_ and ss and arrs:
            self.feat("recursion")
            return ["d := %sFold(d, %sRec(%d, %s, %s, %s));" % (
                U, U, self.rng.randrange(1, 6), self.pick(os_).name, self.pick(ss).name, self.pick(arrs).name)]
        if form == 1 and os_:
            self.feat("inline_candidate")
            return ["d := %sFold(d, %sTiny(%s, %s));" % (U, U, self.pick(os_).name, self.churn())]
        if form == 2 and ss and arrs:
            self.feat("inline_candidate")
            return ["d := %sFold(d, %sTiny2(%s, %s, %s));" % (U, U, self.pick(ss).name, self.pick(arrs).name, self.salt(env))]
        if form == 3 and os_:
            return ["d := %sFold(d, %sTiny3(%s, %s, %s));" % (U, U, self.pick(os_).name, self.churn(), self.salt(env))]
        return self.stmt_pending(env, ctx)

    def stmt_use_closure(self, env, ctx):
        fs = [v for v in env if v.kind in "FG"]
        if not fs:
            return self.stmt_churn(env, ctx)
        f = self.pick(fs)
        # the closure value is pending while Churn allocates
        return ["d := %sFold(d, %s()->Get() + %s + %s()->Get());" % (U, f.name, self.churn(), f.name)]

    def stmt_ternary(self, env, ctx):
        """Conditional and and/or joins that carry references or run Churn on one side."""
        os_ = [v for v in env if v.kind == "O"]
        ss = [v for v in env if v.kind == "S"]
        cond = self.cond(env)
        form = self.rng.randrange(4)
        if form == 0 and os_:
            t = self.u("t")
            env.append(Var(t, "O"))
            return ["%s := (%s) ? %s : %s;" % (t, cond, self.pick(os_).name, self.fresh("O", self.salt(env)))]
        if form == 1 and ss:
            t = self.u("t")
            env.append(Var(t, "S"))
            return ["%s := (%s) ? %s : \"k{$(%s)}\";" % (t, cond, self.pick(ss).name, self.churn())]
        if form == 2:
            b = self.u("b")
            return ["%s := (%s) & (%s) | (%s %% 2 = 1);" % (b, cond, self.cond(env), self.churn()),
                    "if(%s) { d := %sFold(d, 5); } else { d := %sFold(d, 6); };" % (b, U, U)]
        if form == 3 and os_:
            # a ternary as an argument, with a reference pending beside it
            o = self.pick(os_).name
            return ["d := %sFold(d, %sPair(%s, ((%s) ? %s : %s)));" % (U, U, o, cond, self.churn(), self.salt(env))]
        return self.stmt_churn(env, ctx)

    def cond(self, env):
        os_ = [v for v in env if v.kind == "O"]
        opts = [(4, "%s %% %d = %d" % (self.salt(env), self.rng.randrange(2, 5), 0)),
                (2, "%s < %d" % (self.salt(env), self.rng.randrange(3, 60)))]
        if os_:
            opts.append((2, "%sDigO(%s) %% 3 <> 0" % (U, self.pick(os_).name)))
        return self.weighted(opts)

    def stmt_try(self, env, ctx):
        os_ = [v for v in env if v.kind == "O"]
        ss = [v for v in env if v.kind == "S"]
        if not os_ or not ss:
            return self.stmt_churn(env, ctx)
        self.feat("try")
        r = self.u("r")
        o = self.pick(os_).name
        s = self.pick(ss).name
        k = self.salt(env)
        if self.addr or self.chance(50):
            # (with address-valued Ints the region always sits in a helper: an interpreted
            # method under compiled code still scans its operand stack on a guess)
            # the region sits in a small helper: a method with a ?-> region is never compiled
            # (TRY_START), so this keeps the scenario itself a JIT candidate
            self.feat("try_in_helper")
            if self.chance(50):
                return ["d := %sFold(d, %sTr1(%s, %s, %s));" % (U, U, o, s, k)]
            return ["d := %sFold(d, %sTr2(%d, %s, %s, %s));" % (U, U, self.rng.randrange(1, 5), o, s, k)]
        if self.chance(50):
            call = "Guard->New()?->Risky(%s, %s, %s)" % (o, s, k)
        else:
            call = "Guard->New()?->Deep(%d, %s, %s, %s)" % (self.rng.randrange(1, 5), o, s, k)
        return ["%s := %s;" % (r, call),
                "if(%s = Nil) { Runtime->ClearError(); d := %sFold(d, 77); } else { d := %sFold(d, %s->Get()); };" % (r, U, U, r)]

    def stmt_early(self, env, ctx):
        if ctx.get("noexit"):
            return self.stmt_churn(env, ctx)
        vs = self.pick_some(env, 3)
        body = ["return %sFold(d, %s);" % (U, " + ".join(["0"] + [self.dig(v) for v in vs]))]
        k = self.rng.randrange(5, 9)
        j = self.rng.randrange(0, k)
        self.feat("early_exit")
        return ["if(%s %% %d = %d) {" % (self.salt(env), k, j)] + ind(body) + ["};"]

    def stmt_loopctl(self, env, ctx):
        loop = ctx.get("loop")
        if not loop or ctx.get("in_select"):
            return self.stmt_churn(env, ctx)
        self.feat("break_continue")
        kw = self.pick(["break", "continue"])
        return ["if(%s %% %d = %d) { %s; };" % (self.salt(env), self.rng.randrange(3, 7), self.rng.randrange(0, 3), kw)]

    def pick_some(self, env, n):
        if not env:
            return []
        return [self.pick(env) for _ in range(self.rng.randrange(1, n + 1))]

    def stmt_if(self, env, ctx, depth):
        a = self.block(list(env), ctx, depth + 1, self.rng.randrange(2, 5))
        L = ["if(%s) {" % self.cond(env)] + ind(a)
        if self.chance(50):
            L += ["} else {"] + ind(self.block(list(env), ctx, depth + 1, self.rng.randrange(1, 4))) + ["};"]
        else:
            L.append("};")
        return L

    def stmt_select(self, env, ctx, depth):
        sv = self.u("sel")
        nl = self.rng.randrange(2, 5)
        L = ["%s := %s %% %d;" % (sv, self.salt(env), nl + 1), "select(%s) {" % sv]
        sub = dict(ctx)
        sub["in_select"] = True
        for i in range(nl):
            L.append("  label %d: {" % i)
            L += ind(self.block(list(env), sub, depth + 1, self.rng.randrange(1, 4)), 2)
            L.append("  }")
        L.append("  other: {")
        L += ind(self.block(list(env), sub, depth + 1, 1), 2)
        L.append("  }")
        L.append("};")
        return L

    def stmt_loop(self, env, ctx, depth):
        form = self.weighted([(4, "for"), (3, "each"), (3, "while")])
        iv = self.u("i")
        k = self.rng.randrange(2, 6)
        sub = dict(ctx)
        sub["loop"] = form
        self.feat("loop")
        inner = list(env)
        inner.append(Var(iv, "I", ro=True))
        if form == "for":
            head = ["for(%s := 0; %s < %d; %s += 1;) {" % (iv, iv, k, iv)]
        elif form == "each":
            head = ["each(%s : %d) {" % (iv, k)]
        else:
            head = ["%s := 0;" % iv, "while(%s < %d) {" % (iv, k), "  %s += 1;" % iv]
        body = self.block(inner, sub, depth + 1, self.rng.randrange(3, 7), loop_digest=True)
        return head + ind(body) + ["};"]

    # ---- blocks -----------------------------------------------------------------------
    def block(self, env, ctx, depth, count, loop_digest=False):
        """Statements; variables declared here go out of scope at the end, after being digested."""
        base = len(env)
        L = []
        for _ in range(max(1, int(count * self.scale))):
            opts = [(10, self.stmt_decl), (6, self.stmt_churn), (12, self.stmt_mutate), (12, self.stmt_pending),
                    (6, self.stmt_expr_pending), (5, self.stmt_call), (4, self.stmt_use_closure),
                    (3, self.stmt_ternary), (4, self.stmt_try), (3, self.stmt_address)]
            if depth < MAX_DEPTH:
                opts += [(8, lambda e, c: self.stmt_loop(e, c, depth)), (5, lambda e, c: self.stmt_if(e, c, depth)),
                         (3, lambda e, c: self.stmt_select(e, c, depth))]
            if depth >= 1:
                opts += [(3, self.stmt_early)]
            if ctx.get("loop"):
                opts += [(4, self.stmt_loopctl)]
            L += self.weighted(opts)(env, ctx)
        # fold what this block declared (and what a loop's locals ended as) into d
        for v in env[base:]:
            if not v.ro:
                L.append("d := %sFold(d, %s);" % (U, self.dig(v)))
        return L

    def final_digest(self, env):
        return ["d := %sFold(d, %s);" % (U, self.dig(v)) for v in env if not v.ro]

    def seed_env(self, env, field=False, only=None):
        """The scenario's first locals: a spread of reference kinds beside Int and Float."""
        L = []
        kinds = list(only or ALL_KINDS)
        want = [self.pick([k for k in kinds if k in REF_KINDS]) for _ in range(self.rng.randrange(3, 6))]
        want += ["I"] * self.rng.randrange(2, 5) + ["D"] * self.rng.randrange(0, 3)
        self.rng.shuffle(want)
        for k in want:
            L += self.decl(env, k)
        return L

    # ---- scenarios ----------------------------------------------------------------------
    def scenario_static(self, idx):
        env = []
        body = ["d := n;"] + self.seed_env(env)
        body += self.block(env, {}, 0, self.rng.randrange(6, 13))
        body += self.final_digest(env)
        body.append("return d;")
        return (["  function : Sc%d(n : Int) ~ Int {" % idx] + ind(body, 2) + ["  }", ""], False)

    def scenario_instance(self, idx):
        """References are fields: self is a root, and every store is through the write barrier."""
        self.feat("instance_fields")
        fields = []
        nf = self.rng.randrange(3, 6)
        kinds = [self.pick([k for k in FIELD_KINDS if k in REF_KINDS]) for _ in range(nf)] + ["I", "D"]
        fenv = []
        decls = []
        cons = []
        for k in kinds:
            nm = "@f%d" % (len(fenv) + 1)
            before = len(fenv)
            cons += self.decl(fenv, k, name=nm, field=True, s=str(self.rng.randrange(1, 50)))
            fields.append("  %s : %s;" % (nm, TYPES[k]))
        env = list(fenv)
        body = ["d := n;"]
        # a few locals beside the fields
        for k in [self.pick("SAOI"), self.pick("SOI"), "I"]:
            body += self.decl(env, k)
        body += self.block(env, {}, 0, self.rng.randrange(6, 12))
        body += self.final_digest(env)
        body.append("return d;")
        cname = "Hold%d" % idx
        text = ["class %s {" % cname] + fields + ["", "  New() {"] + ind(cons, 2) + ["  }", "",
                "  method : public : Run(n : Int) ~ Int {"] + ind(body, 2) + ["  }", "}", ""]
        return (text, True)

    def scenario_thread(self, idx, worker_scenarios):
        """Live references in the spawning thread while workers run generated scenarios."""
        self.feat("threads")
        env = []
        body = ["d := n;"] + self.seed_env(env)
        body += self.block(env, {"noexit": True}, 1, self.rng.randrange(2, 5))
        nw = self.rng.randrange(2, 4)
        body.append("ws := RcWorker->New[%d];" % (nw + 1))
        for i in range(nw):
            body.append("ws[%d] := RcWorker->New(%d, n + %d);" % (i, self.pick(worker_scenarios), i))
        body.append("ws[%d] := RcWorker->New(-1, %d + n %% 3);" % (nw, self.rng.randrange(8, 20)))
        body.append("each(i : ws) { ws[i]->Execute(Nil); };")
        body += self.block(env, {"noexit": True}, 1, self.rng.randrange(3, 7))
        body.append("each(i : ws) { ws[i]->Join(); };")
        body.append("each(i : ws) { d := %sFold(d, ws[i]->Res()); };" % U)
        body += self.final_digest(env)
        body.append("return d;")
        return (["  function : Sc%d(n : Int) ~ Int {" % idx] + ind(body, 2) + ["  }", ""], False)

    # ---- the program ----------------------------------------------------------------------
    def program(self):
        n_static = self.rng.randrange(3, 6)
        n_inst = self.rng.randrange(1, 3)
        n_thread = 1 if self.chance(70) else 0
        scen = []        # (index, text, is_class, thread_scenario)
        classes = []
        funcs = []
        idx = 0
        plain = []       # scenarios a worker thread may run
        for _ in range(n_static):
            t, _c = self.scenario_static(idx)
            funcs += t
            scen.append((idx, False))
            plain.append(idx)
            idx += 1
        for _ in range(n_inst):
            t, _c = self.scenario_instance(idx)
            classes += t
            funcs += ["  function : Sc%d(n : Int) ~ Int {" % idx,
                      "    return Hold%d->New()->Run(n);" % idx, "  }", ""]
            scen.append((idx, False))
            plain.append(idx)
            idx += 1
        for _ in range(n_thread):
            t, _c = self.scenario_thread(idx, plain)
            funcs += t
            scen.append((idx, True))
            idx += 1
        self.sc_count = idx

        out = ["# Generated by tools/gc_roots/gen_roots.py --seed %d. Do not edit." % self.seed,
               "use System.Concurrency;", ""]
        out += PRELUDE_CLASSES
        out += classes
        out += self.util_class()
        out += ["class RcMain {"]
        out += funcs
        # dispatch for worker threads
        out += ["  function : Dispatch(sc : Int, n : Int) ~ Int {", "    select(sc) {"]
        for i in plain:
            out.append("      label %d: { return Sc%d(n); }" % (i, i))
        out += ["      other: { return 0; }", "    };", "    return 0;", "  }", ""]
        out += ["  function : Main(args : String[]) ~ Nil {", "    total := 0;"]
        for i, _t in scen:
            out.append("    s%d := 0;" % i)
        out.append("    for(rep := 0; rep < %d; rep += 1;) {" % REPS)
        for i, is_thread in scen:
            if is_thread:
                out.append("      if(rep %% 3 = 0) { s%d := %sFold(s%d, Sc%d(rep)); };" % (i, U, i, i))
            else:
                out.append("      s%d := %sFold(s%d, Sc%d(rep));" % (i, U, i, i))
        out.append("    };")
        for i, _t in scen:
            out.append("    \"s%d={$s%d}\"->PrintLine();" % (i, i))
            out.append("    total := %sFold(total, s%d);" % (U, i))
        out += ["    \"total={$total}\"->PrintLine();", "  }", "}", ""]
        return "\n".join(out)

    def util_class(self):
        ch = self.churn_unit
        big = ""
        if self.big_every:
            big = "      if(i %% %d = 0) { big := Int->New[600]; big[7] := i; t += big[7] %% 3; };\n" % self.big_every
        text = UTIL_HEAD % (ch, big)
        L = [text]
        for _name, t in self.helpers.values():
            L += t
        L += ["}", ""]
        return L


PRELUDE_CLASSES = """class RcError from Error {
  @k : Int;
  New(m : String, k : Int) { Parent(m); @k := k; }
  method : public : K() ~ Int { return @k; }
}

class Box {
  @v : Int;
  @r : Box;
  New(v : Int) { @v := v; @r := Nil; }
  method : virtual : public : Get() ~ Int;
  method : public : Set(v : Int) ~ Nil { @v := v; }
  method : public : Link() ~ Box { return @r; }
  method : public : SetLink(b : Box) ~ Nil { @r := b; }
  method : public : Mix(c : Int, x : Int) ~ Int { return Get() + c * 3 + x; }
}

class Ba from Box {
  New(v : Int) { Parent(v); }
  method : public : Get() ~ Int { return @v; }
}

class Bb from Box {
  New(v : Int) { Parent(v); }
  method : public : Get() ~ Int { return @v + 1000; }
}

class Guard {
  New() {}

  method : public : Risky(b : Box, s : String, k : Int) ~ IntRef {
    t := Ba->New(k);
    x := RcU->Churn(1);
    if(k % 3 = 0) { Runtime->Raise(RcError->New("r", k)); };
    z := t->Get() - k;
    if(k % 3 = 1) { return IntRef->New(10 / z); };
    return IntRef->New(b->Get() + s->Size() + x % 10);
  }

  method : public : Deep(m : Int, b : Box, s : String, k : Int) ~ IntRef {
    t := Ba->New(m + k);
    a := Int->New[3];
    a[0] := m;
    if(m = 0) { return Risky(b, s, k); };
    r := Deep(m - 1, b, s, k);
    if(r = Nil) { return Nil; };
    return IntRef->New(r->Get() + t->Get() + a[0] + RcU->Churn(1) %% 5);
  }
}

class RcWorker from Thread {
  @sc : Int;
  @n : Int;
  @res : Int;

  New(sc : Int, n : Int) { Parent("rc"); @sc := sc; @n := n; @res := 0; }
  method : public : Res() ~ Int { return @res; }

  method : public : Run(param : System.Base) ~ Nil {
    if(@sc < 0) {
      t := 0;
      for(i := 0; i < @n; i += 1;) { t += RcU->Churn(3); };
      @res := t;
    }
    else {
      @res := RcMain->Dispatch(@sc, @n);
    };
  }
}
""".replace("%%", "%").split("\n")

UTIL_HEAD = """class RcU {
  function : Fold(d : Int, x : Int) ~ Int {
    return (d * 131 + x) %% 1000000007;
  }

  function : Churn(n : Int) ~ Int {
    t := 0;
    for(i := 0; i < n * %d; i += 1;) {
      b := Ba->New(i);
      b->SetLink(Ba->New(i + 1));
      s := "c{$i}";
      a := Int->New[(i %% 5) + 2];
      a[0] := i;
      t += b->Link()->Get() + s->Size() + a[0] + a->Size();
%s    };
    return t %% 10007;
  }

  function : DigS(s : String) ~ Int {
    t := s->Size();
    each(i : s->Size()) { t := (t * 31 + s->Get(i)->As(Int)) %% 1000003; };
    return t;
  }

  function : DigA(a : Int[]) ~ Int {
    t := a->Size();
    each(i : a) { t := (t * 31 + a[i]) %% 1000003; };
    return t;
  }

  function : DigO(b : Box) ~ Int {
    t := 0;
    c := b;
    k := 0;
    while(c <> Nil & k < 6) {
      t := (t * 31 + c->Get()) %% 1000003;
      c := c->Link();
      k += 1;
    };
    return t;
  }

  function : DigB(b : Box[]) ~ Int {
    t := b->Size();
    each(i : b) { t := (t * 31 + DigO(b[i])) %% 1000003; };
    return t;
  }

  function : DigT(a : String[]) ~ Int {
    t := a->Size();
    each(i : a) {
      if(a[i] <> Nil) { t := (t * 31 + DigS(a[i])) %% 1000003; };
    };
    return t;
  }

  function : Eq3(a : Int, c : Int, b : Int) ~ Int {
    if(a = b) { return 100 + c %% 10; };
    return c %% 10;
  }

  function : Mk(x : Int) ~ Box {
    if(x %% 3 = 0) { return Bb->New(x); };
    return Ba->New(x);
  }

  function : MkBa(x : Int) ~ Box {
    return Ba->New(x);
  }

  function : Chain(x : Int, len : Int) ~ Box {
    h := Mk(x);
    for(i := 1; i < len; i += 1;) {
      n := Mk(x + i);
      n->SetLink(h);
      h := n;
      h->Set(h->Get() + Churn(1) %% 1);
    };
    return h;
  }

  function : Mkarr(x : Int) ~ Int[] {
    a := Int->New[(x %% 4) + 2];
    each(i : a) { a[i] := (x + i) %% 1000; };
    return a;
  }

  function : Mkb(x : Int) ~ Box[] {
    b := Box->New[3];
    b[0] := Mk(x);
    b[1] := Chain(x, 2);
    return b;
  }

  function : Mkt(x : Int) ~ String[] {
    t := String->New[3];
    t[0] := "m{$x}";
    t[1] := "n{$(Churn(1))}";
    return t;
  }

  function : Mkf(x : Int) ~ FuncRef<IntRef> {
    c := Mk(x);
    return FuncRef->New(\\() ~ IntRef : () => IntRef->New(c->Get() + 3))<IntRef>;
  }

  function : Tr1(b : Box, s : String, k : Int) ~ Int {
    r := Guard->New()?->Risky(b, s, k);
    if(r = Nil) { Runtime->ClearError(); return 77; };
    return r->Get();
  }

  function : Tr2(m : Int, b : Box, s : String, k : Int) ~ Int {
    r := Guard->New()?->Deep(m, b, s, k);
    if(r = Nil) { Runtime->ClearError(); return 78; };
    return r->Get();
  }

  function : Pair(b : Box, x : Int) ~ Int {
    return b->Get() * 7 + x;
  }

  function : Tiny(b : Box, x : Int) ~ Int {
    t := Ba->New(x + b->Get());
    return t->Get() * 2 + 1;
  }

  function : Tiny2(s : String, a : Int[], x : Int) ~ Int {
    return Churn(1) + s->Size() + a[0] + x;
  }

  function : Tiny3(b : Box, c : Int, x : Int) ~ Int {
    n := Ba->New(c);
    n->SetLink(b);
    return n->Link()->Get() + x;
  }

  function : Rec(k : Int, b : Box, s : String, a : Int[]) ~ Int {
    if(k = 0) { return DigO(b) + DigS(s) + DigA(a); };
    nb := Ba->New(k + b->Get() %% 1000);
    ns := "{$k}{$(s->Size())}";
    na := Int->New[3];
    na[0] := k;
    r := Rec(k - 1, nb, ns, na);
    junk := Churn(1);
    return Fold(r, DigO(nb) + DigS(ns) + DigA(na) + DigA(a) + DigS(s) + DigO(b) + junk %% 1);
  }

"""


def info(g, text):
    return {"seed": g.seed, "features": sorted(g.features), "address_int": g.uses_addr,
            "scenarios": g.sc_count, "lines": text.count("\n")}


def generate(seed, scale=1.0, addr="off"):
    g = Gen(seed, scale, addr)
    text = g.program()
    return text, info(g, text)


def main():
    ap = argparse.ArgumentParser(description=__doc__.split("\n")[0])
    ap.add_argument("--seed", type=int, required=True)
    ap.add_argument("--scale", type=float, default=1.0, help="statement count multiplier")
    ap.add_argument("--addr", choices=("auto", "on", "off"), default="off",
                    help="address-valued Int statements (needs OBJECK_GC_TEST_HOOKS=1): auto = seeds divisible by 3")
    ap.add_argument("--info", action="store_true", help="print the program's features as JSON instead")
    a = ap.parse_args()
    text, meta = generate(a.seed, a.scale, a.addr)
    if a.info:
        print(json.dumps(meta))
    else:
        sys.stdout.write(text)


if __name__ == "__main__":
    main()
