"""Blind mutation fuzz of a compiled .obe against obr. Usage: obe_mutfuzz.py <obr> <seed.obe> <count> <seed> <jobs> <outdir>
Classes: refused (exit 1 + message), ran (exit 0), error (other small exit), crash (signal / NTSTATUS), hang (timeout)."""
import os, random, struct, subprocess, sys, zlib, collections
from concurrent.futures import ThreadPoolExecutor

obr, seed_obe, count, seed, jobs, out = sys.argv[1], sys.argv[2], int(sys.argv[3]), int(sys.argv[4]), int(sys.argv[5]), sys.argv[6]
os.makedirs(out, exist_ok=True)
raw = open(seed_obe, "rb").read()
image = zlib.decompress(raw[4:])
rng = random.Random(seed)
INTS = [0, 1, -1, 2, 255, 256, 65535, 0x7fffffff, -0x80000000, 0x40000000, 1 << 24, 100000]


def mutate(img):
    b = bytearray(img)
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
        p = rng.randrange(n); l = rng.randint(1, 64); del b[p:p + l]
    elif kind == "chunkdup":
        p = rng.randrange(n); l = rng.randint(1, 64); b[p:p] = b[p:p + l]
    elif kind == "trunc":
        del b[rng.randrange(8, n):]
    elif kind == "zero":
        p = rng.randrange(n); l = rng.randint(1, 128); b[p:p + l] = bytes(len(b[p:p + l]))
    elif kind == "head":
        for _ in range(rng.randint(1, 3)):
            b[rng.randrange(0, min(n, 64))] = rng.randrange(256)
    return kind, bytes(b)


mutants = []
for i in range(count):
    kind, img = mutate(image)
    mutants.append((i, kind, struct.pack("<I", len(img)) + zlib.compress(img, 1)))


def run(m):
    i, kind, data = m
    path = os.path.join(out, "m%d_%d.obe" % (seed, i))
    with open(path, "wb") as f:
        f.write(data)
    try:
        p = subprocess.run([obr, path], capture_output=True, timeout=8)
        rc = p.returncode
        txt = (p.stdout + p.stderr).decode("utf-8", "replace")
        if rc == 0:
            cls = "ran"
        elif rc in (4294967295, -1, 255) and ">>>" in txt:
            cls = "refused"
        elif rc < 0 or rc >= 0xC0000000 or rc in (139, 134, 136, 132):
            cls = "crash"
        elif rc == 1 and txt.strip():
            cls = "refused"
        elif rc == 1:
            cls = "silent-exit1"
        else:
            cls = "error"
        sig = txt.strip().splitlines()[0][:90] if txt.strip() else ""
    except subprocess.TimeoutExpired:
        cls, rc, sig = "hang", None, ""
    if cls in ("ran", "refused", "error"):
        os.remove(path)
    return i, kind, cls, rc, sig, path


res = collections.Counter()
by_kind = collections.defaultdict(collections.Counter)
bad = []
with ThreadPoolExecutor(jobs) as ex:
    for i, kind, cls, rc, sig, path in ex.map(run, mutants):
        res[cls] += 1
        by_kind[kind][cls] += 1
        if cls in ("crash", "hang", "silent-exit1"):
            bad.append((i, kind, cls, rc, sig, path))
print("mutants", count, "seed", seed, dict(res))
for k, v in sorted(by_kind.items()):
    print("  %-9s %s" % (k, dict(v)))
for b in bad[:8]:
    print("BAD", b)
