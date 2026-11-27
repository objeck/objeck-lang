#!/usr/bin/env python3
"""Mutation campaign over compiled programs: every mutant must be refused by the
loader, stop with a runtime error, or run; none may crash the VM.

    python3 obe_campaign.py --obr <obr> --seeds <dir of .obe> --out <dir>
                            [--count 10000] [--seed 1] [--jobs 16]
                            [--modes default,off,jit1] [--structured 0.7]
                            [--timeout 10] [--gdb] [--keep-hangs]

obe_mutfuzz.py changes bytes blindly, and a blind change lands in a string or a
library method the program never runs nine times in ten. This driver also reads
the image (tools/fuzz/loader/obe_image.py) and changes one thing the loader and
the interpreter act on: an opcode (for another with the same operand layout, so
the image still parses), an operand, an instruction (dropped, repeated or
swapped with its neighbour), a method's parameter count, frame size or a
declaration's type, a class's sizes or parent. Six in ten structured changes
land in the program's own classes (the ones that run), the rest anywhere.

Each mutant runs once per mode: 'default' (JIT at its usual threshold), 'off'
(--jit=off) and 'jit1' (OBJECK_JIT_THRESHOLD=1: every method compiled on its
first call). Outcomes: ran (status 0), refused (status 1 and a message: the
loader or a runtime error), error (another small status), hang (timeout: a
changed loop bound is not a defect, so hangs are counted, not failed), crash
(a signal or an NTSTATUS). A crash is kept in <out>/crashes with a line in
<out>/crashes.jsonl saying what was changed; with --gdb (Linux) the faulting
frames are recorded and the crashes are grouped by them.

Exit status 1 when any mutant crashed.
"""
import argparse
import collections
import json
import os
import random
import re
import struct
import subprocess
import sys
import zlib
from concurrent.futures import ThreadPoolExecutor

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, os.path.join(HERE, "..", "loader"))
import obe_image  # noqa: E402

OP = obe_image.OP
NAMES = obe_image.NAMES
INTS = [0, 1, -1, 2, 3, 7, 255, 256, 65535, 0x7fffffff, -0x80000000, 0x40000000, 1 << 24, 100000]
# opcodes the loader refuses outright or that never appear in an executable
_SKIP = {"LIB_NEW_OBJ_INST", "LIB_MTHD_CALL", "LIB_OBJ_INST_CAST", "LIB_FUNC_DEF", "END_STMTS",
         "MTHD_CALL_JIT", "DYN_MTHD_CALL_JIT"}
# opcodes whose stand-in would block the run rather than test the VM
_BLOCKING = {"THREAD_SLEEP", "THREAD_JOIN", "THREAD_MUTEX", "CRITICAL_START", "CRITICAL_END"}


def layout_groups():
    """Loadable opcodes grouped by how many operand bytes follow them."""
    loader_cpp = os.path.normpath(os.path.join(HERE, "..", "..", "..", "core", "vm", "loader.cpp"))
    with open(loader_cpp, encoding="utf-8", errors="replace") as f:
        text = f.read()
    body = text[text.index("void Loader::LoadStatements"):]
    body = body[:body.index("void Loader::VerifyFailed")]
    loadable = set(re.findall(r"case ([A-Z0-9_]+):", body))
    groups = collections.defaultdict(list)
    for name, num in OP.items():
        if name not in loadable or name in _SKIP or name in _BLOCKING:
            continue
        if name in ("LOAD_INT_LIT", "LOAD_FLOAT_LIT"):
            groups["lit8"].append(num)
        elif name == "LOAD_CHAR_LIT":
            continue
        else:
            groups[obe_image.OPERAND_INTS.get(num, 0)].append(num)
    return groups


GROUPS = layout_groups()
GROUP_OF = {num: key for key, nums in GROUPS.items() for num in nums}


class Seed:
    """A decompressed image and where its methods are."""

    def __init__(self, path):
        self.path = path
        self.name = os.path.splitext(os.path.basename(path))[0]
        with open(path, "rb") as f:
            raw = f.read()
        self.image = zlib.decompress(raw[4:])
        _, classes, _ = obe_image.parse(self.image)
        self.classes = classes
        self.methods = []  # (class index, method name, start)
        self.own = []
        for ci, cls in enumerate(classes):
            own = "." not in cls["name"] and not cls["name"].startswith("$")
            for name, start, end in cls["methods"]:
                if end - start < 40:
                    continue
                self.methods.append((ci, name, start))
                if own:
                    self.own.append((ci, name, start))
        self._code = {}

    def code(self, method):
        ci, _, start = method
        if start not in self._code:
            self._code[start] = obe_image.method_code(self.image, start, self.classes[ci]["debug"])
        return self._code[start]

    def header(self, method):
        """Offsets of a method record's parameter count and frame size."""
        r = obe_image.Reader(self.image, method[2])
        r.int()
        r.skip(3)
        r.string()
        r.string()
        return r.pos, r.pos + 4

    def class_fields(self, ci):
        """Offsets of a class record's parent id, class size and instance size."""
        cls = self.classes[ci]
        r = obe_image.Reader(self.image, cls["start"])
        r.int()
        r.string()
        parent_at = r.pos
        r.int()
        r.string()
        r.skip(4 * max(r.int(), 0))
        r.byte()
        if r.byte() != 0:
            r.string()
        return parent_at, r.pos, r.pos + 4


def near(rng, value):
    pick = rng.random()
    if pick < 0.35:
        return value + rng.choice([-2, -1, 1, 2])
    if pick < 0.5:
        return value ^ (1 << rng.randrange(8))
    if pick < 0.6:
        return 0
    return rng.choice(INTS)


def put_int(b, at, value):
    struct.pack_into("<i", b, at, max(-0x80000000, min(0x7fffffff, value)))


def get_int(b, at):
    return struct.unpack_from("<i", b, at)[0]


def structured(rng, seed):
    """One change to something the VM acts on. Returns (description, image)."""
    b = bytearray(seed.image)
    pool = seed.own if seed.own and rng.random() < 0.6 else seed.methods
    for _ in range(50):
        method = rng.choice(pool)
        dclrs_at, code = seed.code(method)
        if code:
            break
    else:
        return blind(rng, seed)
    where = "%s" % method[1]
    kind = rng.choice(["opcode", "opcode", "opcode", "operand", "operand", "operand", "drop", "repeat", "swap",
                       "params", "frame", "dclr", "class", "lit"])
    count_at = code[0][1] - 4
    idx = rng.randrange(len(code))
    op, at, operands_at = code[idx]
    end = code[idx + 1][1] if idx + 1 < len(code) else None
    if kind == "opcode":
        group = GROUPS.get(GROUP_OF.get(op))
        if not group or len(group) < 2:
            kind = "operand"
        else:
            new = rng.choice([o for o in group if o != op])
            b[at] = new
            return "opcode %s[%d] %s -> %s" % (where, idx, NAMES.get(op, op), NAMES[new]), bytes(b)
    if kind == "operand":
        n = obe_image.OPERAND_INTS.get(op, 0)
        if n == 0:
            kind = "lit"
        else:
            which = rng.randrange(n)
            old = get_int(b, operands_at + 4 * which)
            new = near(rng, old)
            put_int(b, operands_at + 4 * which, new)
            return "operand %s[%d] %s #%d %d -> %d" % (where, idx, NAMES.get(op, op), which, old, new), bytes(b)
    if kind == "lit":
        lits = [c for c in code if c[0] == OP["LOAD_INT_LIT"]]
        if not lits:
            kind = "drop"
        else:
            op, at, operands_at = rng.choice(lits)
            old = struct.unpack_from("<q", b, operands_at)[0]
            new = near(rng, old) if rng.random() < 0.8 else rng.choice([1 << 40, -(1 << 40), 0x7fffffffffff])
            struct.pack_into("<q", b, operands_at, new)
            return "literal %s %d -> %d" % (where, old, new), bytes(b)
    if kind in ("drop", "repeat", "swap"):
        if end is None:
            end = at + 1 + (operands_at - at - 1) + 4 * obe_image.OPERAND_INTS.get(op, 0)
            if op in (OP["LOAD_INT_LIT"], OP["LOAD_FLOAT_LIT"]):
                end = operands_at + 8
            kind = "drop" if kind == "swap" else kind
        count = struct.unpack_from("<I", b, count_at)[0]
        if kind == "drop":
            del b[at:end]
            struct.pack_into("<I", b, count_at, count - 1)
            return "drop %s[%d] %s" % (where, idx, NAMES.get(op, op)), bytes(b)
        if kind == "repeat":
            b[at:at] = b[at:end]
            struct.pack_into("<I", b, count_at, count + 1)
            return "repeat %s[%d] %s" % (where, idx, NAMES.get(op, op)), bytes(b)
        nxt_end = code[idx + 2][1] if idx + 2 < len(code) else None
        if nxt_end is None:
            del b[at:end]
            struct.pack_into("<I", b, count_at, count - 1)
            return "drop %s[%d] %s" % (where, idx, NAMES.get(op, op)), bytes(b)
        first, second = bytes(b[at:end]), bytes(b[end:nxt_end])
        b[at:nxt_end] = second + first
        return "swap %s[%d] %s <-> %s" % (where, idx, NAMES.get(op, op), NAMES.get(code[idx + 1][0], "?")), bytes(b)
    if kind in ("params", "frame"):
        params_at, frame_at = seed.header(method)
        at = params_at if kind == "params" else frame_at
        old = get_int(b, at)
        new = near(rng, old)
        put_int(b, at, new)
        return "%s %s %d -> %d" % (kind, where, old, new), bytes(b)
    if kind == "dclr":
        n = get_int(b, dclrs_at)
        debug = seed.classes[method[0]]["debug"]
        if n < 1 or debug:
            old = get_int(b, dclrs_at)
            put_int(b, dclrs_at, near(rng, old))
            return "dclr count %s %d" % (where, old), bytes(b)
        which = rng.randrange(n)
        at = dclrs_at + 4 + 4 * which
        old = get_int(b, at)
        new = rng.randrange(-1, 12)
        put_int(b, at, new)
        return "dclr %s #%d type %d -> %d" % (where, which, old, new), bytes(b)
    # class
    parent_at, cls_size_at, inst_size_at = seed.class_fields(method[0])
    at = rng.choice([parent_at, cls_size_at, inst_size_at])
    old = get_int(b, at)
    new = near(rng, old)
    put_int(b, at, new)
    label = {parent_at: "parent", cls_size_at: "class size", inst_size_at: "instance size"}[at]
    return "class %s %s %d -> %d" % (seed.classes[method[0]]["name"], label, old, new), bytes(b)


def blind(rng, seed):
    """obe_mutfuzz.py's byte-level changes."""
    b = bytearray(seed.image)
    kind = rng.choice(["flip", "int", "int", "chunkdel", "chunkdup", "trunc", "zero", "head"])
    n = len(b)
    if kind == "flip":
        for _ in range(rng.randint(1, 8)):
            b[rng.randrange(n)] ^= 1 << rng.randrange(8)
    elif kind == "int":
        for _ in range(rng.randint(1, 4)):
            p = rng.randrange(0, n - 4) & ~3 if rng.random() < .7 else rng.randrange(0, n - 4)
            struct.pack_into("<i", b, p, rng.choice(INTS))
    elif kind == "chunkdel":
        p = rng.randrange(n)
        del b[p:p + rng.randint(1, 64)]
    elif kind == "chunkdup":
        p = rng.randrange(n)
        b[p:p] = b[p:p + rng.randint(1, 64)]
    elif kind == "trunc":
        del b[rng.randrange(8, n):]
    elif kind == "zero":
        p = rng.randrange(n)
        size = rng.randint(1, 128)
        b[p:p + size] = bytes(len(b[p:p + size]))
    elif kind == "head":
        for _ in range(rng.randint(1, 3)):
            b[rng.randrange(0, min(n, 64))] = rng.randrange(256)
    return "blind " + kind, bytes(b)


def pack(image):
    return struct.pack("<I", len(image)) + zlib.compress(image, 1)


MODES = {
    "default": ([], {}),
    "off": (["--jit=off"], {}),
    "jit1": ([], {"OBJECK_JIT_THRESHOLD": "1"}),
}


def _die_with_parent():
    """Linux: a child is killed when this process goes, so stopping a campaign cannot
    leave a mutant that loops forever running (PR_SET_PDEATHSIG)."""
    try:
        import ctypes
        import signal
        ctypes.CDLL("libc.so.6", use_errno=True).prctl(1, signal.SIGKILL)
    except (OSError, AttributeError):
        pass


CHILD = {"preexec_fn": _die_with_parent} if sys.platform.startswith("linux") else {}


def classify(rc, text):
    if rc == 0:
        return "ran"
    if rc < 0 or rc >= 0xC0000000 or rc in (139, 134, 136, 132, 135, 138):
        return "crash"
    if rc in (1, 255, 4294967295) and text.strip():
        return "refused"
    if rc == 1:
        return "silent-exit1"
    return "error"


_FRAME = re.compile(r"^#\d+\s+(?:0x[0-9a-f]+ in )?([^\s(]+)")


def gdb_frames(obr, flags, env, path, timeout):
    """The named frames at the fault, innermost first."""
    cmd = ["gdb", "-q", "-batch", "-ex", "run", "-ex", "bt 12", "--args", obr] + flags + [path]
    try:
        p = subprocess.run(cmd, capture_output=True, timeout=timeout * 4, env=env, stdin=subprocess.DEVNULL, **CHILD)
    except subprocess.TimeoutExpired:
        return ["(gdb timeout)"]
    frames = []
    for line in p.stdout.decode("utf-8", "replace").splitlines():
        m = _FRAME.match(line)
        if m:
            frames.append(m.group(1))
    return frames or ["(no frames)"]


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--obr", required=True)
    ap.add_argument("--seeds", required=True)
    ap.add_argument("--out", required=True)
    ap.add_argument("--count", type=int, default=10000)
    ap.add_argument("--seed", type=int, default=1)
    ap.add_argument("--jobs", type=int, default=16)
    ap.add_argument("--modes", default="default,off,jit1")
    ap.add_argument("--structured", type=float, default=0.7)
    ap.add_argument("--timeout", type=float, default=10)
    ap.add_argument("--gdb", action="store_true")
    ap.add_argument("--keep-hangs", action="store_true")
    args = ap.parse_args()

    modes = [m for m in args.modes.split(",") if m]
    seeds = [Seed(os.path.join(args.seeds, f)) for f in sorted(os.listdir(args.seeds)) if f.endswith(".obe")]
    if not seeds:
        print("no seeds in", args.seeds)
        return 2
    out = os.path.abspath(args.out)
    work = os.path.join(out, "work")
    keep = os.path.join(out, "crashes")
    os.makedirs(work, exist_ok=True)
    os.makedirs(keep, exist_ok=True)
    obr = os.path.abspath(args.obr)

    # the control: an unchanged seed must run, or every count below means nothing
    control = os.path.join(work, "control.obe")
    with open(control, "wb") as f:
        f.write(pack(seeds[0].image))
    p = subprocess.run([obr, control], capture_output=True, timeout=args.timeout, cwd=work, stdin=subprocess.DEVNULL)
    if p.returncode != 0:
        print("control failed: the unchanged seed %s exits %d: %s" % (seeds[0].name, p.returncode, p.stdout[:200]))
        return 2

    def one(i):
        # each mutant draws from its own stream, so a run is the same at any --jobs
        r = random.Random((args.seed << 32) ^ i)
        seed = r.choice(seeds)
        try:
            what, image = structured(r, seed) if r.random() < args.structured else blind(r, seed)
        except (ValueError, struct.error, IndexError):
            what, image = blind(r, seed)
        path = os.path.join(work, "m%d_%d.obe" % (args.seed, i))
        with open(path, "wb") as f:
            f.write(pack(image))
        results = {}
        for mode in modes:
            flags, extra = MODES[mode]
            env = dict(os.environ, **extra)
            try:
                p = subprocess.run([obr] + flags + [path], capture_output=True, timeout=args.timeout, env=env,
                                   cwd=work, stdin=subprocess.DEVNULL, **CHILD)
                text = (p.stdout + p.stderr).decode("utf-8", "replace")
                cls = classify(p.returncode, text)
                first = text.strip().splitlines()[0][:120] if text.strip() else ""
                results[mode] = (cls, p.returncode, first)
            except subprocess.TimeoutExpired:
                results[mode] = ("hang", None, "")
        record = None
        bad = [m for m in modes if results[m][0] in ("crash", "silent-exit1")]
        hung = [m for m in modes if results[m][0] == "hang"]
        if bad or (hung and args.keep_hangs):
            kept = os.path.join(keep, "%s_%d_%d.obe" % (seed.name, args.seed, i))
            os.replace(path, kept)
            record = {"file": os.path.basename(kept), "seed": seed.name, "change": what,
                      "results": {m: list(results[m]) for m in modes}}
            if args.gdb and bad:
                flags, extra = MODES[bad[0]]
                record["frames"] = gdb_frames(obr, flags, dict(os.environ, **extra), kept, args.timeout)
        else:
            os.remove(path)
        return what.split(" ")[0] if not what.startswith("blind") else what, results, record

    totals = {m: collections.Counter() for m in modes}
    by_kind = collections.defaultdict(collections.Counter)
    records = []
    with ThreadPoolExecutor(args.jobs) as ex:
        for kind, results, record in ex.map(one, range(args.count)):
            worst = "ran"
            for m in modes:
                totals[m][results[m][0]] += 1
                for level in ("crash", "silent-exit1", "hang", "error", "refused"):
                    if results[m][0] == level:
                        if ["ran", "refused", "error", "hang", "silent-exit1", "crash"].index(level) > \
                           ["ran", "refused", "error", "hang", "silent-exit1", "crash"].index(worst):
                            worst = level
            by_kind[kind][worst] += 1
            if record:
                records.append(record)

    with open(os.path.join(out, "crashes.jsonl"), "w", encoding="utf-8") as f:
        for record in records:
            f.write(json.dumps(record) + "\n")

    print("mutants %d, seeds %d, rng seed %d" % (args.count, len(seeds), args.seed))
    for m in modes:
        print("  %-8s %s" % (m, dict(sorted(totals[m].items()))))
    print("by change (worst outcome over the modes):")
    for kind, counts in sorted(by_kind.items()):
        print("  %-14s %s" % (kind, dict(sorted(counts.items()))))
    crashes = [r for r in records if any(v[0] in ("crash", "silent-exit1") for v in r["results"].values())]
    if crashes:
        clusters = collections.defaultdict(list)
        for r in crashes:
            frames = r.get("frames")
            if frames:
                named = [f for f in frames if f != "??"][:3]
                key = " < ".join(named) if named else "(jit code)"
            else:
                key = "status " + ",".join(sorted({str(v[1]) for v in r["results"].values() if v[0] == "crash"}))
            clusters[key].append(r)
        print("crash clusters: %d (%d mutants)" % (len(clusters), len(crashes)))
        for key, rs in sorted(clusters.items(), key=lambda kv: -len(kv[1])):
            print("  %5d  %s" % (len(rs), key))
            for r in rs[:3]:
                print("           %s: %s" % (r["file"], r["change"]))
    return 1 if crashes else 0


if __name__ == "__main__":
    sys.exit(main())
