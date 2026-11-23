#!/usr/bin/env python3
"""The bytecode instruction table agrees with every hand-written copy of it.

WHY THIS EXISTS
---------------
What an opcode carries and does was written out by hand in a dozen places, and
nothing checked that they agree:

  core/shared/instrs.h            the opcode enum (the numbers ARE the file format)
  core/compiler/emit.cpp          IntermediateInstruction::Write: the operands written
  core/compiler/linker.cpp        Library::LoadStatements: the .obl reader
  core/vm/loader.cpp              Loader::LoadStatements: the .obe reader + verifier
  core/vm/dispatch.cpp            the handler table, indexed by opcode
  core/compiler/optimization.cpp  the label passes and the inliner's slot/label shift

core/shared/instr_info.h is the one description (docs/INSTRUCTION_SET.md). This
script parses it and the sites above and fails when they disagree. Every
mismatch is either a wrong row (fix the row) or a real bug in a site (fix the
site, or list it in KNOWN below with the reason and what fixes it).

WHAT IT CHECKS
--------------
 a. the table has exactly one row per enumerator of InstructionType, in order,
    and instr_info.h still carries the static_asserts that make the compiler say so;
 b. the operands emit.cpp Write writes, and the operands the loader and the .obl
    reader read, equal what the row's roles imply (operand order, int/char/i64/
    double/string kind), and the instruction header (type byte, then line) is read
    in the order it is written; VM-only, library-only and pseudo opcodes have no
    business in the .obe loader;
 c. every opcode that carries a label is in the optimizer's label passes
    (MaxLabel, CleanLabelsLocation, JumpToLocation) and in the inliner (its
    label-collision scan and its shifting copy, or CanInlineMethod refuses the
    callee), reading the operand the table says the label is in; every opcode
    that carries a slot is in the inliner's slot shift;
 d. every opcode that names a jump target is checked by the loader's CheckTarget;
 e. every library-only opcode is resolved by the linker;
 f. dispatch.cpp's handler table lists the handlers in the enum's order.

The sites are found by function name in every core/compiler/*.cpp, not by line,
so a pass that moves to another file (the optimizer is being split) is still
found; one that is renamed fails loudly rather than being skipped.

Usage: check_instr_table.py          (from anywhere in the tree)
       check_instr_table.py --test   (negative controls: each mutation must be caught)
"""

import glob
import os
import re
import sys

ROOT = os.path.normpath(os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", ".."))

INSTRS_H = "core/shared/instrs.h"
INFO_H = "core/shared/instr_info.h"
EMIT_CPP = "core/compiler/emit.cpp"
LINKER_CPP = "core/compiler/linker.cpp"
LOADER_CPP = "core/vm/loader.cpp"
DISPATCH_CPP = "core/vm/dispatch.cpp"
COMPILER_GLOB = "core/compiler/*.cpp"

# Problems that are real and known, each with what fixes it. A listed problem is
# reported as a note, not a failure; one that no longer occurs is a warning, so
# the entry gets removed with the fix.
KNOWN = {
    "order:core/compiler/linker.cpp": (
        "Library::LoadStatements reads the line number before the type byte, but "
        "IntermediateInstruction::Write writes the type, then the line. A library built "
        "with -debug links into scrambled code (the program is refused at load: 'the "
        "method can run past its last instruction'). Fixed by PR #1724 (read the type first); "
        "remove this entry once that has merged."),
}

# Rows whose reader-side rule is waived, and why. Same discipline as KNOWN: the
# entry must still be needed.
LABEL_PRESCAN_EXEMPT = {
    "TRY_START": (
        "InlineMethod's label-collision scan skips TRY_START: its handler label is always "
        "also defined by an LBL in the same method, which the scan does include. CanInlineMethod "
        "refuses a callee with a TRY_START, so the shifting copy never sees one either."),
}


# ----------------------------------------------------------------------------
# Source handling
# ----------------------------------------------------------------------------

def read(rel):
    with open(os.path.join(ROOT, rel), encoding="utf-8-sig", errors="replace") as f:
        return f.read()


def strip_comments(src):
    """Blank comments, and the contents of string and char literals (so a brace or a
    `case` in a message cannot be mistaken for code). Newlines and offsets are kept."""
    out = []
    i = 0
    n = len(src)
    while i < n:
        c = src[i]
        nxt = src[i + 1] if i + 1 < n else ""
        if c == "/" and nxt == "/":
            while i < n and src[i] != "\n":
                out.append(" ")
                i += 1
        elif c == "/" and nxt == "*":
            out.append("  ")
            i += 2
            while i < n and not (src[i] == "*" and i + 1 < n and src[i + 1] == "/"):
                out.append("\n" if src[i] == "\n" else " ")
                i += 1
            out.append("  ")
            i += 2
        elif c == '"' or c == "'":
            quote = c
            out.append(quote)
            i += 1
            while i < n and src[i] != quote:
                if src[i] == "\\" and i + 1 < n:
                    out.append("  ")
                    i += 2
                    continue
                if src[i] == "\n":
                    break
                out.append(" ")
                i += 1
            if i < n and src[i] == quote:
                out.append(quote)
                i += 1
        else:
            out.append(c)
            i += 1
    return "".join(out)


def match_close(text, open_pos, open_ch, close_ch):
    depth = 0
    for i in range(open_pos, len(text)):
        if text[i] == open_ch:
            depth += 1
        elif text[i] == close_ch:
            depth -= 1
            if depth == 0:
                return i
    return -1


def find_defs(text, name):
    """Every definition `X::name(params) { body }` in already comment-stripped text:
    a list of (params, body). A call or a declaration (no brace after the parameters)
    is skipped."""
    defs = []
    for m in re.finditer(r"\b\w+::" + re.escape(name) + r"\s*\(", text):
        close = match_close(text, m.end() - 1, "(", ")")
        if close < 0:
            continue
        j = close + 1
        while j < len(text) and (text[j].isspace() or text.startswith("const", j)):
            j += 5 if text.startswith("const", j) else 1
        if j >= len(text) or text[j] != "{":
            continue
        end = match_close(text, j, "{", "}")
        if end < 0:
            continue
        defs.append((text[m.end():close], text[j + 1:end]))
    return defs


def find_switch(body, selector_re):
    """The inside of the first `switch(<selector>) { ... }` whose selector matches."""
    for m in re.finditer(r"\bswitch\s*\(", body):
        close = match_close(body, m.end() - 1, "(", ")")
        if close < 0:
            continue
        sel = body[m.end():close]
        if not re.search(selector_re, sel):
            continue
        j = close + 1
        while j < len(body) and body[j].isspace():
            j += 1
        if j < len(body) and body[j] == "{":
            end = match_close(body, j, "{", "}")
            if end > 0:
                return body[j + 1:end]
    return None


def case_groups(sw):
    """Split a switch's inside into [(labels, text)]: stacked `case A: case B:` share
    the text that follows them. Only the switch's own labels count (brace depth 0)."""
    marks = []  # (kind, name, start, end)
    depth = 0
    i = 0
    n = len(sw)
    label_re = re.compile(r"(?:\bcase\s+(?:[\w]+::)*(\w+)\s*:(?!:))|(?:\b(default)\s*:(?!:))")
    while i < n:
        c = sw[i]
        if c == "{":
            depth += 1
        elif c == "}":
            depth -= 1
        elif depth == 0 and (sw.startswith("case", i) or sw.startswith("default", i)) and (i == 0 or not (sw[i - 1].isalnum() or sw[i - 1] == "_")):
            m = label_re.match(sw, i)
            if m:
                marks.append((m.group(1) or "default", m.start(), m.end()))
                i = m.end()
                continue
        i += 1
    groups = []
    pending = []
    for k, (name, start, end) in enumerate(marks):
        stop = marks[k + 1][1] if k + 1 < len(marks) else n
        text = sw[end:stop]
        pending.append(name)
        if text.strip():
            groups.append((pending, text))
            pending = []
    if pending:
        groups.append((pending, ""))
    return groups


def group_map(groups):
    result = {}
    for labels, text in groups:
        for name in labels:
            result[name] = text
    return result


# ----------------------------------------------------------------------------
# The enum and the table
# ----------------------------------------------------------------------------

def parse_enum(instrs_h):
    text = strip_comments(instrs_h)
    m = re.search(r"enum\s+InstructionType\s*\{(.*?)\}", text, re.S)
    if not m:
        raise SystemExit("check_instr_table: no 'enum InstructionType' in " + INSTRS_H)
    names = []
    for part in m.group(1).split(","):
        part = part.strip()
        if not part:
            continue
        mm = re.match(r"(\w+)\s*(=\s*(-?\w+))?$", part)
        if not mm:
            raise SystemExit("check_instr_table: cannot parse enumerator '%s'" % part)
        names.append(mm.group(1))
    return names


ROW_RE = re.compile(
    r'OBJK_ROW\(\s*(\w+)\s*,\s*(\w+)\s*,\s*(\w+)\s*,\s*(\w+)\s*,\s*(\w+)\s*,\s*(\w+)\s*,\s*(\w+)\s*,'
    r'\s*([^,"]*?)\s*,\s*"((?:[^"\\]|\\.)*)"\s*\)')


class Row(object):
    def __init__(self, name, ops, wide, pops, pushes, flags, note):
        self.name = name
        self.ops = ops          # [op1, op2, op3] role names
        self.wide = wide
        self.pops = pops
        self.pushes = pushes
        self.flags = flags      # set of IFLAG_ names without the prefix
        self.note = note

    def has(self, flag):
        return flag in self.flags

    def label_pos(self):
        """The operand slot (0, 1, 2) holding a label reference, or None."""
        for i, r in enumerate(self.ops):
            if r == "LABEL":
                return i
        return None

    def carries_label(self):
        return any(r in ("LABEL", "LABEL_DEF") for r in self.ops)

    def carries_slot(self):
        return "SLOT" in self.ops

    def wire(self):
        """What a file holds after the type byte (and the line, in a debug build):
        [(kind, operand-name)]. kind is Int, Char, Int64, Double or String."""
        out = []
        for i, role in enumerate(self.ops):
            if role in ("NONE", "RUNTIME"):
                continue
            out.append(("Char" if role == "CHAR" else "Int", "operand" + ("" if i == 0 else str(i + 1))))
        if self.wide == "I64":
            out.append(("Int64", "operand7"))
        elif self.wide == "F64":
            out.append(("Double", "operand4"))
        elif self.wide == "STR":
            out.append(("String", "operand5"))
        elif self.wide == "STR2":
            out.append(("String", "operand5"))
            out.append(("String", "operand6"))
        return out


def parse_table(info_h):
    rows = []
    # the table, not the macro's own definition (which has no string literal)
    for m in ROW_RE.finditer(info_h):
        name, o1, o2, o3, wide, pops, pushes, flags, note = m.groups()
        fl = set()
        for tok in re.split(r"\|", flags):
            tok = tok.strip()
            if tok == "0" or not tok:
                continue
            if not tok.startswith("IFLAG_"):
                raise SystemExit("check_instr_table: row %s has the unknown flag token '%s'" % (name, tok))
            fl.add(tok[len("IFLAG_"):])
        rows.append(Row(name, [o1, o2, o3], wide, pops, pushes, fl, note))
    return rows


def defined_flags(info_h):
    return set(re.findall(r"constexpr\s+uint16_t\s+IFLAG_(\w+)\s*=", info_h))


# ----------------------------------------------------------------------------
# The checks
# ----------------------------------------------------------------------------

class Report(object):
    def __init__(self):
        self.errors = []   # (id, message)

    def err(self, eid, msg):
        self.errors.append((eid, msg))


def check_enum_vs_table(enum_names, rows, info_h, rep):
    names = [r.name for r in rows]
    if names != enum_names:
        enum_set, row_set = set(enum_names), set(names)
        for n in enum_names:
            if n not in row_set:
                rep.err("a:missing:" + n, "(a) %s has no row in the table" % n)
        for n in names:
            if n not in enum_set:
                rep.err("a:extra:" + n, "(a) the table has a row for %s, which is not an enumerator" % n)
        if enum_set == row_set:
            for i, (e, r) in enumerate(zip(enum_names, names)):
                if e != r:
                    rep.err("a:order", "(a) row %d is %s but enumerator %d is %s: the table must be in enum order" % (i, r, i, e))
                    break
    # the compile-time guards must still be there
    for needle, what in (("TableMatchesEnum()", "the order assert"),
                         ("kInstrCount == static_cast<size_t>(instructions::END_STMTS) + 1", "the count assert")):
        if needle not in info_h:
            rep.err("a:guard:" + needle, "(a) instr_info.h lost %s (%s)" % (what, needle))
    if not re.search(r"static_assert\(\s*TableMatchesEnum\(\)", info_h):
        rep.err("a:guard:static_assert", "(a) instr_info.h no longer static_asserts TableMatchesEnum()")
    unknown = {f for r in rows for f in r.flags} - defined_flags(info_h)
    for f in sorted(unknown):
        rep.err("a:flag:" + f, "(a) a row uses IFLAG_%s, which is not defined" % f)
    for r in rows:
        if r.pops != "STACK_VAR" and not r.pops.isdigit():
            rep.err("a:pops:" + r.name, "(a) %s: pops '%s' is neither a count nor STACK_VAR" % (r.name, r.pops))
        if r.pushes != "STACK_VAR" and not r.pushes.isdigit():
            rep.err("a:pushes:" + r.name, "(a) %s: pushes '%s' is neither a count nor STACK_VAR" % (r.name, r.pushes))
        if r.has("JUMP") and not r.carries_label():
            rep.err("a:jump:" + r.name, "(a) %s is a jump but no operand is a label" % r.name)
        if r.carries_slot() and r.ops[1] != "CTX":
            rep.err("a:slot:" + r.name, "(a) %s carries a slot but operand2 is not its context" % r.name)


READ_KIND = {"Int": "Int", "Int64": "Int64", "Char": "Char", "Double": "Double"}


def not_in_file(row):
    return row.has("VM_ONLY") or row.has("PSEUDO")


def check_writer(rows, emit_src, rep):
    defs = find_defs(strip_comments(emit_src), "Write")
    body = None
    for params, b in defs:
        if re.search(r"bool\s+is_debug\s*,\s*OutputStream", params) and re.search(r"switch\s*\(\s*type\s*\)", b):
            body = b
            break
    if body is None:
        rep.err("b:writer:locate", "(b) cannot find IntermediateInstruction::Write in " + EMIT_CPP)
        return None
    sw = find_switch(body, r"^\s*type\s*$")
    if sw is None:
        rep.err("b:writer:switch", "(b) IntermediateInstruction::Write has no switch(type)")
        return None
    head = body[:body.find("switch")]
    gm = group_map(case_groups(sw))
    for r in rows:
        if r.name in gm:
            got = [(k, o) for k, o in re.findall(r"\bWrite(Int64|Int|Char|Double|String)\s*\(\s*(?:static_cast<[^>]*>\s*\(\s*|\([^)]*\)\s*)?(operand\d?)\b", gm[r.name])]
        else:
            got = []
        if not_in_file(r):
            if got:
                rep.err("b:writer:" + r.name, "(b) emit.cpp Write writes %s for %s, which the table says is never in a file" % (got, r.name))
            continue
        want = r.wire()
        if got != want:
            rep.err("b:writer:" + r.name, "(b) emit.cpp Write writes %s for %s; the table's roles imply %s" % (got, r.name, want))
    for name in gm:
        if name != "default" and name not in {r.name for r in rows}:
            rep.err("b:writer:unknown:" + name, "(b) emit.cpp Write has a case for %s, which is not an opcode" % name)
    return head


def header_order(text, line_pat, type_pat):
    """'type,line' or 'line,type', by which comes first, or None if either is not found."""
    t = re.search(type_pat, text)
    l = re.search(line_pat, text)
    if not t or not l:
        return None
    return "type,line" if t.start() < l.start() else "line,type"


def check_readers(rows, loader_src, linker_src, writer_head, rep):
    by_name = {r.name: r for r in rows}

    # ---- the instruction header: type byte, then line number ----
    want_order = header_order(writer_head or "", r"\bline_num\b", r"\bWriteByte\s*\(\s*type\b") if writer_head else None

    # ---- .obe loader ----
    ldefs = [(p, b) for p, b in find_defs(strip_comments(loader_src), "LoadStatements")]
    lbody = ldefs[0][1] if ldefs else None
    loader_groups = None
    loader_sw = None
    if lbody is None:
        rep.err("b:loader:locate", "(b) cannot find Loader::LoadStatements in " + LOADER_CPP)
    else:
        loader_sw = find_switch(lbody, r"^\s*type\s*$")
        if loader_sw is None:
            rep.err("b:loader:switch", "(b) Loader::LoadStatements has no switch(type)")
        else:
            loader_groups = group_map(case_groups(loader_sw))
            pre = lbody[:lbody.find("switch")]
            got_order = header_order(pre, r"line_num\s*=\s*[^;]*ReadInt\s*\(", r"\bReadByte\s*\(")
            if want_order and got_order and got_order != want_order:
                rep.err("order:" + LOADER_CPP, "(b) %s reads the header as %s but emit.cpp writes %s" % (LOADER_CPP, got_order, want_order))
    if loader_groups is not None:
        for r in rows:
            text = loader_groups.get(r.name)
            rejected = text is not None and "LoadFailed" in text
            reads = re.findall(r"\bRead(Int64|Int|Char|Double)\s*\(\s*\)", text) if text else []
            if not_in_file(r) or r.has("VM_ONLY") or r.has("LIB_ONLY") or r.has("RESERVED"):
                if text is not None and not rejected:
                    rep.err("b:loader:" + r.name, "(b) the .obe loader has a case for %s, which the table says is %s" %
                            (r.name, "/".join(sorted(f for f in r.flags if f in ("VM_ONLY", "LIB_ONLY", "PSEUDO", "RESERVED")))))
                continue
            if text is None:
                rep.err("b:loader:" + r.name, "(b) the .obe loader has no case for %s: a program containing it is refused" % r.name)
                continue
            if rejected:
                rep.err("b:loader:" + r.name, "(b) the .obe loader rejects %s but the table says it is a normal instruction" % r.name)
                continue
            want = [k for k, _ in r.wire()]
            if reads != want:
                rep.err("b:loader:" + r.name, "(b) the .obe loader reads %s for %s; the table's roles imply %s" % (reads, r.name, want))

    # ---- .obl reader ----
    kdefs = find_defs(strip_comments(linker_src), "LoadStatements")
    kbody = None
    for p, b in kdefs:
        if "LibraryMethod" in p:
            kbody = b
    if kbody is None:
        rep.err("b:linker:locate", "(b) cannot find Library::LoadStatements in " + LINKER_CPP)
        return
    ksw = find_switch(kbody, r"^\s*type\s*$")
    if ksw is None:
        rep.err("b:linker:switch", "(b) Library::LoadStatements has no switch(type)")
        return
    pre = kbody[:kbody.find("switch")]
    got_order = header_order(pre, r"line_num\s*=\s*[^;]*ReadInt\s*\(", r"\bconst\s+int\s+type\s*=\s*ReadByte\s*\(")
    if want_order and got_order and got_order != want_order:
        rep.err("order:" + LINKER_CPP, "(b) %s reads the header as %s but emit.cpp writes %s" % (LINKER_CPP, got_order, want_order))
    kgroups = group_map(case_groups(ksw))
    for r in rows:
        text = kgroups.get(r.name)
        if r.has("VM_ONLY") or r.has("PSEUDO") or r.has("RESERVED"):
            if text is not None:
                rep.err("b:linker:" + r.name, "(b) the .obl reader has a case for %s, which the table says is %s" %
                        (r.name, "/".join(sorted(f for f in r.flags if f in ("VM_ONLY", "PSEUDO", "RESERVED")))))
            continue
        if text is None:
            rep.err("b:linker:" + r.name, "(b) the .obl reader has no case for %s: a library containing it fails to link" % r.name)
            continue
        reads = []
        for m in re.finditer(r"\bRead(Int64|Int|Char|Double)\s*\(\s*\)|\b(operand_string)\s*\(\s*\)", text):
            reads.append(m.group(1) or "String")
        want = [k for k, _ in r.wire()]
        if reads != want:
            rep.err("b:linker:" + r.name, "(b) the .obl reader reads %s for %s; the table's roles imply %s" % (reads, r.name, want))

    # ---- (d) the loader's target check ----
    if loader_groups is not None:
        for r in rows:
            if r.has("JUMP") and not not_in_file(r):
                text = loader_groups.get(r.name, "")
                if "CheckTarget" not in text:
                    rep.err("d:" + r.name, "(d) the .obe loader does not CheckTarget the label of %s: a crafted program could jump outside the method" % r.name)


def acc_name(pos):
    return "GetOperand" if pos == 0 else "GetOperand%d" % (pos + 1)


def check_label_pass(rows, name, sw, rep, exempt=None, must_not_have=None):
    """A switch that lists opcodes by `case`: each label-carrying opcode must be there and
    read its label from the operand the table says. exempt: {opcode: reason} that may be
    absent (and must still be needed)."""
    gm = group_map(case_groups(sw))
    for r in rows:
        if not r.carries_label() or r.has("VM_ONLY") or r.has("PSEUDO"):
            continue
        if r.name not in gm:
            if exempt and r.name in exempt:
                continue
            rep.err("c:%s:%s" % (name, r.name), "(c) %s has no case for %s, which carries a label" % (name, r.name))
            continue
        pos = r.label_pos() if r.label_pos() is not None else r.ops.index("LABEL_DEF")
        if group_labels_single_position(rows, sw, r.name) and acc_name(pos) + "(" not in gm[r.name]:
            rep.err("c:%s:%s:operand" % (name, r.name),
                    "(c) %s handles %s without %s(), but the table says its label is in operand%s" %
                    (name, r.name, acc_name(pos), "" if pos == 0 else str(pos + 1)))
    if must_not_have:
        by_name = {r.name: r for r in rows}
        for nm in must_not_have:
            rr = by_name.get(nm)
            if rr is not None and not rr.carries_label():
                rep.err("c:%s:extra:%s" % (name, nm), "(c) %s handles %s as a label but the table says it carries none" % (name, nm))
    if exempt:
        for nm in exempt:
            if nm in gm:
                rep.err("c:%s:stale:%s" % (name, nm), "(c) %s now handles %s: remove the exemption" % (name, nm))


def group_labels_single_position(rows, sw, opname):
    """True when every opcode sharing opname's case group has its label in the same operand,
    so one accessor check is meaningful."""
    by_name = {r.name: r for r in rows}
    for labels, text in case_groups(sw):
        if opname in labels:
            positions = set()
            for l in labels:
                rr = by_name.get(l)
                if rr is not None and rr.label_pos() is not None:
                    positions.add(rr.label_pos())
                elif rr is not None and "LABEL_DEF" in rr.ops:
                    positions.add(rr.ops.index("LABEL_DEF"))
            return len(positions) == 1
    return False


def mentions(body, name):
    return re.search(r"\b" + re.escape(name) + r"\b", body) is not None


def check_optimizer(rows, compiler_srcs, rep):
    stripped = {p: strip_comments(s) for p, s in compiler_srcs.items()}

    def locate(fname, pred):
        found = []
        for path, text in stripped.items():
            for params, body in find_defs(text, fname):
                if pred(params):
                    found.append((path, body))
        return found

    def one(fname, pred, what):
        f = locate(fname, pred)
        if not f:
            rep.err("c:locate:" + what, "(c) cannot find %s in core/compiler/*.cpp: if the pass was renamed or moved, update the checker" % what)
            return None
        return f[0][1]

    block = lambda p: "IntermediateBlock" in p and "vector" not in p
    refs = [r for r in rows if r.carries_label() and not r.has("VM_ONLY") and not r.has("PSEUDO")]

    # ---- MaxLabel: a pure label pass, listed by case ----
    body = one("MaxLabel", lambda p: True, "MaxLabel")
    if body is not None:
        sw = find_switch(body, r"GetType")
        if sw is None:
            rep.err("c:MaxLabel:switch", "(c) MaxLabel has no switch on the instruction type")
        else:
            check_label_pass(rows, "MaxLabel", sw, rep, must_not_have=[n for labs, _ in case_groups(sw) for n in labs if n != "default"])

    # ---- CleanLabelsLocation (the per-block one): rewrites jumps by name ----
    body = one("CleanLabelsLocation", block, "CleanLabelsLocation")
    if body is not None:
        for r in refs:
            if not mentions(body, r.name):
                rep.err("c:CleanLabelsLocation:" + r.name, "(c) CleanLabelsLocation never mentions %s, which carries a label" % r.name)
            else:
                pos = r.label_pos()
                # JMP_TABLE keeps its label in operand3: the rewrite must use it
                if pos is not None and pos > 0 and acc_name(pos) + "(" not in body:
                    rep.err("c:CleanLabelsLocation:%s:operand" % r.name, "(c) CleanLabelsLocation never reads %s(), where %s keeps its label" % (acc_name(pos), r.name))

    # ---- JumpToLocation (the per-block one): label id -> instruction index ----
    body = one("JumpToLocation", block, "JumpToLocation")
    if body is not None:
        for r in refs:
            if not mentions(body, r.name):
                rep.err("c:JumpToLocation:" + r.name, "(c) JumpToLocation never mentions %s, which carries a label" % r.name)
        sw_all = [find_switch(body, r"GetType")]
        # every switch in the body, since it has two (definitions, then references)
        text_all = body
        for r in refs:
            pos = r.label_pos()
            if pos is not None and pos > 0 and acc_name(pos) + "(" not in text_all:
                rep.err("c:JumpToLocation:%s:operand" % r.name, "(c) JumpToLocation never reads %s(), where %s keeps its label" % (acc_name(pos), r.name))

    # ---- the inliner ----
    body = one("InlineMethod", block, "InlineMethod")
    cbody = one("CanInlineMethod", lambda p: True, "CanInlineMethod")
    rejected = set()
    cswitch = None
    if cbody is not None:
        cswitch = find_switch(cbody, r"GetType")
        if cswitch is None:
            rep.err("c:CanInlineMethod:switch", "(c) CanInlineMethod has no switch on the callee's instruction types")
        else:
            for labels, text in case_groups(cswitch):
                if re.fullmatch(r"\s*return\s+false\s*;\s*", text):
                    rejected.update(labels)

    if body is not None:
        prescan = find_switch(body, r"GetType")
        shift = find_switch(body, r"mthd_called_instr")
        if prescan is None:
            rep.err("c:InlineMethod:prescan", "(c) InlineMethod's label-collision scan (a switch over the caller's instructions) is gone")
        else:
            exempt = {k: v for k, v in LABEL_PRESCAN_EXEMPT.items()}
            check_label_pass(rows, "InlineMethod's label scan", prescan, rep, exempt=exempt)
        if shift is None:
            rep.err("c:InlineMethod:shift", "(c) InlineMethod's shifting copy (a switch over the callee's instructions) is gone")
        else:
            sg = group_map(case_groups(shift))
            for r in rows:
                if r.has("VM_ONLY") or r.has("PSEUDO"):
                    continue
                needs = r.carries_label() or r.carries_slot()
                if not needs:
                    continue
                kind = "label" if r.carries_label() else "slot"
                if r.name in rejected:
                    continue  # CanInlineMethod refuses a callee with it, so it is never copied
                if r.name not in sg:
                    rep.err("c:InlineMethod:shift:" + r.name,
                            "(c) the inliner copies %s unshifted (its default case) although it carries a %s, and CanInlineMethod does not refuse it" % (r.name, kind))
                    continue
                if kind == "label":
                    pos = r.label_pos()
                    if pos is not None and group_labels_single_position(rows, shift, r.name):
                        if acc_name(pos) + "(" not in sg[r.name]:
                            rep.err("c:InlineMethod:shift:%s:operand" % r.name,
                                    "(c) the inliner shifts %s without reading %s(), where the table says its label is" % (r.name, acc_name(pos)))
            # an opcode that is refused must not ALSO be shifted by accident of a stale entry
            for name in rejected:
                if name in sg:
                    rep.err("c:InlineMethod:shift:redundant:" + name, "(c) the inliner shifts %s and CanInlineMethod refuses it: one of the two is stale" % name)

    # CanInlineMethod's collision check: label ops that are not refused must be checked
    if cswitch is not None:
        cg = group_map(case_groups(cswitch))
        for r in refs:
            if r.name in rejected:
                continue
            text = cg.get(r.name)
            if text is None or "lbl_jmp_offsets" not in text:
                if r.name == "LBL" or text is None or "lbl_jmp_offsets" not in text:
                    rep.err("c:CanInlineMethod:collision:" + r.name,
                            "(c) CanInlineMethod does not check the shifted label of %s against the caller's labels" % r.name)
            else:
                pos = r.label_pos() if r.label_pos() is not None else (r.ops.index("LABEL_DEF") if "LABEL_DEF" in r.ops else 0)
                if group_labels_single_position(rows, cswitch, r.name) and acc_name(pos) + "(" not in text:
                    rep.err("c:CanInlineMethod:collision:%s:operand" % r.name,
                            "(c) CanInlineMethod checks %s without reading %s(), where the table says its label is" % (r.name, acc_name(pos)))
        # a non-local variable access refuses the inline: every slot opcode must be in that group
        slot_group = None
        for labels, text in case_groups(cswitch):
            if "LOCL" in text and "return false" in text:
                slot_group = set(labels)
        for r in rows:
            if r.carries_slot() and not r.has("VM_ONLY") and not (slot_group and r.name in slot_group):
                if r.name not in rejected:
                    rep.err("c:CanInlineMethod:slot:" + r.name, "(c) CanInlineMethod does not refuse a non-local %s, which carries a slot" % r.name)


def check_library_resolution(rows, linker_src, rep):
    text = strip_comments(linker_src)
    defs = find_defs(text, "ResolveExternalMethodCalls")
    if not defs:
        rep.err("e:locate", "(e) cannot find Linker::ResolveExternalMethodCalls in " + LINKER_CPP)
        return
    body = defs[0][1]
    sw = find_switch(body, r"GetType")
    gm = group_map(case_groups(sw)) if sw else {}
    for r in rows:
        if r.has("LIB_ONLY") and r.name not in gm:
            rep.err("e:" + r.name, "(e) the linker never resolves %s, which only a .obl carries: it would reach the program unresolved" % r.name)
    for name in gm:
        rr = next((x for x in rows if x.name == name), None)
        if rr is not None and not rr.has("LIB_ONLY") and name != "default":
            rep.err("e:extra:" + name, "(e) the linker resolves %s but the table does not mark it library-only" % name)


def check_dispatch(enum_names, dispatch_src, rep):
    text = strip_comments(dispatch_src)
    m = re.search(r"instr_dispatch\s*\[\s*\]\s*=\s*\{(.*?)\n\};", text, re.S)
    if not m:
        rep.err("f:locate", "(f) cannot find the instr_dispatch table in " + DISPATCH_CPP)
        return
    got = re.findall(r"\bHandle_(\w+)\s*,?", m.group(1))
    if got != enum_names:
        for i, (g, e) in enumerate(zip(got, enum_names)):
            if g != e:
                rep.err("f:order", "(f) dispatch.cpp's handler %d is Handle_%s but opcode %d is %s" % (i, g, i, e))
                return
        rep.err("f:count", "(f) dispatch.cpp lists %d handlers for %d opcodes" % (len(got), len(enum_names)))


def run_checks(src):
    """src: {path: text}. Returns the Report."""
    rep = Report()
    enum_names = parse_enum(src[INSTRS_H])
    rows = parse_table(src[INFO_H])
    check_enum_vs_table(enum_names, rows, src[INFO_H], rep)
    if any(e[0].startswith("a:missing") or e[0].startswith("a:extra") or e[0] == "a:order" for e in rep.errors):
        return rep  # the per-opcode checks assume a table in enum order
    head = check_writer(rows, src[EMIT_CPP], rep)
    check_readers(rows, src[LOADER_CPP], src[LINKER_CPP], head, rep)
    compiler_srcs = {p: s for p, s in src.items() if p.startswith("core/compiler/") and p.endswith(".cpp")}
    check_optimizer(rows, compiler_srcs, rep)
    check_library_resolution(rows, src[LINKER_CPP], rep)
    check_dispatch(enum_names, src[DISPATCH_CPP], rep)
    return rep


def load_sources():
    src = {}
    for rel in (INSTRS_H, INFO_H, EMIT_CPP, LINKER_CPP, LOADER_CPP, DISPATCH_CPP):
        src[rel] = read(rel)
    for path in sorted(glob.glob(os.path.join(ROOT, COMPILER_GLOB))):
        rel = os.path.relpath(path, ROOT).replace(os.sep, "/")
        if rel not in src:
            src[rel] = read(rel)
    return src


def report(rep, quiet=False):
    """Prints, and returns the number of failures."""
    failures = 0
    seen_known = set()
    for eid, msg in rep.errors:
        if eid in KNOWN:
            seen_known.add(eid)
            if not quiet:
                print("known:   " + msg)
                print("         (" + KNOWN[eid] + ")")
            continue
        failures += 1
        if not quiet:
            print("FAIL:    " + msg)
    if not quiet:
        for eid in sorted(set(KNOWN) - seen_known):
            print("warning: KNOWN entry '%s' no longer occurs: remove it from check_instr_table.py" % eid)
    return failures


# ----------------------------------------------------------------------------
# Negative controls
# ----------------------------------------------------------------------------

def mutate(src, rel, old, new):
    """Replace one literal occurrence; the target vanishing is itself a test failure."""
    text = src[rel]
    if old not in text:
        raise MutationGone("%s: %r" % (rel, old))
    out = dict(src)
    out[rel] = text.replace(old, new, 1)
    return out


def mutate_re(src, rel, pattern, repl):
    text = src[rel]
    new, n = re.subn(pattern, repl, text, count=1, flags=re.S)
    if n != 1:
        raise MutationGone("%s: /%s/" % (rel, pattern))
    out = dict(src)
    out[rel] = new
    return out


class MutationGone(Exception):
    pass


OPT_CPP = "core/compiler/optimization.cpp"


def self_test():
    src = load_sources()
    base = run_checks(src)
    base_ids = {e[0] for e in base.errors}
    problems = []

    def row_line(name):
        m = re.search(r"^.*OBJK_ROW\(%s,.*$" % name, src[INFO_H], re.M)
        return m.group(0)

    def swap_rows():
        a, b = row_line("ADD_INT"), row_line("SUB_INT")
        t = mutate(src, INFO_H, a, "@@A@@")
        t = mutate(t, INFO_H, b, a)
        return mutate(t, INFO_H, "@@A@@", b)

    def swap_handlers():
        text = src[DISPATCH_CPP]
        i = text.index("instr_dispatch[]")
        head, tail = text[:i], text[i:]
        tail = tail.replace("Handle_ADD_INT,", "@@A@@", 1).replace("Handle_SUB_INT,", "Handle_ADD_INT,", 1).replace("@@A@@", "Handle_SUB_INT,", 1)
        out = dict(src)
        out[DISPATCH_CPP] = head + tail
        return out

    ld = row_line("LOAD_INT_VAR")
    # (the id the mutation must add, what it breaks, a function that makes the mutated sources)
    cases = [
        ("a:missing:TRY_END", "a row removed",
         lambda: mutate(src, INFO_H, row_line("TRY_END") + "\n", "")),
        ("a:order", "two rows swapped", swap_rows),
        ("a:guard:static_assert", "the static_assert removed",
         lambda: mutate(src, INFO_H, "static_assert(TableMatchesEnum(),", "static_assert(true,")),
        ("b:writer:LOAD_INT_VAR", "a wrong row (operand2 dropped)",
         lambda: mutate(src, INFO_H, ld, re.sub(r"SLOT,\s*CTX,", "SLOT, NONE,", ld, count=1))),
        ("b:loader:JMP", "the loader reads one operand fewer",
         lambda: mutate_re(src, LOADER_CPP, r"const long cond = ReadInt\(\);(\s*mthd_instrs\[i\] = StackInstr\(line_num, JMP,)", r"const long cond = 0;\1")),
        ("b:linker:LOAD_INT_LIT", "the .obl reader reads a 32-bit literal",
         lambda: mutate_re(src, LINKER_CPP, r"emit\(line_num, LOAD_INT_LIT, ReadInt64\(\)\);", "emit(line_num, LOAD_INT_LIT, ReadInt());")),
        ("b:writer:JMP_TABLE", "the writer drops JMP_TABLE's default label",
         lambda: mutate_re(src, EMIT_CPP, r"(case JMP_TABLE:\s*WriteInt\([^\n]*\n\s*WriteInt\([^\n]*\n)\s*WriteInt\([^\n]*operand3[^\n]*\n", r"\1")),
        ("c:MaxLabel:JMP_TABLE_SLOT", "MaxLabel forgets JMP_TABLE_SLOT",
         lambda: mutate_re(src, OPT_CPP, r"(MaxLabel\s*\([^)]*\)\s*\{.*?)case JMP_TABLE_SLOT:", r"\1")),
        ("c:InlineMethod:shift:JMP_TABLE_SLOT", "the inliner copies JMP_TABLE_SLOT unshifted",
         lambda: mutate_re(src, OPT_CPP, r"case JMP_TABLE_SLOT:(\s*outputs->AddInstruction)", r"case NEG_INF_FLOAT:\1")),
        ("c:InlineMethod:shift:LOAD_FUNC_VAR", "the inliner does not shift a func-ref slot",
         lambda: mutate_re(src, OPT_CPP, r"case LOAD_FUNC_VAR:(\s*case STOR_FUNC_VAR:\s*case COPY_FUNC_VAR:\s*#ifdef _DEBUG)", r"\1")),
        ("c:InlineMethod:shift:TRY_START", "a callee with a TRY_START is no longer refused, and the shift does not know it",
         lambda: mutate_re(src, OPT_CPP, r"case instructions::TRY_START:(\s*case instructions::TRY_END:)", r"\1")),
        ("c:InlineMethod:shift:JMP_TABLE:operand", "the inliner shifts JMP_TABLE's label through operand, not operand3",
         lambda: mutate_re(src, OPT_CPP, r"mthd_called_instr->GetOperand3\(\) \+ jump_inline_offset\)\);(\s*break;\s*case JMP_TABLE_SLOT)", r"mthd_called_instr->GetOperand() + jump_inline_offset));\1")),
        ("d:JMP_TABLE_SLOT", "the loader stops checking a table slot's target",
         lambda: mutate_re(src, LOADER_CPP, r"(target_ip\s*=\s*)CheckTarget\(method, ip, (ReadInt\(\)), count\)", r"\1\2")),
        ("e:LIB_FUNC_DEF", "the linker stops resolving LIB_FUNC_DEF",
         lambda: mutate_re(src, LINKER_CPP, r"case instructions::LIB_FUNC_DEF:(\s*\{\s*LibraryClass\* lib_klass)", r"case instructions::NEW_FUNC_INST:\1")),
        ("f:order", "two handlers swapped in the dispatch table", swap_handlers),
    ]

    for expect, what, make in cases:
        if expect in base_ids:
            problems.append("%s: the unmutated tree already reports %s, so the control proves nothing" % (what, expect))
            continue
        try:
            mutated = make()
        except MutationGone as gone:
            problems.append("%s: the text to break has moved (%s): update the control" % (what, gone))
            continue
        got = {e[0] for e in run_checks(mutated).errors}
        if expect not in got:
            problems.append("%s: expected %s, got %s" % (what, expect, sorted(got - base_ids) or "nothing new"))
        else:
            print("ok: caught: " + what)

    if problems:
        for p in problems:
            print("SELF-TEST FAIL: " + p)
        return 1
    print("check_instr_table --test: all %d mutations were caught" % len(cases))
    return 0


def main(argv):
    if "--test" in argv:
        return self_test()
    src = load_sources()
    rep = run_checks(src)
    failures = report(rep)
    rows = len(parse_table(src[INFO_H]))
    if failures:
        print("check_instr_table: %d problem(s) across %d opcodes" % (failures, rows))
        return 1
    print("check_instr_table: %d opcodes: the table agrees with the writer, both readers, the label and slot passes, the inliner, the loader's target check and the dispatch table" % rows)
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
