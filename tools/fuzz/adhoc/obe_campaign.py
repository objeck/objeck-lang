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

Verdict. The inputs that make a run reproducible are printed and written to <out>/verdict.json
(rng seed, mutant count, the hash of the seed list by content, modes, structured share), with the
number of mutants attempted and completed: fewer completed than requested fails (status 3). A crash
passes only by SIGNATURE (the function that faulted, from the --jit=off gdb frames; '(jit code)'
for a fault in generated code) listed in obe_accepted_crashes.json, whose every entry carries a
reason and the class number from docs/design/OBE_TYPE_VERIFIER.md / the residual assessment. When the
run's inputs equal the ones the file was measured with, a crash must also be one of that signature's
witness mutants, so a NEW crash at a known site cannot replace a known one. A signature that is not
listed, or a crash whose signature cannot be established (no --gdb), fails (status 1); a listed
signature not seen is reported, not failed (a fix landed: remove the entry). With no accepted file
(or --no-accepted) any crash fails. --replay DIR runs known crash images instead of mutating, to
re-derive the signatures; --write-accepted FILE writes a skeleton to complete by hand.
"""
import argparse
import collections
import hashlib
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
        self.raw = raw
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
        obe_image.skip_method_flags(r)
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


# ---- the verdict: crashes are accepted by SIGNATURE from a checked-in file, never by count ----

ACCEPTED_FORMAT = 1
DEFAULT_ACCEPTED = os.path.join(HERE, "obe_accepted_crashes.json")
RC_CRASH, RC_INCOMPLETE, RC_USAGE = 1, 3, 2


def seed_list_hash(seeds):
    """sha256 over 'name sha256(file)' lines of the sorted seed images: the seed LIST, by content."""
    h = hashlib.sha256()
    for s in sorted(seeds, key=lambda s: s.name):
        h.update(("%s %s\n" % (s.name, hashlib.sha256(s.raw).hexdigest())).encode())
    return h.hexdigest()


def crash_signature(record):
    """What a crash is, apart from which mutant made it: the function that faulted.

    Taken from the --jit=off run when it crashed (the interpreter's frames are named; compiled
    code has none), else from the first crashing mode: '(jit code)' for a fault in generated code,
    'silent-exit1' for a status 1 with no message. None when the frames were not recorded."""
    results = record["results"]
    crashed = [m for m, v in results.items() if v[0] == "crash"]
    if not crashed:
        return "silent-exit1"
    frames = record.get("frames_by_mode", {})
    pick = "off" if "off" in crashed and "off" in frames else crashed[0]
    got = frames.get(pick) or (record.get("frames") if pick == (crashed[0]) else None)
    if not got:
        return None
    if got[0] in ("??", "(no frames)", "(gdb timeout)"):
        return "(jit code)" if got[0] == "??" else None
    return got[0]


def load_accepted(path):
    """The accepted-crash file: {format, inputs, entries: [{signature, classes, reason, witnesses}]}.
    Raises ValueError naming what is wrong: an entry with no reason or class is not accepted."""
    with open(path, encoding="utf-8") as f:
        data = json.load(f)
    if data.get("format") != ACCEPTED_FORMAT or not isinstance(data.get("entries"), list):
        raise ValueError("%s: not a format-%d accepted-crash file" % (path, ACCEPTED_FORMAT))
    seen = set()
    for i, e in enumerate(data["entries"]):
        for key in ("signature", "classes", "reason"):
            if not e.get(key):
                raise ValueError("%s: entry %d has no %s" % (path, i, key))
        if not all(isinstance(c, int) for c in e["classes"]):
            raise ValueError("%s: entry %d: classes must be the assessment's class numbers" % (path, i))
        if e["signature"] in seen:
            raise ValueError("%s: signature %s listed twice" % (path, e["signature"]))
        seen.add(e["signature"])
    return data


def assess(crashes, accepted, inputs, attempted, completed, gdb):
    """(rc, lines, report). crashes: records; accepted: loaded file or None; inputs: the run's pinned
    inputs {seed, count, seeds_sha256, modes, structured}.

    PASS needs: every requested mutant completed; no crash at all when there is no accepted file;
    otherwise every crash has a signature in the file and, when the run's inputs are the ones the file
    was measured with, is one of that signature's witness mutants, so a NEW crash at a known site
    cannot stand in for a known one. Accepted signatures this run did not see are reported (a fix
    landed: remove the entry), not failed."""
    lines, bad = [], []
    if completed < attempted:
        bad.append("only %d of %d mutants completed" % (completed, attempted))
    sigs = {}
    if accepted is None:
        if crashes:
            bad.append("%d crash(es) and no accepted-crash file: none is accepted" % len(crashes))
    else:
        entries = {e["signature"]: e for e in accepted["entries"]}
        pinned = accepted.get("inputs", {})
        same = all(pinned.get(k) == inputs.get(k) for k in ("seed", "count", "seeds_sha256", "modes", "structured"))
        lines.append("accepted-crash file inputs %s this run's: crash identity is %s"
                     % ("equal" if same else "NOT equal to", "signature and witness mutant" if same else "signature only"))
        for r in crashes:
            sig = crash_signature(r)
            if sig is None:
                bad.append("%s: crashed and its signature could not be established (run with --gdb on a -g build)" % r["file"])
                continue
            sigs.setdefault(sig, []).append(r["file"])
            e = entries.get(sig)
            if e is None:
                bad.append("%s: UNACCEPTED crash signature %s (%s)" % (r["file"], sig, r["change"]))
            elif same and e.get("witnesses") and os.path.splitext(r["file"])[0] not in e["witnesses"]:
                bad.append("%s: signature %s is accepted but this mutant is not one of its witnesses (a new crash at a known site)"
                           % (r["file"], sig))
        for sig, e in sorted(entries.items()):
            if sig not in sigs:
                lines.append("accepted signature not seen (fixed? remove it from the file): %s (classes %s)" % (sig, e["classes"]))
    classes_of = {e["signature"]: e["classes"] for e in accepted["entries"]} if accepted else {}
    for sig, files in sorted(sigs.items()):
        lines.append("  %3d  %s  classes %s" % (len(files), sig, classes_of.get(sig, "UNACCEPTED")))
    report = {"inputs": inputs, "attempted": attempted, "completed": completed, "crashes": len(crashes),
              "signatures": {s: len(f) for s, f in sorted(sigs.items())}, "problems": bad}
    if bad:
        crash_problem = any(not b.startswith("only ") for b in bad)
        return (RC_CRASH if crash_problem else RC_INCOMPLETE), lines + ["verdict: FAIL"] + ["  - " + b for b in bad], report
    return 0, lines + ["verdict: PASS (%d crash(es), all accepted by signature)" % len(crashes)], report


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--obr", required=True)
    ap.add_argument("--seeds", default=None, help="directory of the seed .obe images (not needed with --replay)")
    ap.add_argument("--out", required=True)
    ap.add_argument("--count", type=int, default=10000)
    ap.add_argument("--seed", type=int, default=1)
    ap.add_argument("--jobs", type=int, default=16)
    ap.add_argument("--modes", default="default,off,jit1")
    ap.add_argument("--structured", type=float, default=0.7)
    ap.add_argument("--timeout", type=float, default=10)
    ap.add_argument("--gdb", action="store_true")
    ap.add_argument("--keep-hangs", action="store_true")
    ap.add_argument("--accepted", default=None,
                    help="accepted-crash file (default: obe_accepted_crashes.json beside this script when it "
                         "exists): a crash passes only if its signature is listed")
    ap.add_argument("--no-accepted", action="store_true", help="accept no crash at all, even if a file exists")
    ap.add_argument("--replay", default=None, metavar="DIR",
                    help="do not mutate: run the .obe files of DIR (known crash images) as the mutants, to "
                         "derive or re-check the accepted signatures; --count and --seeds are not used")
    ap.add_argument("--write-accepted", default=None, metavar="FILE",
                    help="write the crashes of this run as a NEW accepted-crash file skeleton (reasons and "
                         "class numbers are filled in by hand from the assessment)")
    args = ap.parse_args()

    modes = [m for m in args.modes.split(",") if m]
    unknown = [m for m in modes if m not in MODES]
    if not modes or unknown:
        print("unknown or no --modes: %s (known: %s)" % (",".join(unknown) or "none given", ",".join(MODES)))
        return RC_USAGE
    replay = None
    if args.replay:
        replay = sorted(f for f in os.listdir(args.replay) if f.endswith(".obe"))
        if not replay:
            print("no .obe files in", args.replay)
            return RC_USAGE
        args.count = len(replay)
        seeds = []
    else:
        if not args.seeds:
            print("--seeds is required (or --replay DIR)")
            return RC_USAGE
        seeds = [Seed(os.path.join(args.seeds, f)) for f in sorted(os.listdir(args.seeds)) if f.endswith(".obe")]
        if not seeds:
            print("no seeds in", args.seeds)
            return RC_USAGE
    # The inputs that make a run reproducible, printed and written with the verdict: the same seed,
    # count and seed list give the same mutants at any --jobs.
    inputs = {"seed": args.seed, "count": args.count, "seeds": len(seeds),
              "seeds_sha256": seed_list_hash(seeds) if seeds else "replay", "modes": ",".join(modes),
              "structured": args.structured}
    out = os.path.abspath(args.out)
    work = os.path.join(out, "work")
    keep = os.path.join(out, "crashes")
    os.makedirs(work, exist_ok=True)
    os.makedirs(keep, exist_ok=True)
    obr = os.path.abspath(args.obr)

    # the control: an unchanged seed must run, or every count below means nothing
    if seeds:
        control = os.path.join(work, "control.obe")
        with open(control, "wb") as f:
            f.write(pack(seeds[0].image))
        p = subprocess.run([obr, control], capture_output=True, timeout=args.timeout, cwd=work, stdin=subprocess.DEVNULL)
        if p.returncode != 0:
            print("control failed: the unchanged seed %s exits %d: %s" % (seeds[0].name, p.returncode, p.stdout[:200]))
            return RC_USAGE

    def one(i):
        # each mutant draws from its own stream, so a run is the same at any --jobs
        path = os.path.join(work, "m%d_%d.obe" % (args.seed, i))
        if replay is not None:
            with open(os.path.join(args.replay, replay[i]), "rb") as src:
                data = src.read()
            with open(path, "wb") as f:
                f.write(data)
            what = "replay " + replay[i]
            seed = argparse.Namespace(name=os.path.splitext(replay[i])[0])
        else:
            r = random.Random((args.seed << 32) ^ i)
            seed = r.choice(seeds)
            try:
                what, image = structured(r, seed) if r.random() < args.structured else blind(r, seed)
            except (ValueError, struct.error, IndexError):
                what, image = blind(r, seed)
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
            kept = os.path.join(keep, seed.name + ".obe" if replay is not None else "%s_%d_%d.obe" % (seed.name, args.seed, i))
            os.replace(path, kept)
            record = {"file": os.path.basename(kept), "seed": seed.name, "change": what,
                      "results": {m: list(results[m]) for m in modes}}
            if args.gdb and bad:
                # the interpreter's frames name the faulting function; compiled code has none. So the
                # --jit=off run is traced when it crashed, and the first crashing mode otherwise.
                traced = "off" if "off" in bad and results["off"][0] == "crash" else bad[0]
                flags, extra = MODES[traced]
                record["frames"] = gdb_frames(obr, flags, dict(os.environ, **extra), kept, args.timeout)
                record["frames_by_mode"] = {traced: record["frames"]}
        else:
            os.remove(path)
        return what.split(" ")[0] if not what.startswith("blind") else what, results, record

    totals = {m: collections.Counter() for m in modes}
    by_kind = collections.defaultdict(collections.Counter)
    records = []
    completed = 0
    harness_errors = []

    def guarded(i):
        # a harness failure for one mutant is counted (attempted != completed fails the verdict), not
        # allowed to end the run with a traceback and a status that reads like a crash
        try:
            return one(i)
        except Exception as e:  # noqa: BLE001
            harness_errors.append("mutant %d: %s: %s" % (i, type(e).__name__, e))
            return None

    with ThreadPoolExecutor(args.jobs) as ex:
        for done in ex.map(guarded, range(args.count)):
            if done is None:
                continue
            completed += 1
            kind, results, record = done
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

    print("inputs: seed=%d count=%d seeds=%d seeds_sha256=%s modes=%s structured=%s"
          % (inputs["seed"], inputs["count"], inputs["seeds"], inputs["seeds_sha256"], inputs["modes"], inputs["structured"]))
    print("mutants attempted %d, completed %d%s" % (args.count, completed,
          "" if not harness_errors else " (harness errors: %s)" % "; ".join(harness_errors[:3])))
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
    accepted_path = None if args.no_accepted else (args.accepted or (DEFAULT_ACCEPTED if os.path.isfile(DEFAULT_ACCEPTED) else None))
    if args.write_accepted:
        skeleton = {"format": ACCEPTED_FORMAT, "inputs": inputs, "entries": []}
        by_sig = collections.defaultdict(list)
        for r in crashes:
            by_sig[crash_signature(r) or "UNKNOWN"].append(os.path.splitext(r["file"])[0])
        for sig, files in sorted(by_sig.items()):
            skeleton["entries"].append({"signature": sig, "classes": [], "reason": "", "witnesses": sorted(files)})
        with open(args.write_accepted, "w", encoding="utf-8") as f:
            json.dump(skeleton, f, indent=1)
            f.write(chr(10))
        print("wrote %d signature(s) to %s: fill in classes and reasons by hand" % (len(by_sig), args.write_accepted))
    try:
        accepted = load_accepted(accepted_path) if accepted_path else None
    except (ValueError, OSError, json.JSONDecodeError) as e:
        print("accepted-crash file: %s" % e)
        return RC_USAGE
    rc, lines, report = assess(crashes, accepted, inputs, args.count, completed, args.gdb)
    print("accepted-crash file: %s" % (accepted_path or "none (any crash fails)"))
    for line in lines:
        print(line)
    report["harness_errors"] = harness_errors
    with open(os.path.join(out, "verdict.json"), "w", encoding="utf-8") as f:
        json.dump(report, f, indent=1)
    return rc


if __name__ == "__main__":
    sys.exit(main())
