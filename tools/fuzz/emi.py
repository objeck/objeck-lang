#!/usr/bin/env python3
"""EMI (equivalence modulo inputs) mutation of existing Objeck programs.

    python tools/fuzz/emi.py --bin core/release/deploy-x64/bin --variants 10 --seed 1 -j 8
    python tools/fuzz/emi.py --bin <bin> --tests core_arithmetic,opt_cse --variants 4

Takes regression tests that compile and exit 0 (programs/regression/*.obs,
minus EXPECT_*, NONDETERMINISTIC_OUTPUT / DIFF_* / JIT opt-out markers, network,
threads and timing tests) and derives variants that must print exactly what the
original prints under -opt s0 --jit=off:

  dead      insert `if(EmiGuardZq->Dead()) { ... };` at a statement boundary.
            The guard reads a static set in Main from args->Size(), so the s3
            folder cannot prove it false; the block writes live Int locals,
            clones the previous statement, loops, and first calls
            EmiGuardZq->Trip(<mark>) (prints and exits 97), so a guard that is
            wrongly taken shows up as a divergence.
  delete    empty blocks that never ran. A probe build puts
            `EmiGuardZq->Probe(K);` at the top of every if/else/while/for/each/
            do/label block and runs once at s0/off; blocks whose probe never
            printed (to stderr) are candidates. No coverage tooling needed.
  identity  wrap Int literals and Int locals in identities over runtime values
            (`(x + EmiGuardZq->Zero())`, `(7 * EmiGuardZq->One())`, `xor`...),
            Float literals in `* EmiGuardZq->FOne()`, and if/while conditions
            in `(c) & EmiGuardZq->Live()` / `(c) | EmiGuardZq->Dead()`.

Every variant is compiled at s0 and s3 and run under --jit=off and --jit=1.
Configurations are compared with the original's s0/off run (stdout and
zero/non-zero exit, as fuzzlib does). A variant that fails its s0 compile is
repaired by dropping the mutations named by the error lines (else half of
them), up to a few times, and is otherwise discarded as invalid. The oracle is
strict: any configuration whose outcome differs from the original's s0/off run
is a divergence -- the configurations split, an s3 compile fails, a run
crashes, or every configuration agrees on a different output. The last kind
is a *uniform* divergence (a front-end or emitter miscompile that no opt level
or JIT setting escapes); its signature starts with `uniform: <reason>;`, the
reason being `dead guard taken` (EMI-DEAD printed or exit 97), `exit changed`
or `output changed`. Every divergence is re-run twice to confirm, reduced to a
minimal set of mutations, saved under --out/diverge/<test>/v<index>/ and
counted in the exit status.

Exit status: 0 no confirmed divergence, 1 divergences, 2 usage error.
"""

import argparse
import concurrent.futures
import fnmatch
import json
import os
import random
import re
import subprocess
import sys
import threading
import time

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(os.path.dirname(HERE))
REG_DIR = os.path.join(ROOT, "programs", "regression")
sys.path.insert(0, HERE)
import fuzzlib  # noqa: E402

sys.path.insert(0, REG_DIR)
import run_differential as rdiff  # noqa: E402

GUARD = "EmiGuardZq"
MARK_BASE = 910000
TRIP_EXIT = 97

# (name, opt level, obr flags). Every one is compared with the original's s0/off.
CONFIGS = [
    ("s0/off", "s0", ["--jit=off"]),
    ("s3/off", "s3", ["--jit=off"]),
    ("s0/jit1", "s0", ["--jit=1"]),
    ("s3/jit1", "s3", ["--jit=1"]),
]
CONFIG_NAMES = [c[0] for c in CONFIGS]
REF = "ref"  # the original program's s0/off run, first in every partition

GUARD_CLASS = """
class %(g)s {
  @dead : static : Bool;
  @zero : static : Int;
  @hits : static : Int[];

  function : Init(args : String[]) ~ Nil {
    @dead := args->Size() > 4096;
    @zero := args->Size() * 0;
  }

  function : Dead() ~ Bool {
    return @dead;
  }

  function : Live() ~ Bool {
    return @dead = false;
  }

  function : Zero() ~ Int {
    return @zero;
  }

  function : One() ~ Int {
    return @zero + 1;
  }

  function : FOne() ~ Float {
    return (@zero + 1)->As(Float);
  }

  function : Trip(mark : Int) ~ Nil {
    "EMI-DEAD {$mark}"->PrintLine();
    Runtime->Exit(%(trip)d);
  }

  function : Probe(k : Int) ~ Nil {
    if(@hits = Nil) {
      @hits := Int->New[%(nprobe)d];
    };
    if(@hits[k] = 0) {
      @hits[k] := 1;
      "@@EMI-PROBE {$k}"->ErrorLine();
    };
  }
}
"""

# Tests excluded by name or source even without a marker: network, timing,
# host services, the debugger and native media.
# Identifiers after which an expression starts: `return -5` has a unary minus
# and `return [1, 2]` an array literal, not a binary operator or an index.
KEYWORDS_BEFORE_EXPR = frozenset(("return", "leaving", "in", "and", "or", "not"))

# Whole underscore-separated name components, so runtime_* is not "time".
NAME_EXCLUDE = re.compile(r"(?:^|_)(https?|socket|net|tls|web|websocket|mcp|dap|debugger|obd|api|thread|"
                          r"timing|time|timer|bench|perf|stress|sdl|gl|opencv|onnx|ollama|openai|gemini|"
                          r"clock|sleep|signal|process|console)(?=_|$)", re.I)
SOURCE_EXCLUDE = re.compile(r"(System\.Time|Timer->|Date->New|GetTime|Thread->Sleep|System\.IO\.Net|"
                            r"Console->Read|GetEnv|System\.Concurrency|Web\.HTTP)")


# ---------------------------------------------------------------------------
# tokens

class Tok:
    __slots__ = ("kind", "text", "start", "end")

    def __init__(self, kind, text, start, end):
        self.kind, self.text, self.start, self.end = kind, text, start, end

    def __repr__(self):
        return "Tok(%s,%r)" % (self.kind, self.text)


_NUM = re.compile(r"0[xX][0-9A-Fa-f]+|\d+(?:\.\d+)?(?:[eE][+-]?\d+)?[A-Za-z_]*")
_IDENT = re.compile(r"@?[A-Za-z_][A-Za-z0-9_]*")
_PUNCT = ["->", ":=", "+=", "-=", "*=", "/=", "<>", "<=", ">=", "=>", "++", "--", "<<", ">>"]
_INT_LIT = re.compile(r"\d+|0[xX][0-9A-Fa-f]+")
_FLOAT_LIT = re.compile(r"\d+\.\d+(?:[eE][+-]?\d+)?")


def tokenize(src):
    """Objeck tokens with character offsets; comments and blanks dropped."""
    toks = []
    i, n = 0, len(src)
    while i < n:
        c = src[i]
        if c.isspace():
            i += 1
            continue
        if c == "#":
            if src.startswith("#~", i):
                j = src.find("~#", i + 2)
                i = n if j < 0 else j + 2
            else:
                j = src.find("\n", i)
                i = n if j < 0 else j
            continue
        if c in "\"'":
            j = i + 1
            while j < n and src[j] != c:
                j += 2 if src[j] == "\\" else 1
            j = min(j + 1, n)
            toks.append(Tok("str" if c == '"' else "char", src[i:j], i, j))
            i = j
            continue
        if c.isdigit():
            m = _NUM.match(src, i)
            toks.append(Tok("num", m.group(0), i, m.end()))
            i = m.end()
            continue
        if c.isalpha() or c in "_@":
            m = _IDENT.match(src, i)
            if m:
                toks.append(Tok("ident", m.group(0), i, m.end()))
                i = m.end()
                continue
        for p in _PUNCT:
            if src.startswith(p, i):
                toks.append(Tok("punct", p, i, i + len(p)))
                i += len(p)
                break
        else:
            toks.append(Tok("punct", c, i, i + 1))
            i += 1
    return toks


# ---------------------------------------------------------------------------
# structure

STMT_KINDS = ("body", "block", "label", "lambda")
BRACE_KINDS = STMT_KINDS + ("decl", "select", "opaque")
BLOCK_CTRL = ("if", "else", "while", "for", "each", "do")


class Func:
    def __init__(self, name, header, open_index):
        self.name = name
        self.header = header
        self.first = {}  # identifier -> token index of its first appearance (-1: header)
        m = re.search(r"\bMain\s*\(\s*(\w+)\s*:\s*String\s*\[\s*\]\s*\)", header)
        self.main_args = m.group(1) if m else None
        self.is_ctor = name == "New"
        rt = re.search(r"~\s*([A-Za-z_][\w.]*)", header)
        self.returns_value = bool(rt) and rt.group(1) != "Nil"


class Frame:
    def __init__(self, kind, open_index, ctrl=None, code=False, func=None):
        self.kind = kind
        self.open_index = open_index
        self.ctrl = ctrl
        self.code = code
        self.func = func
        self.vars = {}  # name -> "Int" | "Float"
        self.stmt_start = open_index + 1
        self.last_stmt = None  # (start, end) of the previous simple statement
        self.block_id = None


class Boundary:
    def __init__(self, pos, vars_, last_stmt, func, in_lambda):
        self.pos = pos
        self.vars = vars_
        self.last_stmt = last_stmt
        self.func = func
        self.in_lambda = in_lambda


class Block:
    def __init__(self, bid, open_end, close_start, ctrl, parent):
        self.id = bid
        self.open_end = open_end
        self.close_start = close_start
        self.ctrl = ctrl
        self.parent = parent


class Analysis:
    """Mutation sites of one program: statement boundaries, blocks, Int/Float
    literal and variable reads, if/while conditions, Main bodies."""

    def __init__(self, src):
        self.src = src
        self.toks = tokenize(src)
        self.boundaries = []
        self.blocks = []
        self.literals = []   # (start, end, "Int"|"Float")
        self.var_reads = []  # (start, end, "Int"|"Float")
        self.conds = []      # (start, end) of the condition inside if( ... )
        self.mains = []      # (insert pos, args name)
        self._match()
        self._walk()

    def _match(self):
        self.match = {}
        stack = []
        for i, t in enumerate(self.toks):
            if t.kind != "punct":
                continue
            if t.text in "({[":
                stack.append(i)
            elif t.text in ")}]" and stack:
                j = stack.pop()
                self.match[i] = j
                self.match[j] = i

    def _tok(self, i):
        return self.toks[i] if 0 <= i < len(self.toks) else None

    def _text(self, i):
        t = self._tok(i)
        return t.text if t else ""

    def _classify_brace(self, i, frames):
        parent = frames[-1]
        if not parent.code:
            j = i - 1
            while j >= 0 and self.toks[j].text not in (";", "{", "}"):
                j -= 1
            header_toks = self.toks[j + 1:i]
            texts = [t.text for t in header_toks]
            if parent.kind in ("decl",) and ("function" in texts or "method" in texts or
                                             (texts[:1] == ["New"] and "(" in texts)):
                name = ""
                for k, t in enumerate(header_toks):
                    if t.text == "(" and k > 0:
                        name = header_toks[k - 1].text
                        break
                start = header_toks[0].start if header_toks else self.toks[i].start
                func = Func(name, self.src[start:self.toks[i].start], i)
                for k in range(j + 1, i):
                    if self.toks[k].kind == "ident":
                        func.first.setdefault(self.toks[k].text, -1)
                return Frame("body", i, code=True, func=func)
            return Frame("decl" if parent.kind == "decl" else "opaque", i)
        func = parent.func
        prev = self._text(i - 1)
        if prev == "=>":
            return Frame("lambda", i, code=True, func=func)
        if prev in ("else", "do"):
            return Frame("block", i, ctrl=prev, code=True, func=func)
        if prev == ")":
            j = self.match.get(i - 1)
            kw = self._text(j - 1) if j is not None else ""
            if kw in BLOCK_CTRL:
                return Frame("block", i, ctrl=kw, code=True, func=func)
            if kw == "select":
                return Frame("select", i, code=True, func=func)
        if prev == ":" and parent.kind == "select":
            return Frame("label", i, ctrl="label", code=True, func=func)
        return Frame("opaque", i)

    def _scope_vars(self, frames):
        out = {}
        for f in reversed(frames):
            if f.kind in STMT_KINDS:
                for k, v in f.vars.items():
                    out.setdefault(k, v)
            if f.kind in ("body", "lambda"):
                break
        return out

    def _in_lambda(self, frames):
        for f in reversed(frames):
            if f.kind == "lambda":
                return True
            if f.kind == "body":
                return False
        return False

    def _in_array_literal(self, frames):
        for f in reversed(frames):
            if f.kind == "array":
                return True
            if f.kind in BRACE_KINDS:
                return False
        return False

    def _probe_parent(self, frames):
        for f in reversed(frames[:-1]):
            if f.block_id is not None:
                return f.block_id
            if f.kind in ("body", "lambda"):
                return None
        return None

    def _walk(self):
        toks = self.toks
        frames = [Frame("decl", -1)]
        for i, t in enumerate(toks):
            top = frames[-1]
            if t.kind == "punct" and t.text == "{":
                f = self._classify_brace(i, frames)
                frames.append(f)
                if f.kind == "body" and f.func.main_args:
                    self.mains.append((t.end, f.func.main_args))
                if f.kind in STMT_KINDS:
                    if not (f.kind == "body" and f.func.is_ctor):
                        self._boundary(t.end, frames, i)
                if f.kind in ("block", "label") and not self._in_lambda(frames):
                    f.block_id = len(self.blocks)
                    self.blocks.append(Block(f.block_id, t.end, None, f.ctrl, self._probe_parent(frames)))
                if f.kind == "block" and f.ctrl == "for":
                    self._for_header_var(i, f)
                continue
            if t.kind == "punct" and t.text == "}":
                while len(frames) > 1:
                    f = frames.pop()
                    if f.kind in BRACE_KINDS:
                        if f.block_id is not None:
                            self.blocks[f.block_id].close_start = t.start
                        break
                continue
            if t.kind == "punct" and t.text in "([":
                if t.text == "(":
                    frames.append(Frame("paren", i, code=top.code, func=top.func))
                else:
                    prev = self._tok(i - 1)
                    literal = prev is None or prev.text in KEYWORDS_BEFORE_EXPR or \
                        not (prev.kind in ("ident", "num", "str") or prev.text in (")", "]"))
                    frames.append(Frame("array" if literal else "index", i, code=top.code, func=top.func))
                continue
            if t.kind == "punct" and t.text in ")]":
                if top.kind in ("paren", "array", "index"):
                    frames.pop()
                continue
            if not top.code:
                continue
            func = top.func
            if t.kind == "ident" and func is not None:
                func.first.setdefault(t.text, i)
            if t.text == ";" and top.kind in STMT_KINDS:
                self._statement_end(i, frames)
                continue
            if t.kind == "ident" and t.text in ("if", "while") and self._text(i + 1) == "(":
                close = self.match.get(i + 1)
                if close is not None and close > i + 2:
                    self.conds.append((toks[i + 1].end, toks[close].start))
                continue
            if t.kind == "num":
                self._literal(i, frames)
            elif t.kind == "ident":
                self._var_read(i, frames)

    def _boundary(self, pos, frames, i):
        top = frames[-1]
        nxt = self._tok(i + 1)
        # a statement after the last one of a value-returning body can trip the
        # all-paths-return check
        if nxt is not None and nxt.text == "}" and top.kind == "body" and top.func.returns_value:
            return
        in_lambda = self._in_lambda(frames)
        self.boundaries.append(Boundary(pos, {} if in_lambda else self._scope_vars(frames),
                                        top.last_stmt, top.func, in_lambda))

    def _statement_end(self, i, frames):
        top = frames[-1]
        stmt = self.toks[top.stmt_start:i]
        texts = [t.text for t in stmt]
        func = top.func
        if stmt and stmt[0].kind == "ident" and func is not None and func.first.get(stmt[0].text) == top.stmt_start:
            name = stmt[0].text
            if len(stmt) >= 3 and texts[1] == ":" and texts[2] in ("Int", "Float") and (len(stmt) == 3 or texts[3] == ":="):
                top.vars[name] = texts[2]
            elif len(stmt) in (3, 4) and texts[1] == ":=":
                lit = stmt[-1]
                neg = len(stmt) == 4 and texts[2] == "-"
                if lit.kind == "num" and (len(stmt) == 3 or neg):
                    if _INT_LIT.fullmatch(lit.text):
                        top.vars[name] = "Int"
                    elif _FLOAT_LIT.fullmatch(lit.text):
                        top.vars[name] = "Float"
        terminal = bool(texts) and texts[0] in ("return", "break", "continue")
        simple = bool(stmt) and not terminal and "{" not in texts and "=>" not in texts and "label" not in texts
        top.last_stmt = (stmt[0].start, self.toks[i].end) if simple else None
        top.stmt_start = i + 1
        if not terminal:
            self._boundary(self.toks[i].end, frames, i)

    def _for_header_var(self, brace, frame):
        close = brace - 1
        open_ = self.match.get(close)
        if open_ is None:
            return
        toks = self.toks
        k = open_ + 1
        if k + 3 < close and toks[k].kind == "ident" and toks[k + 1].text == ":=" and \
                toks[k + 2].kind == "num" and _INT_LIT.fullmatch(toks[k + 2].text) and toks[k + 3].text == ";" and \
                frame.func is not None and frame.func.first.get(toks[k].text) == k:
            frame.vars[toks[k].text] = "Int"

    def _literal(self, i, frames):
        t = self.toks[i]
        top = frames[-1]
        if top.kind == "select" or self._in_array_literal(frames):
            return
        prev = self._tok(i - 1)
        if prev is not None and prev.text in ("-", "+"):
            pp = self._tok(i - 2)
            binary = pp is not None and pp.text not in KEYWORDS_BEFORE_EXPR and \
                (pp.kind in ("ident", "num", "str", "char") or pp.text in (")", "]"))
            if not binary:
                return
        if prev is not None and prev.text == "label":
            return
        if _INT_LIT.fullmatch(t.text):
            try:
                if int(t.text, 0) > (1 << 62):
                    return
            except ValueError:
                return
            self.literals.append((t.start, t.end, "Int"))
        elif _FLOAT_LIT.fullmatch(t.text):
            self.literals.append((t.start, t.end, "Float"))

    def _var_read(self, i, frames):
        t = self.toks[i]
        if frames[-1].kind == "select" or self._in_array_literal(frames):
            return
        vt = self._scope_vars(frames).get(t.text)
        if vt is None:
            return
        prev, nxt = self._text(i - 1), self._text(i + 1)
        if prev in ("->", ":", "@", ".", "++", "--", "&", "label"):
            return
        if nxt in (":=", "+=", "-=", "*=", "/=", "->", "++", "--", "[", "(", ":", "{"):
            return
        self.var_reads.append((t.start, t.end, vt))


# ---------------------------------------------------------------------------
# variants

class Edit:
    def __init__(self, start, end, text, order=0):
        self.start, self.end, self.text, self.order = start, end, text, order


class Mutation:
    def __init__(self, kind, desc, edits):
        self.kind, self.desc, self.edits = kind, desc, edits

    def to_json(self):
        return {"kind": self.kind, "desc": self.desc}


class Variant:
    def __init__(self, src, base, mutations, key):
        self.src = src
        self.base = base
        self.mutations = mutations
        self.key = key

    def with_mutations(self, mutations):
        return Variant(self.src, self.base, mutations, self.key)

    def build(self):
        """(text, [(first line, last line)] per mutation) of this variant."""
        return build_text(self.src, self.base, self.mutations)

    @property
    def text(self):
        return self.build()[0]


def build_text(src, base, mutations):
    tagged = [(e, None) for e in base]
    for m_index, m in enumerate(mutations):
        tagged += [(e, m_index) for e in m.edits]
    tagged.sort(key=lambda p: (p[0].start, p[0].order))
    out = []
    pos = 0
    line = 1
    spans = [[None, None] for _ in mutations]
    for e, m_index in tagged:
        chunk = src[pos:e.start] if e.start >= pos else ""
        out.append(chunk)
        line += chunk.count("\n")
        first = line
        out.append(e.text)
        line += e.text.count("\n")
        if m_index is not None:
            s = spans[m_index]
            s[0] = first if s[0] is None else min(s[0], first)
            s[1] = line if s[1] is None else max(s[1], line)
        pos = max(pos, e.end)
    out.append(src[pos:])
    return "".join(out), [tuple(s) for s in spans]


def _wrap_edits(start, end, prefix, suffix):
    span = end - start
    return [Edit(start, start, prefix, order=-span - 1), Edit(end, end, suffix, order=span + 1)]


def _int_identity(rng, x_is_literal):
    g = GUARD
    forms = [("(", " + %s->Zero())" % g), ("(", " - %s->Zero())" % g), ("(", " * %s->One())" % g),
             ("(%s->Zero() + " % g, ")"), ("(", " xor %s->Zero())" % g), ("(", " or %s->Zero())" % g),
             ("(%s->One() * " % g, ")")]
    if x_is_literal:
        forms.append(("((", " + %s->One()) - %s->One())" % (g, g)))
    return rng.choice(forms)


def _float_identity(rng):
    return rng.choice([("(", " * %s->FOne())" % GUARD), ("(", " / %s->FOne())" % GUARD),
                       ("(%s->FOne() * " % GUARD, ")")])


def unhit_blocks(analysis, hits):
    """Blocks whose probe never printed and whose enclosing probed block did."""
    out = []
    for b in analysis.blocks:
        if b.close_start is None or b.id in hits:
            continue
        if b.parent is not None and b.parent not in hits:
            continue
        out.append(b)
    return out


def dead_block(rng, boundary, mark, src):
    g = GUARD
    items = ["%s->Trip(%d);" % (g, mark)]
    ints = sorted(n for n, t in boundary.vars.items() if t == "Int")
    floats = sorted(n for n, t in boundary.vars.items() if t == "Float")
    for _ in range(rng.randint(0, 3)):
        choices = ["nested"]
        if ints:
            choices += ["add", "mul", "loop", "sub"]
        if floats:
            choices.append("float")
        if boundary.last_stmt:
            choices.append("clone")
        c = rng.choice(choices)
        if c == "add":
            items.append("%s += %d;" % (rng.choice(ints), rng.randint(1, 99)))
        elif c == "mul":
            items.append("%s := %s * %d;" % (rng.choice(ints), rng.choice(ints), rng.randint(2, 9)))
        elif c == "sub":
            items.append("%s := %s - %s;" % (rng.choice(ints), rng.choice(ints), rng.choice(ints)))
        elif c == "float":
            v = rng.choice(floats)
            items.append("%s := %s * 2.0;" % (v, v))
        elif c == "loop":
            iv = "emiI%d" % mark
            items.append("for(%s := 0; %s < 3; %s += 1) { %s += %s; };" % (iv, iv, iv, rng.choice(ints), iv))
        elif c == "clone":
            s, e = boundary.last_stmt
            items.append(src[s:e])
        elif c == "nested":
            items.append("if(%s->Dead()) { %s->Trip(%d); };" % (g, g, mark))
    body = " ".join(items)
    form = rng.randrange(4)
    if form == 0:
        return " if(%s->Dead()) { %s };" % (g, body)
    if form == 1:
        return " if(%s->Live()) { } else { %s };" % (g, body)
    if form == 2:
        return " while(%s->Dead()) { %s };" % (g, body)
    return " if(%s->Zero() <> 0) { %s };" % (g, body)


def base_edits(analysis):
    src = analysis.src
    edits = [Edit(pos, pos, " %s->Init(%s);" % (GUARD, args)) for pos, args in analysis.mains]
    edits.append(Edit(len(src), len(src), GUARD_CLASS % {"g": GUARD, "trip": TRIP_EXIT,
                                                          "nprobe": max(1, len(analysis.blocks))},
                      order=10 ** 9))
    return edits


def probe_text(analysis):
    """The program with a Probe(K) at the top of every block."""
    edits = base_edits(analysis)
    for b in analysis.blocks:
        edits.append(Edit(b.open_end, b.open_end, " %s->Probe(%d);" % (GUARD, b.id), order=-(10 ** 9)))
    return build_text(analysis.src, edits, [])[0]


def parse_probe_hits(stderr):
    return {int(m.group(1)) for m in re.finditer(r"@@EMI-PROBE (\d+)", stderr)}


def make_variant(analysis, seed, test, index, hits=None):
    """A deterministic variant: the same (seed, test, index, hits) always gives
    the same text."""
    rng = random.Random("emi:%d:%s:%d" % (seed, test, index))
    src = analysis.src
    muts = []
    focus = rng.choice(["mixed", "mixed", "dead", "identity", "delete"])
    n_dead = rng.randint(1, 4) if focus in ("mixed", "dead") else rng.randint(0, 1)
    n_ident = rng.randint(1, 8) if focus in ("mixed", "identity") else rng.randint(0, 2)
    n_cond = rng.randint(0, 2) if focus in ("mixed", "identity") else 0
    n_del = rng.randint(1, 3) if focus in ("mixed", "delete") else 0

    if analysis.boundaries:
        for k, bi in enumerate(sorted(rng.sample(range(len(analysis.boundaries)),
                                                 min(n_dead, len(analysis.boundaries))))):
            b = analysis.boundaries[bi]
            mark = MARK_BASE + bi
            muts.append(Mutation("dead", "boundary %d in %s" % (bi, b.func.name if b.func else "?"),
                                 [Edit(b.pos, b.pos, dead_block(rng, b, mark, src))]))

    sites = [("lit",) + s for s in analysis.literals] + [("var",) + s for s in analysis.var_reads]
    for si in sorted(rng.sample(range(len(sites)), min(n_ident, len(sites)))):
        what, s, e, vt = sites[si]
        pre, suf = _int_identity(rng, what == "lit") if vt == "Int" else _float_identity(rng)
        muts.append(Mutation("identity", "%s %r" % (what, src[s:e]), _wrap_edits(s, e, pre, suf)))

    for ci in sorted(rng.sample(range(len(analysis.conds)), min(n_cond, len(analysis.conds)))):
        s, e = analysis.conds[ci]
        pre, suf = rng.choice([("(", ") & %s->Live()" % GUARD), ("(", ") | %s->Dead()" % GUARD),
                               ("%s->Live() & (" % GUARD, ")"), ("(%s->Dead() | (" % GUARD, "))")])
        muts.append(Mutation("identity", "cond %r" % src[s:e][:40], _wrap_edits(s, e, pre, suf)))

    if hits is not None and n_del:
        cands = unhit_blocks(analysis, hits)
        for b in sorted(rng.sample(cands, min(n_del, len(cands))), key=lambda b: b.id):
            muts.append(Mutation("delete", "%s block %d" % (b.ctrl, b.id),
                                 [Edit(b.open_end, b.close_start, " ", order=0)]))

    muts = _drop_overlaps(muts)
    if not muts and analysis.boundaries:
        # a variant identical to the original tests nothing: force one dead block
        bi = rng.randrange(len(analysis.boundaries))
        b = analysis.boundaries[bi]
        muts.append(Mutation("dead", "boundary %d in %s" % (bi, b.func.name if b.func else "?"),
                             [Edit(b.pos, b.pos, dead_block(rng, b, MARK_BASE + bi, src))]))
    return Variant(src, base_edits(analysis), muts, (seed, test, index))


def _drop_overlaps(muts):
    """A deletion swallows every other mutation with an edit inside it; two
    deletions never nest (only maximal unhit blocks are candidates)."""
    ranges = [(e.start, e.end) for m in muts if m.kind == "delete" for e in m.edits]
    out = []
    for m in muts:
        if m.kind != "delete" and any(s < e.start < t or s < e.end < t for e in m.edits for s, t in ranges):
            continue
        out.append(m)
    # two wraps of the same token would interleave their halves
    seen = set()
    final = []
    for m in out:
        key = tuple((e.start, e.end) for e in m.edits) if m.kind == "identity" else None
        if key is not None:
            spans = {(m.edits[0].start, m.edits[-1].end)}
            if spans & seen:
                continue
            seen |= spans
        final.append(m)
    return final


# ---------------------------------------------------------------------------
# running

class Tools:
    def __init__(self, bin_dir, obc=None, obr=None, timeout=60.0):
        self.tc = fuzzlib.Toolchain(bin_dir, obc=obc, obr=obr, timeout=timeout)
        self.env = dict(self.tc.env)
        self.env.pop("OBJECK_VM_ARGS", None)
        self.timeout = timeout

    def compile(self, src_path, opt, dest, libs, asm=False):
        if os.path.exists(dest):
            os.remove(dest)
        cmd = fuzzlib.tool_command(self.tc.obc) + ["-src", os.path.abspath(src_path), "-lib", libs,
                                                  "-opt", opt, "-dest", os.path.abspath(dest)]
        if asm:
            cmd.append("-asm")
        r = run_proc(cmd, self.tc.bin_dir, self.env, max(120.0, self.timeout * 2))
        return r, os.path.exists(dest)

    def run(self, obe, flags, cwd, timeout, args=()):
        cmd = fuzzlib.tool_command(self.tc.obr) + list(flags) + [os.path.abspath(obe)] + list(args)
        return run_proc(cmd, cwd, self.env, timeout)


def run_proc(cmd, cwd, env, timeout):
    start = time.monotonic()
    try:
        p = subprocess.run(cmd, env=env, cwd=cwd, stdin=subprocess.DEVNULL, capture_output=True, timeout=timeout)
        return fuzzlib.Result(p.returncode, fuzzlib._text(p.stdout), fuzzlib._text(p.stderr),
                              time.monotonic() - start)
    except subprocess.TimeoutExpired as e:
        return fuzzlib.Result(None, fuzzlib._text(e.stdout), fuzzlib._text(e.stderr),
                              time.monotonic() - start, timed_out=True)


def eligibility(name, text):
    """None when the test can be mutated, else why not."""
    m = rdiff.parse_markers(text)
    if m["compile_error"] or m["runtime_error"]:
        return "EXPECT_* marker"
    if m["nondeterministic"] or m["diff_configs"] is not None or m["requires_jit"] or m["jit_disable"]:
        return "differential opt-out marker"
    if m["serial"]:
        return "network or threads"
    if NAME_EXCLUDE.search(name):
        return "network/timing/host test (name)"
    if SOURCE_EXCLUDE.search(text):
        return "network/timing/host API (source)"
    if not re.search(r"\bfunction\s*:\s*Main\s*\(", text):
        return "no Main"
    return None


def dead_guard_taken(result):
    return result is not None and (result.code == TRIP_EXIT or "EMI-DEAD " in (result.stdout or ""))


def uniform_reason(ref, results, configs=CONFIG_NAMES):
    """Why a variant whose configurations all agree differs from `ref`."""
    rs = [results.get(n) for n in configs]
    if any(dead_guard_taken(r) for r in rs):
        return "dead guard taken"
    if any(fuzzlib.outcome_key(r)[0] != fuzzlib.outcome_key(ref)[0] for r in rs):
        return "exit changed"
    return "output changed"


def classify(ref, results, configs=CONFIG_NAMES):
    """('pass' | 'diverge', signature). `ref` is the original's s0/off Result,
    `results` the variant's per-configuration Results. Every configuration must
    match `ref`; a variant all of whose configurations agree on something else
    is still a divergence, with a signature starting `uniform: <reason>;`."""
    ref_key = fuzzlib.outcome_key(ref)
    keys = {n: fuzzlib.outcome_key(results.get(n)) for n in configs}
    if all(k == ref_key for k in keys.values()):
        return "pass", None
    allres = dict(results)
    allres[REF] = ref
    order = [REF] + list(configs)
    sig = fuzzlib.signature(allres, order)
    sig = sig or "diverge: %s" % fuzzlib.partition_text(fuzzlib.partition(allres, order))
    if len(set(keys.values())) == 1:
        sig = "uniform: %s; %s" % (uniform_reason(ref, results, configs), sig)
    return "diverge", sig


def compile_error_lines(result):
    return {int(m.group(1)) for m in re.finditer(r"\.obs:\((\d+),\d+\)", result.stdout + result.stderr)}


class TestRun:
    """The original and its variants for one regression test."""

    def __init__(self, tools, name, args, log):
        self.tools = tools
        self.name = name
        self.args = args
        self.log = log
        self.src_path = os.path.join(args.tests_dir, name + ".obs")
        with open(self.src_path, encoding="utf-8", errors="replace") as f:
            text = f.read()
        if text.startswith("﻿"):
            text = text[1:]
        self.text = text.replace("\r\n", "\n")
        self.work = os.path.join(args.work, name)
        os.makedirs(self.work, exist_ok=True)
        self.libs = ",".join([rdiff.BASE_LIBS] + rdiff.parse_markers(self.text)["extra_libs"])
        # programs run in an empty directory: sources and .obe files pile up in
        # self.work, and a test that lists its working directory would see them
        self.cwd = os.path.join(self.work, "run")
        os.makedirs(self.cwd, exist_ok=True)
        self.rec = {"test": name, "status": "ok", "variants": 0, "pass": 0, "invalid": 0, "uniform": 0,
                    "diverge": 0, "flaky": 0, "repaired": 0, "findings": [], "notes": []}

    # -- helpers
    def compile_both(self, text, stem, opts=("s0", "s3")):
        src = os.path.join(self.work, stem + ".obs")
        with open(src, "w", encoding="utf-8", newline="\n") as f:
            f.write(text)
        out = {}
        for opt in opts:
            dest = os.path.join(self.work, "%s_%s.obe" % (stem, opt))
            out[opt] = self.tools.compile(src, opt, dest, self.libs) + (dest,)
        return out

    def run_configs(self, compiled, configs=None):
        res = {}
        for name, opt, flags in CONFIGS:
            if configs is not None and name not in configs:
                continue
            res[name] = self.tools.run(compiled[opt][2], flags, self.cwd, self.run_timeout)
        return res

    # -- phases
    def baseline(self):
        comp = self.compile_both(self.text, "orig")
        for opt, (r, wrote, _) in comp.items():
            if r.code != 0 or not wrote:
                self.rec["status"] = "skip"
                self.rec["notes"].append("original does not compile at %s" % opt)
                return False
        self.run_timeout = self.args.timeout
        ref = self.tools.run(comp["s0"][2], ["--jit=off"], self.cwd, self.run_timeout)
        if ref.code != 0:
            self.cwd = self.args.tests_dir
            ref = self.tools.run(comp["s0"][2], ["--jit=off"], self.cwd, self.run_timeout)
        if ref.code != 0 or ref.timed_out:
            self.rec["status"] = "skip"
            self.rec["notes"].append("original s0/off %s" % fuzzlib.describe_exit(ref))
            return False
        again = self.tools.run(comp["s0"][2], ["--jit=off"], self.cwd, self.run_timeout)
        if fuzzlib.outcome_key(again) != fuzzlib.outcome_key(ref):
            self.rec["status"] = "skip"
            self.rec["notes"].append("original output is nondeterministic")
            return False
        self.ref = ref
        self.run_timeout = min(self.args.timeout, max(15.0, 10.0 * ref.seconds))
        orig = self.run_configs(comp)
        self.configs = [n for n in CONFIG_NAMES if fuzzlib.outcome_key(orig[n]) == fuzzlib.outcome_key(ref)]
        dropped = [n for n in CONFIG_NAMES if n not in self.configs]
        if dropped:
            self.rec["notes"].append("original already differs under %s; not compared" % ",".join(dropped))
        if "s0/off" not in self.configs:
            self.rec["status"] = "skip"
            return False
        self.analysis = Analysis(self.text)
        self.rec["sites"] = {"boundaries": len(self.analysis.boundaries), "blocks": len(self.analysis.blocks),
                             "literals": len(self.analysis.literals), "var_reads": len(self.analysis.var_reads),
                             "conds": len(self.analysis.conds)}
        if not self.analysis.mains:
            self.rec["status"] = "skip"
            self.rec["notes"].append("Main(args : String[]) not found")
            return False
        return True

    def probe(self):
        self.hits = None
        if self.args.no_delete or not self.analysis.blocks:
            return
        comp = self.compile_both(probe_text(self.analysis), "probe", opts=("s0",))
        r, wrote, obe = comp["s0"]
        if r.code != 0 or not wrote:
            self.rec["notes"].append("probe build does not compile; no deletions")
            return
        res = self.tools.run(obe, ["--jit=off"], self.cwd, self.run_timeout)
        if fuzzlib.outcome_key(res) != fuzzlib.outcome_key(self.ref):
            self.rec["notes"].append("probe build changes output; no deletions")
            return
        self.hits = parse_probe_hits(res.stderr)
        self.rec["blocks_unhit"] = len(unhit_blocks(self.analysis, self.hits))

    def evaluate(self, variant, stem):
        """(status, signature, compiled, results). status: invalid | compile | pass | semantic | diverge."""
        text = variant.text
        comp = self.compile_both(text, stem)
        r0, w0, _ = comp["s0"]
        if r0.code != 0 or not w0:
            return "invalid", None, comp, {}
        r3, w3, _ = comp["s3"]
        if r3.timed_out or fuzzlib.is_crash_code(r3.code) or r3.code != 0 or not w3:
            sig = fuzzlib.compile_signature({"s0": (r0, w0), "s3": (r3, w3)})
            return "diverge", sig, comp, {}
        res = self.run_configs(comp, self.configs)
        status, sig = classify(self.ref, res, self.configs)
        return status, sig, comp, res

    def repair(self, variant, stem):
        """Drop mutations until the s0 compile succeeds. Returns (variant, outcome)."""
        rng = random.Random("repair:%r" % (variant.key,))
        for attempt in range(5):
            out = self.evaluate(variant, stem)
            if out[0] != "invalid" or not variant.mutations:
                return variant, out
            text, spans = variant.build()
            lines = compile_error_lines(out[2]["s0"][0])
            keep = [m for m, (a, b) in zip(variant.mutations, spans)
                    if not (a is not None and any(a <= ln <= b for ln in lines))]
            if len(keep) == len(variant.mutations):
                keep = [m for m in variant.mutations if rng.random() < 0.5]
            variant = variant.with_mutations(keep)
            self.rec["repaired"] += 1
        return variant, self.evaluate(variant, stem)

    def reduce(self, variant, signature, stem):
        """ddmin over the mutations, keeping the same signature."""
        muts = list(variant.mutations)

        def still(ms):
            st, sig, _, _ = self.evaluate(variant.with_mutations(ms), stem + "_r")
            return st == "diverge" and sig == signature

        n = 2
        while len(muts) >= 2:
            chunk = max(1, len(muts) // n)
            reduced = False
            for i in range(0, len(muts), chunk):
                cand = muts[:i] + muts[i + chunk:]
                if cand and still(cand):
                    muts = cand
                    n = max(n - 1, 2)
                    reduced = True
                    break
            if not reduced:
                if chunk == 1:
                    break
                n = min(len(muts), n * 2)
        if len(muts) == 1 and still([]):
            muts = []
        return variant.with_mutations(muts)

    def variants(self):
        seen = set()
        for index in range(self.args.variants):
            v = make_variant(self.analysis, self.args.seed, self.name, index, self.hits)
            stem = "v%d" % index
            v, (status, sig, comp, res) = self.repair(v, stem)
            if v.text in seen and status != "invalid":
                status = "duplicate"
            seen.add(v.text)
            self.rec["variants"] += 1
            if status == "duplicate":
                self.rec.setdefault("duplicate", 0)
                self.rec["duplicate"] += 1
                continue
            if status in ("invalid", "pass"):
                self.rec[status] += 1
                continue
            # confirm twice before calling it real
            confirmed = True
            for _ in range(2):
                st2, sig2, _, _ = self.evaluate(v, stem + "_c")
                if (st2, sig2) != (status, sig):
                    confirmed = False
                    break
            if not confirmed:
                self.rec["flaky"] += 1
                self.save(v, stem, "flaky", sig, comp, res)
                continue
            self.rec["diverge"] += 1
            if sig.startswith("uniform:"):
                self.rec["uniform"] += 1
            reduced = self.reduce(v, sig, stem) if self.args.reduce else v
            path = self.save(v, stem, "diverge", sig, comp, res, reduced)
            self.rec["findings"].append({"variant": index, "signature": sig, "path": path,
                                         "mutations": [m.to_json() for m in reduced.mutations]})
            self.log("[DIVERGE] %s v%d: %s" % (self.name, index, sig))

    def save(self, variant, stem, kind, sig, comp, res, reduced=None):
        dest = os.path.join(self.args.out, kind, self.name, stem)
        os.makedirs(dest, exist_ok=True)
        with open(os.path.join(dest, "variant.obs"), "w", encoding="utf-8", newline="\n") as f:
            f.write(variant.text)
        if reduced is not None:
            with open(os.path.join(dest, "reduced.obs"), "w", encoding="utf-8", newline="\n") as f:
                f.write(reduced.text)
        info = {"test": self.name, "key": list(variant.key), "signature": sig, "libs": self.libs,
                "mutations": [m.to_json() for m in variant.mutations],
                "reduced_mutations": [m.to_json() for m in reduced.mutations] if reduced else None,
                "reference": self.ref.to_json(),
                "compile": {k: v[0].to_json() for k, v in comp.items()},
                "results": {k: v.to_json() for k, v in res.items()}}
        with open(os.path.join(dest, "outcome.json"), "w", encoding="utf-8") as f:
            json.dump(info, f, indent=1)
        return dest

    def run(self):
        start = time.monotonic()
        try:
            if self.baseline():
                self.probe()
                self.variants()
        except Exception as e:  # keep the other tests going
            self.rec["status"] = "error"
            self.rec["notes"].append("%s: %s" % (type(e).__name__, e))
        self.rec["seconds"] = round(time.monotonic() - start, 1)
        return self.rec


def select_tests(tests_dir, patterns):
    names = sorted(os.path.splitext(f)[0] for f in os.listdir(tests_dir) if f.endswith(".obs"))
    if not patterns:
        return names
    out = []
    for p in patterns:
        p = os.path.splitext(os.path.basename(p))[0]
        out += [n for n in names if fnmatch.fnmatch(n, p) and n not in out]
    return out


def main(argv=None):
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--bin", required=True, help="deploy tree bin directory (obc, obr)")
    ap.add_argument("--tests", action="append", default=[],
                    help="test names or globs, comma separated (default: every eligible test)")
    ap.add_argument("--tests-dir", default=REG_DIR, help="directory of .obs tests (default programs/regression)")
    ap.add_argument("--variants", type=int, default=10, help="variants per test (default 10)")
    ap.add_argument("--seed", type=int, default=1)
    ap.add_argument("-j", "--jobs", type=int, default=4)
    ap.add_argument("--out", default=os.path.join(HERE, "out", "emi"), help="findings directory")
    ap.add_argument("--work", default=None, help="scratch directory (default <out>/work)")
    ap.add_argument("--json", default=None, help="write the summary here")
    ap.add_argument("--timeout", type=float, default=60.0, help="seconds per run, at most")
    ap.add_argument("--no-delete", action="store_true", help="skip the probe run and block deletion")
    ap.add_argument("--no-reduce", dest="reduce", action="store_false")
    ap.add_argument("--obc", default=None, help="compiler override (a .py runs under this Python)")
    ap.add_argument("--obr", default=None, help="VM override")
    ap.add_argument("--list", action="store_true", help="print eligible tests and exit")
    args = ap.parse_args(argv)
    if args.variants < 1 or args.jobs < 1:
        ap.error("--variants and -j must be positive")
    args.tests_dir = os.path.abspath(args.tests_dir)
    args.out = os.path.abspath(args.out)
    args.work = os.path.abspath(args.work or os.path.join(args.out, "work"))
    patterns = [p for group in args.tests for p in group.split(",") if p.strip()]
    names = select_tests(args.tests_dir, patterns)
    if not names:
        print("no tests match", file=sys.stderr)
        return 2

    eligible, skipped = [], {}
    for n in names:
        with open(os.path.join(args.tests_dir, n + ".obs"), encoding="utf-8", errors="replace") as f:
            why = eligibility(n, f.read())
        if why:
            skipped[n] = why
        else:
            eligible.append(n)
    if args.list:
        for n in eligible:
            print(n)
        return 0

    tools = Tools(args.bin, obc=args.obc, obr=args.obr, timeout=args.timeout)
    lock = threading.Lock()

    def log(line):
        with lock:
            print(line, flush=True)

    start = time.monotonic()
    log("emi: %d eligible of %d tests, %d variants each, seed %d, -j %d" %
        (len(eligible), len(names), args.variants, args.seed, args.jobs))
    records = []
    with concurrent.futures.ThreadPoolExecutor(args.jobs) as pool:
        futs = {pool.submit(TestRun(tools, n, args, log).run): n for n in eligible}
        for fut in concurrent.futures.as_completed(futs):
            rec = fut.result()
            records.append(rec)
            if rec["status"] == "ok":
                log("%-4s %-40s variants=%d pass=%d invalid=%d diverge=%d (uniform=%d) flaky=%d (%.0fs)" %
                    ("DIFF" if rec["diverge"] else "ok", rec["test"], rec["variants"], rec["pass"], rec["invalid"],
                     rec["diverge"], rec["uniform"], rec["flaky"], rec["seconds"]))
            else:
                log("%-4s %-40s %s" % (rec["status"], rec["test"], "; ".join(rec["notes"])))
    records.sort(key=lambda r: r["test"])
    total = lambda k: sum(r.get(k, 0) for r in records)
    signatures = {}
    for r in records:
        for f in r["findings"]:
            signatures.setdefault(f["signature"], []).append("%s v%d" % (r["test"], f["variant"]))
    summary = {"seed": args.seed, "variants_per_test": args.variants, "eligible": len(eligible),
               "skipped": skipped, "ran": sum(1 for r in records if r["status"] == "ok"),
               "stats": {k: total(k) for k in ("variants", "pass", "invalid", "diverge", "uniform", "flaky",
                                               "duplicate", "repaired")},
               "signatures": signatures, "tests": records, "seconds": round(time.monotonic() - start, 1)}
    log("")
    log("emi summary: %d tests ran, %s" % (summary["ran"], ", ".join("%s=%d" % kv for kv in summary["stats"].items())))
    for sig, where in sorted(signatures.items()):
        log("  %s  [%s]" % (sig, ", ".join(where)))
    if args.json:
        with open(args.json, "w", encoding="utf-8") as f:
            json.dump(summary, f, indent=1)
    return 1 if signatures else 0


if __name__ == "__main__":
    sys.exit(main())
