#!/usr/bin/env python3
"""Reads and rewrites a decompressed program image, the bytes Loader::Load reads.

The layout mirrors core/vm/loader.cpp (Load, LoadClasses, LoadMethods,
LoadStatements); the opcode numbers come from core/shared/instrs.h, read at run
time so a renumbering cannot leave this file behind. Used to

  * cut small seeds out of big ones (slice): a regression program's image
    carries the whole standard library and is 100 KB to 1.5 MB, which keeps
    libFuzzer near 100 executions a second; a slice of a few classes and
    methods is a few hundred bytes to a few KB.
  * re-stamp an image with the current toolchain version (set_version), so a
    stored malformed input is not refused by the version check before it
    reaches the code it was written to exercise.

    python3 obe_image.py dump  <image>          # one line per class and method
    python3 obe_image.py slice <image> <classes> <methods> <out>
"""
import os
import re
import struct
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
INSTRS_H = os.path.normpath(os.path.join(HERE, "..", "..", "..", "core", "shared", "instrs.h"))


def opcodes():
    """{name: number} for enum InstructionType in instrs.h."""
    with open(INSTRS_H, encoding="utf-8", errors="replace") as f:
        text = f.read()
    body = text[text.index("enum InstructionType"):]
    body = body[body.index("{") + 1:body.index("}")]
    body = re.sub(r"//[^\n]*", "", body)
    codes, value = {}, 0
    for item in body.split(","):
        item = item.strip()
        if not item:
            continue
        if "=" in item:
            name, num = item.split("=")
            value = int(num.strip(), 0)
            item = name.strip()
        codes[item] = value
        value += 1
    return codes


OP = opcodes()
NAMES = {v: k for k, v in OP.items()}

# operand layout per opcode, as LoadStatements reads it
_ONE_INT = ["TRAP", "TRAP_RTRN", "OBJ_INST_CAST", "NEW_FLOAT_ARY", "NEW_INT_ARY", "NEW_BYTE_ARY",
            "NEW_CHAR_ARY", "NEW_OBJ_INST", "NEW_FUNC_INST", "LBL", "OBJ_TYPE_OF", "JMP_TABLE_SLOT"]
_TWO_INTS = ["LOAD_INT_VAR", "LOAD_FUNC_VAR", "LOAD_FLOAT_VAR", "STOR_INT_VAR", "STOR_FUNC_VAR",
             "STOR_FLOAT_VAR", "COPY_INT_VAR", "COPY_FLOAT_VAR", "LOAD_BYTE_ARY_ELM", "LOAD_CHAR_ARY_ELM",
             "LOAD_INT_ARY_ELM", "LOAD_FLOAT_ARY_ELM", "STOR_BYTE_ARY_ELM", "STOR_CHAR_ARY_ELM",
             "STOR_INT_ARY_ELM", "STOR_FLOAT_ARY_ELM", "DYN_MTHD_CALL", "JMP", "TRY_START"]
_THREE_INTS = ["MTHD_CALL", "ASYNC_MTHD_CALL", "JMP_TABLE"]
OPERAND_INTS = {}
for _names, _n in ((_ONE_INT, 1), (_TWO_INTS, 2), (_THREE_INTS, 3)):
    for _name in _names:
        OPERAND_INTS[OP[_name]] = _n


class Reader:
    def __init__(self, data, pos=0):
        self.data, self.pos = data, pos

    def int(self):
        v = struct.unpack_from("<i", self.data, self.pos)[0]
        self.pos += 4
        return v

    def uint(self):
        v = struct.unpack_from("<I", self.data, self.pos)[0]
        self.pos += 4
        return v

    def byte(self):
        v = self.data[self.pos]
        self.pos += 1
        return v

    def skip(self, n):
        if n < 0 or self.pos + n > len(self.data):
            raise ValueError(f"bad length {n} at {self.pos}")
        self.pos += n

    def string(self):
        n = self.int()
        s = self.data[self.pos:self.pos + n].decode("utf-8", errors="replace")
        self.skip(n)
        return s


def read_header(r):
    """Reads through the start ids; returns where the class count starts."""
    r.int()  # version
    r.int()  # magic
    r.int()  # string class id
    for width in (8, 1, 1, 8):  # float, bool, byte, int strings
        for _ in range(r.int()):
            r.skip(r.int() * width)
    for _ in range(r.int()):  # char strings
        r.string()
    r.int()  # start class
    r.int()  # start method
    return r.pos


def read_dclrs(r, debug):
    for _ in range(r.int()):
        r.int()
        if debug:
            r.string()


def read_statements(r, debug):
    for _ in range(r.uint()):
        op = r.byte()
        if debug:
            r.int()
        if op == OP["LOAD_INT_LIT"] or op == OP["LOAD_FLOAT_LIT"]:
            r.skip(8)
        elif op == OP["LOAD_CHAR_LIT"]:
            r.skip(r.int())
        else:
            r.skip(4 * OPERAND_INTS.get(op, 0))


def method_code(data, start, debug=False):
    """For the method record at 'start': where its declarations begin, and one
    (opcode, opcode offset, operands offset) per instruction."""
    r = Reader(data, start)
    r.int()  # id
    r.skip(3)
    r.string()  # name
    r.string()  # return
    r.int()  # params
    r.int()  # memory size
    dclrs_at = r.pos
    read_dclrs(r, debug)
    code = []
    for _ in range(r.uint()):
        at = r.pos
        op = r.byte()
        if debug:
            r.int()
        operands_at = r.pos
        if op == OP["LOAD_INT_LIT"] or op == OP["LOAD_FLOAT_LIT"]:
            r.skip(8)
        elif op == OP["LOAD_CHAR_LIT"]:
            r.skip(r.int())
        else:
            r.skip(4 * OPERAND_INTS.get(op, 0))
        code.append((op, at, operands_at))
    return dclrs_at, code


def start_ids_at(data):
    """Where the start class and start method ids sit in the header."""
    return read_header(Reader(data)) - 8


def read_method(r, debug):
    """Returns (name, start, id_at, end)."""
    start = r.pos
    r.int()  # id
    r.skip(3)
    name = r.string()
    r.string()  # return
    r.int()  # params
    r.int()  # memory size
    read_dclrs(r, debug)
    read_statements(r, debug)
    return name, start, r.pos


def read_class(r):
    """Returns a dict describing one class record's spans."""
    start = r.pos
    r.int()  # id
    name = r.string()
    r.int()  # parent id
    r.string()
    r.skip(4 * max(r.int(), 0))  # interfaces
    r.byte()  # virtual
    debug = r.byte() != 0
    if debug:
        r.string()
    r.int()
    r.int()
    read_dclrs(r, debug)
    read_dclrs(r, debug)
    for _ in range(r.int()):  # closures
        r.int()
        read_dclrs(r, debug)
    methods_at = r.pos
    methods = [read_method(r, debug) for _ in range(r.int())]
    return {"name": name, "start": start, "methods_at": methods_at, "methods": methods, "end": r.pos,
            "debug": debug}


def parse(data):
    r = Reader(data)
    classes_at = read_header(r)
    classes = [read_class(r) for _ in range(r.int())]
    return classes_at, classes, r.pos


def set_version(data, version):
    return struct.pack("<i", version) + data[4:]


def slice_image(data, keep_classes, keep_methods, strip_strings=True):
    """The header plus the classes keep_classes names (a count: the first that
    many; or a list of indices), each with at most keep_methods methods, ids
    renumbered so every one is in range. With strip_strings the five string
    tables are emptied (the loader does not relate instructions to them)."""
    classes_at, classes, _ = parse(data)
    if isinstance(keep_classes, int):
        keep_classes = range(min(keep_classes, len(classes)))
    if strip_strings:
        # version, magic, string class id; five empty tables; the start ids
        out = bytearray(data[:12] + struct.pack("<5i", 0, 0, 0, 0, 0) + data[classes_at - 8:classes_at])
    else:
        out = bytearray(data[:classes_at])
    kept = [classes[i] for i in keep_classes]
    out += struct.pack("<i", len(kept))
    for cid, cls in enumerate(kept):
        out += struct.pack("<i", cid) + data[cls["start"] + 4:cls["methods_at"]]
        methods = cls["methods"][:keep_methods]
        out += struct.pack("<i", len(methods))
        for mid, (_, mstart, mend) in enumerate(methods):
            out += struct.pack("<i", mid) + data[mstart + 4:mend]
    return bytes(out)


def main():
    args = sys.argv[1:]
    if len(args) == 2 and args[0] == "dump":
        with open(args[1], "rb") as f:
            data = f.read()
        classes_at, classes, end = parse(data)
        print(f"header {classes_at} bytes, {len(classes)} classes, {end} of {len(data)} bytes read")
        for cls in classes:
            print(f"  {cls['name']}: {len(cls['methods'])} methods, {cls['end'] - cls['start']} bytes")
        return 0
    if len(args) == 5 and args[0] == "slice":
        with open(args[1], "rb") as f:
            data = f.read()
        with open(args[4], "wb") as f:
            f.write(slice_image(data, int(args[2]), int(args[3])))
        return 0
    print(__doc__)
    return 2


if __name__ == "__main__":
    sys.exit(main())
