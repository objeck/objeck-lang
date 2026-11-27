#!/usr/bin/env python3
"""What the compiler writes for each system directive that ends in a trap.

Reads IntermediateEmitter::EmitSystemDirective (core/compiler/intermediate.cpp) and
returns, per trap id name, the instructions written before the trap and the trap
itself: {name: {"op": "TRAP"|"TRAP_RTRN", "count": n, "args": [...], "after": [...]}}.
An argument is ("local", k), ("flocal", k), ("self",), ("field", k), ("lit",) or
("new", OPCODE): the words below the trap id, the first pushed first.
"""
import os
import re

HERE = os.path.dirname(os.path.abspath(__file__))
EMITTER = os.path.normpath(os.path.join(HERE, "..", "..", "core", "compiler", "intermediate.cpp"))

_MAKE = re.compile(r"Make(IntLit)?Instruction\(statement, cur_line_num, ([^;]*)\)\);")


def directive_body(text):
    start = text.index("void IntermediateEmitter::EmitSystemDirective")
    end = text.index("\nvoid IntermediateEmitter::", start + 10)
    return text[start:end]


def shapes(path=EMITTER):
    with open(path, encoding="utf-8", errors="replace") as f:
        body = directive_body(f.read())
    out = {}
    problems = []
    # split on case labels; consecutive labels share a block
    parts = re.split(r"\n\s*case\s+(?:instructions::)?([A-Z0-9_]+):", body)
    labels = []
    for i in range(1, len(parts), 2):
        labels.append(parts[i])
        block = parts[i + 1]
        if not block.strip() or block.strip() == "{":
            continue
        block = block.split("\n  default:")[0]
        # the library half of an is_lib choice names what the program half does by id
        block = re.sub(r"if\(is_lib\)\s*\{.*?\}\s*else\s*\{(.*?)\}", r"\1", block, flags=re.S)
        instrs = []
        for m in _MAKE.finditer(block):
            args = [a.strip() for a in m.group(2).split(",")]
            if m.group(1):
                instrs.append(("LIT", args[0]))
            else:
                instrs.append((args[0],) + tuple(args[1:]))
        traps = [k for k, ins in enumerate(instrs) if ins[0] in ("TRAP", "TRAP_RTRN")]
        if len(traps) > 1:
            problems.append("%s: more than one trap" % "/".join(labels))
        elif traps:
            at = traps[0]
            op = instrs[at][0]
            count = int(instrs[at][1].rstrip("L"))
            if at < 1 or instrs[at - 1][0] != "LIT" or not re.fullmatch(r"(instructions::)?[A-Z][A-Z0-9_]*", instrs[at - 1][1]):
                problems.append("%s: trap without a literal id" % "/".join(labels))
            else:
                trap = instrs[at - 1][1].replace("instructions::", "")
                args = []
                k = 0
                pre = instrs[:at - 1]
                while k < len(pre):
                    ins = pre[k]
                    if ins[0] == "LOAD_INT_VAR" and ins[2] == "LOCL":
                        args.append(("local", int(ins[1])))
                    elif ins[0] == "LOAD_FLOAT_VAR" and ins[2] == "LOCL":
                        args.append(("flocal", int(ins[1])))
                    elif ins[0] == "LOAD_INST_MEM" and k + 1 < len(pre) and pre[k + 1][0] == "LOAD_INT_VAR" and pre[k + 1][2] == "INST":
                        args.append(("field", int(pre[k + 1][1])))
                        k += 1
                    elif ins[0] == "LOAD_INST_MEM":
                        args.append(("self",))
                    elif ins[0] == "LIT" and k + 1 < len(pre) and pre[k + 1][0].startswith("NEW_") and pre[k + 1][0].endswith("_ARY"):
                        args.append(("new", pre[k + 1][0]))
                        k += 1
                    elif ins[0] == "LIT":
                        args.append(("lit",))
                    else:
                        problems.append("%s: unmodelled %s before the trap" % (trap, ins[0]))
                    k += 1
                if len(args) != count - 1:
                    problems.append("%s: %d words pushed for a count of %d" % (trap, len(args) + 1, count))
                row = {"op": op, "count": count, "args": args, "after": [x[0] for x in instrs[at + 1:]], "labels": list(labels)}
                if trap in out and (out[trap]["op"], out[trap]["count"], out[trap]["args"]) != (op, count, args):
                    problems.append("%s: written two ways" % trap)
                out[trap] = row
        labels = []
    return out, problems


if __name__ == "__main__":
    table, problems = shapes()
    for name in sorted(table):
        row = table[name]
        print("%-28s %-9s %d %s %s" % (name, row["op"], row["count"], row["args"], row["after"] or ""))
    print(len(table), "traps")
    for p in problems:
        print("PROBLEM", p)
