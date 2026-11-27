#!/usr/bin/env python3
"""Writes core/shared/trap_signatures.h: what each trap takes and returns.

A trap is a call into the VM: the program pushes the arguments and the trap's id,
and the handler pops what its own id says. Nothing in an image says what that is,
so the loader could not check it. This table is the missing declaration, one row
per trap id, and it is read from the two places that already agree on it:

  * IntermediateEmitter::EmitSystemDirective (tools/cicd/trap_shapes.py): the
    instruction (TRAP or TRAP_RTRN) and the count the compiler writes;
  * the libraries themselves: a directive is a statement of a library method, so
    the method's own signature types the words (`Print(c : Char[])` is
    LOAD_INT_VAR 0; LOAD_INT_LIT STD_OUT_STRING; TRAP 2, and its name spells c*).
    Every method of every image given is read, and the words a trap is given
    are taken where they are parameters or the instance, unchanged.

    python3 gen_trap_signatures.py <dir of .obe> [<dir> ...]     # rewrite the header
    python3 gen_trap_signatures.py --check                        # header against the compiler
    python3 gen_trap_signatures.py --observed <file>              # header against a run

The header is written only from a corpus that can describe every trap: the run
stops, non-zero and with the committed header untouched, when no image was read
(a misspelled or empty directory) or when a trap whose handler reads its caller's
frame or instance was seen in no library method (its row would list no method it
may stand in, and a VM built from it refuses every valid program that uses it:
System.Base:GetClass is LOAD_CLS_BY_INST). --check holds the committed header to
the same rule.

The images are the whole-library programs tools/fuzz/adhoc/obe_corpus_gate.py
compiles (<out>/cover) and the compiled tree (<out>/img). --check needs no build:
it fails when a row's instruction or count is not what the compiler writes, when
the compiler writes a trap that has no row (the array-literal copies included), or
when a row does not say where a frame-reading trap may stand. --observed reads lines of
"<trap id> <words the operand stack moved by, the id included> <times>", what a
VM built to print them saw while the regression suite ran, and lists each trap
whose row says otherwise (2026-11: 192 traps ran, 190 as the libraries imply; the
two others are OVERRIDES below).

The library sources are the authority for two things a corpus can get wrong
without looking wrong (library_traps): every directive is a statement of one
library method, so core/compiler/lib_src says which methods hold each trap. A
corpus that lacks one of them (System.Time.Date:New:l, of SYS_TIME's two) used
to produce a row with a shorter, non-empty list, which --check accepted and the
loader then held valid programs to. The generator stops unless the contexts it
found are exactly the methods the sources hold the directive in, and --check
compares the committed rows with the sources the same way.
"""
import collections
import glob
import os
import re
import struct
import sys
import zlib

HERE = os.path.dirname(os.path.abspath(__file__))
TREE = os.path.normpath(os.path.join(HERE, "..", ".."))
sys.path.insert(0, HERE)
sys.path.insert(0, os.path.join(TREE, "tools", "fuzz", "loader"))
import trap_shapes  # noqa: E402

HEADER = os.path.join(TREE, "core", "shared", "trap_signatures.h")
LIB_SRC = os.path.join(TREE, "core", "compiler", "lib_src")
# a directive the compiler writes as another trap (EmitSystemDirective): source name -> trap
DIRECTIVE_TRAP = {"LOAD_INST_UID": "INST_STABLE_ID"}
W_INT, W_FLOAT, W_BYTE_ARY, W_CHAR_ARY, W_INT_ARY, W_FLOAT_ARY, W_OBJ_ARY, W_OBJ = 1, 2, 4, 8, 16, 32, 64, 128
W_ANY = 255
WORD_NAMES = [(W_INT, "W_INT"), (W_FLOAT, "W_FLOAT"), (W_BYTE_ARY, "W_BYTE_ARY"), (W_CHAR_ARY, "W_CHAR_ARY"),
              (W_INT_ARY, "W_INT_ARY"), (W_FLOAT_ARY, "W_FLOAT_ARY"), (W_OBJ_ARY, "W_OBJ_ARY"), (W_OBJ, "W_OBJ")]
DCLR = {-1500: W_INT, -1499: W_INT, -1498: W_FLOAT, -1497: W_BYTE_ARY, -1496: W_CHAR_ARY, -1495: W_INT_ARY,
        -1494: W_FLOAT_ARY, -1493: W_OBJ, -1492: W_OBJ_ARY}
NEW_ARY = {"NEW_BYTE_ARY": W_BYTE_ARY, "NEW_CHAR_ARY": W_CHAR_ARY, "NEW_INT_ARY": W_INT_ARY,
           "NEW_FLOAT_ARY": W_FLOAT_ARY, "NEW_OBJ_ARY": W_OBJ_ARY}
LOCL = -3498
MAX_ARGS = 8
# literal arrays: written by the compiler outside EmitSystemDirective and checked
# whole by Loader::CheckLiteralCopy; the row types the array and its result
# Where the handler does not do what the library method around the directive
# suggests. Each was found by running the regression suite on a VM that printed
# every trap's effect on the operand stack (--observed), and read in the handler:
#   FILE_TEMP_NAME    File->GetTempName() has no parameter, so the word the directive
#                     pushes is not one, and the String the handler leaves is the
#                     method's result (TrapProcessor::FileTempName)
#   LOAD_CLS_BY_INST  written TRAP_RTRN, but the handler stores the Class object in
#                     the method's first local and pushes nothing (LoadClsByInst)
# A "contexts" entry (a list of method names) names where a frame-reading trap may
# stand when no library method in the corpus holds it; none is needed today.
OVERRIDES = {
    "FILE_TEMP_NAME": {"pushes": 1, "result": (W_OBJ, {"System.String"}, False)},
    "LOAD_CLS_BY_INST": {"pushes": 0},
}
LITERAL_COPIES = {"CPY_CHAR_STR_ARY": W_CHAR_ARY, "CPY_INT_STR_ARY": W_INT_ARY, "CPY_BOOL_STR_ARY": W_BYTE_ARY,
                  "CPY_BYTE_STR_ARY": W_BYTE_ARY, "CPY_FLOAT_STR_ARY": W_FLOAT_ARY}


def enum_values(path, enum):
    with open(path, encoding="utf-8", errors="replace") as f:
        text = f.read()
    body = text[text.index("enum " + enum):]
    body = re.sub(r"//[^\n]*", "", body[body.index("{") + 1:body.index("}")])
    out, value = {}, 0
    for item in body.split(","):
        item = item.strip()
        if not item:
            continue
        if "=" in item:
            name, num = item.split("=")
            value = int(num.strip(), 0)
            item = name.strip()
        out[item] = value
        value += 1
    return out


def trap_ids():
    ids = enum_values(os.path.join(TREE, "core", "shared", "traps.h"), "Traps")
    instrs = enum_values(os.path.join(TREE, "core", "shared", "instrs.h"), "InstructionType")
    for name in ("SET_SIGNAL", "RAISE_SIGNAL"):
        ids[name] = instrs[name]
    return ids


def frame_traps():
    """Traps whose handler reads the calling frame or its instance rather than the
    operand stack: where they stand is part of what they take."""
    with open(os.path.join(TREE, "core", "vm", "common.cpp"), encoding="utf-8", errors="replace") as f:
        text = f.read()
    switch = text[text.index("bool TrapProcessor::ProcessTrap("):]
    switch = switch[:switch.index("\n}\n")]
    handler_of = dict(re.findall(r"case ([A-Z0-9_]+):\s*\n\s*return (\w+)\(", switch))
    bodies = {}
    for m in re.finditer(r"\nbool TrapProcessor::(\w+)\(StackProgram\* program, size_t\* inst,[^)]*\)\s*\{", text):
        bodies[m.group(1)] = text[m.end():text.index("\n}\n", m.end())]
    out = {}
    for trap, handler in handler_of.items():
        body = bodies.get(handler, "")
        if re.search(r"\bframe\b", body) or re.search(r"\binst\b", body):
            # True where it is the instance the handler reads, not only the frame
            out[trap] = bool(re.search(r"\binst\b", body))
    return out


def code_tokens(text):
    """An Objeck source as (word | '{' | '}' | '(' | ')' | '~' | ';'), without its
    comments, strings and character literals: a brace in a string is not a block."""
    out = []
    i, n = 0, len(text)
    while i < n:
        ch = text[i]
        if ch == "#":
            if text.startswith("#~", i):
                end = text.find("~#", i + 2)
                i = n if end < 0 else end + 2
            else:
                end = text.find("\n", i)
                i = n if end < 0 else end
        elif ch == '"':
            i += 1
            while i < n and text[i] != '"':
                i += 2 if text[i] == "\\" else 1
            i += 1
        elif ch == "'":
            # 'x', or an escape up to its closing quote
            end = i + 2 if text[i + 1:i + 2] != "\\" else text.find("'", i + 2)
            i = (end if end > 0 else i) + 1
        elif ch.isalpha() or ch in "_@":
            end = i + 1
            while end < n and (text[end].isalnum() or text[end] in "_."):
                end += 1
            out.append(text[i:end])
            i = end
        else:
            if ch in "{}();~":
                out.append(ch)
            i += 1
    return out


def library_traps(traps, lib_src=None):
    """Where the library sources hold each directive: trap -> [(Bundle.Class:Method,
    tail)], one for each statement. 'tail' is True where the directive is the last
    statement of a method that returns a value, which is where a TRAP leaves that
    value on the stack for the method's RTRN.

    A directive is written only in a library (the compiler reads one nowhere else),
    so this list is complete where a corpus of images may not be. Checked against
    the committed table when written: all 29 frame-reading traps' contexts and all
    TRAP result counts agree with it."""
    out = collections.defaultdict(list)
    for path in sorted(glob.glob(os.path.join(lib_src or LIB_SRC, "*.obs"))):
        with open(path, encoding="utf-8", errors="replace") as f:
            toks = code_tokens(f.read())
        depth = 0
        bundle = []        # (name, the depth of its body), innermost last
        classes = []
        method = None      # (name, returns a value, the depth of its body)
        pending = None     # a declaration read, waiting for its '{'
        k = 0
        while k < len(toks):
            tok = toks[k]
            if tok == "{":
                depth += 1
                if pending:
                    kind, name, value = pending
                    if kind == "bundle":
                        bundle.append((name, depth))
                    elif kind == "class":
                        classes.append((name, depth))
                    else:
                        method = (name, value, depth)
                    pending = None
            elif tok == "}":
                if method and method[2] == depth:
                    method = None
                if classes and classes[-1][1] == depth:
                    classes.pop()
                if bundle and bundle[-1][1] == depth:
                    bundle.pop()
                depth -= 1
            elif tok == "bundle" and k + 1 < len(toks):
                pending = ("bundle", toks[k + 1], None)
            elif tok in ("class", "interface") and not method:
                # 'class Name', 'class : public : Name'
                j = k + 1
                while j < len(toks) and toks[j] in ("public", "private", "native"):
                    j += 1
                if j < len(toks) and toks[j][0].isalpha():
                    pending = ("class", toks[j], None)
            elif tok in ("method", "function", "New") and not method and classes and classes[-1][1] == depth:
                # 'method : public : Name(...) ~ Type {' or 'New(...) {'
                j = k
                while j < len(toks) and toks[j] != "(":
                    j += 1
                name = "New" if tok == "New" else toks[j - 1]
                level = 0
                while j < len(toks):
                    level += toks[j] == "("
                    level -= toks[j] == ")"
                    if level == 0:
                        break
                    j += 1
                value = tok != "New" and toks[j + 1:j + 2] == ["~"] and toks[j + 2:j + 3] != ["Nil"]
                # a virtual or interface declaration has no body: it ends with ';'
                m = j + 1
                while m < len(toks) and toks[m] not in ("{", ";"):
                    m += 1
                if toks[m:m + 1] == ["{"]:
                    pending = ("method", name, value)
                    k = m - 1
            elif method and DIRECTIVE_TRAP.get(tok, tok) in traps and toks[k + 1:k + 2] == [";"]:
                tail = bool(method[1]) and depth == method[2] and toks[k + 2:k + 3] == ["}"]
                where = "%s.%s:%s" % (bundle[-1][0] if bundle else "", classes[-1][0], method[0])
                out[DIRECTIVE_TRAP.get(tok, tok)].append((where, tail))
            k += 1
    return out


def context_method(context):
    """'System.Time.Date:New:l,' -> 'System.Time.Date:New', as library_traps names it."""
    return ":".join(context.split(":")[:2])


def context_problems(trap, contexts, library):
    """What is wrong with a frame-reading trap's contexts, held to the methods the
    library sources hold its directive in ('' when nothing is)."""
    want = collections.Counter(where for where, _ in library.get(trap, []))
    have = collections.Counter(context_method(c) for c in contexts)
    missing = sorted((want - have).elements())
    extra = sorted((have - want).elements())
    said = []
    if missing:
        said.append("no context for %s, which the library sources hold it in (a corpus without that method's image?)" % ", ".join(missing))
    if extra:
        said.append("a context in %s, where the library sources do not hold it" % ", ".join(extra))
    return "; ".join(said)


def split_params(name):
    sig = name.split(":", 2)[2] if name.count(":") >= 2 else ""
    out, depth, cur = [], 0, ""
    for ch in sig:
        depth += ch == "("
        depth -= ch == ")"
        if ch == "," and depth == 0:
            out.append(cur)
            cur = ""
        else:
            cur += ch
    return out


def class_of(encoded):
    """'o.System.String' -> 'System.String'; None unless the name is qualified: a
    bare name may be a generic's parameter or an enum."""
    if not encoded.startswith("o.") or encoded.endswith("*"):
        return None
    name = encoded[2:].split("<")[0]
    return name if "." in name and "#" not in name else None


def words_of_type(encoded):
    if not encoded or encoded[0] in "nm":
        return 0, None
    stars = len(encoded) - len(encoded.rstrip("*"))
    base = encoded[0]
    if stars:
        if stars > 1:
            # Int[,] is one array of Ints; Int[][] an array of arrays: spelled alike
            return (W_INT_ARY | W_OBJ_ARY | {"b": W_BYTE_ARY, "l": W_BYTE_ARY, "c": W_CHAR_ARY, "f": W_FLOAT_ARY}.get(base, 0)), None
        return {"b": W_BYTE_ARY, "l": W_BYTE_ARY, "c": W_CHAR_ARY, "i": W_INT_ARY, "f": W_FLOAT_ARY, "o": W_OBJ_ARY}.get(base, W_ANY), None
    if base in "ilcb":
        return W_INT, None
    if base == "f":
        return W_FLOAT, None
    if base == "o":
        cls = class_of(encoded)
        # an enum is an Int spelled as an object
        return (W_OBJ, cls) if cls else (W_OBJ | W_INT, None)
    return W_ANY, None


class Seen:
    def __init__(self):
        self.ops = collections.Counter()
        self.args = None            # per word: [mask, classes, True when one is unnamed]
        self.pushes = set()
        self.result = [0, set(), False]
        self.contexts = set()
        self.direct = 0


def census(dirs, shapes, ids):
    import obe_image as oi
    names = {v: k for k, v in ids.items()}
    seen = collections.defaultdict(Seen)
    files = []
    parsed = 0
    for d in dirs:
        files += sorted(glob.glob(os.path.join(d, "*.obe")))
    for path in files:
        try:
            img = zlib.decompress(open(path, "rb").read()[4:])
            classes = oi.parse(img)[1]
        except Exception:
            continue
        parsed += 1
        for c in classes:
            if c["debug"]:
                continue
            for (name, start, _) in c["methods"]:
                d, code = oi.method_code(img, start, c["debug"])
                if not any(x[0] in (oi.OP["TRAP"], oi.OP["TRAP_RTRN"]) for x in code):
                    continue
                r = oi.Reader(img, start)
                r.int()
                flags = img[r.pos:r.pos + 3]
                r.skip(3)
                r.string()
                rtrn = r.string()
                params = r.int()
                n = struct.unpack_from("<i", img, d)[0]
                dclrs = struct.unpack_from("<%di" % n, img, d + 4)
                ptypes = split_params(name)

                def operands(i):
                    op, _, at = code[i]
                    k = oi.OPERAND_INTS.get(op, 0)
                    if k:
                        return struct.unpack_from("<%di" % k, img, at)
                    return struct.unpack_from("<q", img, at) if op == oi.OP["LOAD_INT_LIT"] else ()

                for i, (op, _, _) in enumerate(code):
                    opname = oi.NAMES.get(op)
                    if opname not in ("TRAP", "TRAP_RTRN") or i < 1 or code[i - 1][0] != oi.OP["LOAD_INT_LIT"]:
                        continue
                    trap = names.get(operands(i - 1)[0])
                    if trap is None:
                        continue
                    rec = seen[trap]
                    rec.ops[(opname, operands(i)[0])] += 1
                    rec.contexts.add(name)
                    shape = shapes.get(trap)
                    # the and/or slot shifts the locals: such a method is not a wrapper
                    if not shape or flags[1]:
                        continue
                    k, words, ok = i - 2, [], True
                    for a in reversed(shape["args"]):
                        if k < 0:
                            ok = False
                            break
                        o, od = oi.NAMES.get(code[k][0]), operands(k)
                        if a[0] in ("local", "flocal") and o == ("LOAD_INT_VAR" if a[0] == "local" else "LOAD_FLOAT_VAR") \
                           and od[0] == a[1] and od[1] == LOCL and a[1] < params and a[1] < len(ptypes) and a[1] < len(dclrs):
                            mask, cls = DCLR.get(dclrs[a[1]], W_ANY), None
                            if mask == W_OBJ:
                                mask, cls = words_of_type(ptypes[a[1]])
                                mask |= W_OBJ
                            words.append((mask, cls))
                        elif a[0] == "self" and o == "LOAD_INST_MEM":
                            words.append((W_OBJ, c["name"]))
                        elif a[0] == "new" and o == a[1]:
                            words.append((NEW_ARY[a[1]], None))
                            k -= 1
                        elif a[0] == "lit" and o == "LOAD_INT_LIT":
                            words.append((W_INT, None))
                        else:
                            ok = False
                            break
                        k -= 1
                    if not ok:
                        continue
                    words.reverse()
                    rec.direct += 1
                    if rec.args is None:
                        rec.args = [[0, set(), False] for _ in words]
                    for slot, (mask, cls) in zip(rec.args, words):
                        slot[0] |= mask
                        if mask & W_OBJ:
                            if cls:
                                slot[1].add(cls)
                            else:
                                slot[2] = True
                    tail_rtrn = i + 1 < len(code) and oi.NAMES.get(code[i + 1][0]) == "RTRN"
                    if opname == "TRAP_RTRN":
                        rec.pushes.add(1)
                    else:
                        rec.pushes.add(1 if tail_rtrn and rtrn[:1] != "n" else 0)
                    if tail_rtrn and rtrn[:1] not in ("n", "m"):
                        mask, cls = words_of_type(rtrn)
                        rec.result[0] |= mask
                        if mask & W_OBJ:
                            if cls:
                                rec.result[1].add(cls)
                            else:
                                rec.result[2] = True
                    elif opname == "TRAP_RTRN":
                        rec.result[0] |= W_ANY
    return seen, len(files), parsed


def mask_text(mask):
    if mask == W_ANY or mask == 0:
        return "W_ANY"
    return " | ".join(n for bit, n in WORD_NAMES if mask & bit)


def arg_text(mask, classes, unnamed):
    cls = "nullptr"
    if (mask & W_OBJ) and classes and not unnamed:
        cls = 'L"%s"' % "|".join(sorted(classes))
    return "{ %s, %s }" % (mask_text(mask), cls)


def build_rows(seen, shapes, ids):
    """The rows, the notes, and the errors: with an error the header is not written."""
    frames = frame_traps()
    library = library_traps(set(shapes))
    rows, problems, errors = [], [], []
    for trap in sorted(set(shapes) | set(LITERAL_COPIES) | {"CPY_CHAR_STR_ARYS"}, key=lambda t: ids.get(t, 0)):
        if trap not in ids:
            problems.append("%s: no id" % trap)
            continue
        rec = seen.get(trap)
        if trap in LITERAL_COPIES:
            kind = LITERAL_COPIES[trap]
            rows.append((trap, True, 3, 1, (kind, set(), False), [(kind, set(), False), (W_INT, set(), False)], None, False))
            continue
        if trap == "CPY_CHAR_STR_ARYS":
            rows.append((trap, True, -1, 1, (W_OBJ_ARY, set(), False), [], None, False))
            continue
        shape = shapes[trap]
        count = shape["count"]
        if count - 1 > MAX_ARGS:
            problems.append("%s: more than %d arguments" % (trap, MAX_ARGS))
        if rec:
            for (op, n) in rec.ops:
                if (op, n) != (shape["op"], count):
                    problems.append("%s: an image holds %s %d, the compiler writes %s %d" % (trap, op, n, shape["op"], count))
        args = [(W_ANY, set(), False)] * (count - 1)
        pushes = 1 if shape["op"] == "TRAP_RTRN" else 0
        result = (W_ANY, set(), False)
        if rec and rec.args is not None and len(rec.args) == count - 1:
            args = [tuple(a) for a in rec.args]
            if len(rec.pushes) > 1:
                problems.append("%s: pushes a result in one library method and not in another" % trap)
            pushes = max(rec.pushes)
            if rec.result[0]:
                result = tuple(rec.result)
        elif shape["op"] == "TRAP" and not rec:
            problems.append("%s: never seen in a library method: whether it pushes a result is assumed (no)" % trap)
        if trap in OVERRIDES:
            pushes = OVERRIDES[trap].get("pushes", pushes)
            result = OVERRIDES[trap].get("result", result)
        contexts = None
        if trap in frames:
            contexts = OVERRIDES.get(trap, {}).get("contexts")
            if contexts is None:
                contexts = sorted(rec.contexts) if rec else []
            if not contexts:
                errors.append("%s: its handler reads the caller's frame or instance, and no image holds it: the row would "
                              "name no method it may stand in, and every program that uses it would be refused" % trap)
            else:
                # a list that is not empty can still lack a method: the loader refuses the trap there
                wrong = context_problems(trap, contexts, library)
                if wrong:
                    errors.append("%s: %s" % (trap, wrong))
        rows.append((trap, shape["op"] == "TRAP_RTRN", count, pushes, result, args, contexts, frames.get(trap, False)))
    return rows, problems, errors


def write_header(rows, images):
    out = []
    out.append("// Generated by tools/cicd/gen_trap_signatures.py from the compiler's directives and the\n"
               "// compiled libraries. Do not edit: change the directive or the library method and run\n"
               "// the generator. What each trap takes and returns, for Loader::Verify. (No count of\n"
               "// images is written: it changes with every new test and would change this file, and\n"
               "// fail --check, for no reason.)\n"
               "#ifndef __TRAP_SIGNATURES_H__\n#define __TRAP_SIGNATURES_H__\n\n#include \"traps.h\"\n#include \"instrs.h\"\n\n"
               "namespace trap_signatures {\n"
               "  // what a word may be; W_ANY where the libraries do not say\n"
               "  enum Word : unsigned char {\n"
               "    W_INT = 1, W_FLOAT = 2, W_BYTE_ARY = 4, W_CHAR_ARY = 8, W_INT_ARY = 16, W_FLOAT_ARY = 32,\n"
               "    W_OBJ_ARY = 64, W_OBJ = 128, W_ANY = 255\n  };\n\n"
               "  // 'classes': the classes an object is an instance of, 'A|B', or nullptr for any\n"
               "  struct Arg {\n    unsigned char words;\n    const wchar_t* classes;\n  };\n\n"
               "  static const int MAX_ARGS = %d;\n\n"
               "  // 'count' is the instruction's operand (the id and the arguments), -1 where it\n"
               "  // varies; 'contexts' names the methods a trap that reads its caller's frame or\n"
               "  // instance may stand in, 'A|B', nullptr for a trap that reads the operand stack alone;\n"
               "  // 'instance' is set where it is the method's instance the handler reads\n"
               "  struct Row {\n    long long id;\n    const wchar_t* name;\n    bool rtrn;\n    short count;\n    unsigned char pushes;\n"
               "    Arg result;\n    Arg args[MAX_ARGS];\n    const wchar_t* contexts;\n    bool instance;\n  };\n\n"
               "  // sorted by id\n  static const Row rows[] = {\n" % (MAX_ARGS,))
    for (trap, rtrn, count, pushes, result, args, contexts, instance) in rows:
        cells = ", ".join(arg_text(*a) for a in args) if args else ""
        ctx = "nullptr" if contexts is None else 'L"%s"' % "|".join(contexts)
        out.append("    { instructions::%s, L\"%s\", %s, %d, %d, %s, { %s }, %s, %s },\n" %
                   (trap, trap, "true" if rtrn else "false", count, pushes, arg_text(*result), cells, ctx,
                    "true" if instance else "false"))
    out.append("  };\n\n  static const int NUM_ROWS = sizeof(rows) / sizeof(rows[0]);\n\n"
               "  inline const Row* Find(long long id) {\n    int low = 0;\n    int high = NUM_ROWS - 1;\n"
               "    while(low <= high) {\n      const int mid = (low + high) / 2;\n      if(rows[mid].id == id) {\n        return &rows[mid];\n      }\n"
               "      if(rows[mid].id < id) {\n        low = mid + 1;\n      }\n      else {\n        high = mid - 1;\n      }\n    }\n    return nullptr;\n  }\n}\n\n#endif\n")
    with open(HEADER, "w", encoding="utf-8", newline="\n") as f:
        f.write("".join(out))


def check():
    """The committed header against the compiler: needs no build."""
    shapes, problems = trap_shapes.shapes()
    with open(HEADER, encoding="utf-8") as f:
        text = f.read()
    rows = {m.group(1): (m.group(2) == "true", int(m.group(3)))
            for m in re.finditer(r'\{ instructions::([A-Z0-9_]+), L"[A-Z0-9_]+", (true|false), (-?\d+),', text)}
    for trap, shape in sorted(shapes.items()):
        if trap not in rows:
            problems.append("%s: the compiler writes it and trap_signatures.h has no row (run gen_trap_signatures.py)" % trap)
        elif rows[trap] != (shape["op"] == "TRAP_RTRN", shape["count"]):
            problems.append("%s: trap_signatures.h says %s %d, the compiler writes %s %d" %
                            (trap, "TRAP_RTRN" if rows[trap][0] else "TRAP", rows[trap][1], shape["op"], shape["count"]))
    for trap in sorted(set(rows) - set(shapes) - set(LITERAL_COPIES) - {"CPY_CHAR_STR_ARYS"}):
        problems.append("%s: a row for a trap the compiler no longer writes" % trap)
    # The literal copies are written by EmitStaticArray, not by a directive, so they
    # are in no shape above: each is TRAP_RTRN, with the array, the table index and
    # the id (3), or one word for each string (-1). Without its row the loader
    # refuses every program that holds an array literal.
    for trap, count in sorted([(t, 3) for t in LITERAL_COPIES] + [("CPY_CHAR_STR_ARYS", -1)]):
        if trap not in rows:
            problems.append("%s: an array literal is copied with it and trap_signatures.h has no row (run gen_trap_signatures.py)" % trap)
        elif rows[trap] != (True, count):
            problems.append("%s: trap_signatures.h says %s %d, an array literal is copied with TRAP_RTRN %d" %
                            (trap, "TRAP_RTRN" if rows[trap][0] else "TRAP", rows[trap][1], count))
    # Where a trap may stand: the last two cells of its row. A trap whose handler
    # reads the frame or the instance names exactly the methods the library sources
    # hold its directive in, and no other trap names any.
    frames = frame_traps()
    library = library_traps(set(shapes))
    tails = {m.group(1): (m.group(2), m.group(3) == "true")
             for m in re.finditer(r'\{ instructions::([A-Z0-9_]+), L"[A-Z0-9_]+", [^\n]*, (nullptr|L"[^"\n]*"), (true|false) \},\n', text)}
    for trap in sorted(rows):
        if trap not in tails:
            problems.append("%s: the row's contexts cannot be read" % trap)
            continue
        ctx, instance = tails[trap]
        if trap in frames:
            if ctx == "nullptr" or not [c for c in ctx[2:-1].split("|") if c]:
                problems.append("%s: its handler reads the caller's frame or instance and trap_signatures.h names no method "
                                "it may stand in (generated from an empty or partial corpus?)" % trap)
            else:
                wrong = context_problems(trap, ctx[2:-1].split("|"), library)
                if wrong:
                    problems.append("%s: trap_signatures.h has %s" % (trap, wrong))
            if instance != frames[trap]:
                problems.append("%s: trap_signatures.h says instance=%s, the handler %s the instance" %
                                (trap, "true" if instance else "false", "reads" if frames[trap] else "does not read"))
        elif ctx != "nullptr" or instance:
            problems.append("%s: trap_signatures.h limits where it may stand, and its handler reads neither the frame nor the instance" % trap)
    return problems


def observed(path):
    ids = trap_ids()
    names = {v: k for k, v in ids.items()}
    with open(HEADER, encoding="utf-8") as f:
        text = f.read()
    rows = {m.group(1): (int(m.group(3)), int(m.group(4)))
            for m in re.finditer(r'\{ instructions::([A-Z0-9_]+), L"[A-Z0-9_]+", (true|false), (-?\d+), (\d),', text)}
    seen = collections.defaultdict(set)
    with open(path) as f:
        for line in f:
            trap, moved, _ = line.split()
            seen[names.get(int(trap), trap)].add(int(moved))
    bad = 0
    for trap, moves in sorted(seen.items()):
        if trap not in rows:
            print("FAIL %s: ran, and has no row" % trap)
            bad += 1
        elif rows[trap][0] >= 0 and moves != {rows[trap][1] - rows[trap][0]}:
            print("FAIL %s: the row says %d, the run saw %s" % (trap, rows[trap][1] - rows[trap][0], sorted(moves)))
            bad += 1
    print("traps run %d of %d, %d differ" % (len(seen), len(rows), bad))
    return 1 if bad else 0


def main():
    if len(sys.argv) == 3 and sys.argv[1] == "--observed":
        return observed(sys.argv[2])
    if sys.argv[1:] == ["--check"]:
        problems = check()
        for p in problems:
            print("FAIL", p)
        print("trap_signatures.h: %s" % ("ok" if not problems else "%d problem(s)" % len(problems)))
        return 1 if problems else 0
    if len(sys.argv) < 2:
        print(__doc__)
        return 2
    ids = trap_ids()
    shapes, problems = trap_shapes.shapes()
    errors = ["%s: not a directory" % d for d in sys.argv[1:] if not os.path.isdir(d)]
    seen, images, parsed = census(sys.argv[1:], shapes, ids)
    fatal = []
    if not parsed:
        errors.append("no image could be read from %s (%d .obe file(s) found): nothing to describe the traps from" %
                      (", ".join(sys.argv[1:]), images))
    else:
        rows, more, fatal = build_rows(seen, shapes, ids)
    errors += fatal
    if errors:
        for e in errors:
            print("ERROR", e)
        print("trap_signatures.h: not written (%d error(s))" % len(errors))
        return 1
    for p in problems + more:
        print("NOTE", p)
    write_header(rows, images)
    untyped = [r[0] for r in rows if r[2] > 1 and all(a[0] == W_ANY for a in r[5])]
    print("rows %d from %d images; untyped: %s" % (len(rows), images, ", ".join(untyped) or "none"))
    return 0


if __name__ == "__main__":
    sys.exit(main())
