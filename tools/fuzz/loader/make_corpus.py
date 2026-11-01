#!/usr/bin/env python3
"""Seed corpus for the loader's fuzz targets: the regression programs, compiled.

    python3 tools/fuzz/loader/make_corpus.py <deploy bin dir> <out dir> [-j N]

Compiles every programs/regression/*.obs with that tree's obc, the way
run_regression.sh does (libraries cipher,collect,xml,json plus the test's
'# EXTRA_LIBS:'), and every fourth one again with -debug so the loader's
debug-information branches are seeded too. Writes

    <out>/files/<name>.obe    as obc wrote it: loader_file_fuzz's corpus
    <out>/images/<name>.img   decompressed: loader_fuzz's corpus
    <out>/slices/<name>.<i>.img  small images cut from it (obe_image.py): one to
                              three classes, a few methods each, string tables
                              emptied. These are where loader_fuzz runs fast.

A test that does not compile (the negative tests) is skipped.
"""
import concurrent.futures
import os
import random
import subprocess
import sys
import zlib

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
import obe_image  # noqa: E402
REGRESSION = os.path.normpath(os.path.join(HERE, "..", "..", "..", "programs", "regression"))
EXE = ".exe" if os.name == "nt" else ""


def decode(data):
    """The image Loader::DecodeImage produces from an .obe file's bytes."""
    zlib_at_4 = len(data) >= 6 and data[4] == 0x78 and (0x78 * 256 + data[5]) % 31 == 0
    if data[:1] == b"\x78" and not zlib_at_4:
        return zlib.decompress(data)
    return zlib.decompress(data[4:])


def compile_one(obc, bin_dir, src, out, debug):
    name = os.path.splitext(os.path.basename(src))[0] + ("_debug" if debug else "")
    libs = "cipher,collect,xml,json"
    with open(src, encoding="utf-8", errors="replace") as f:
        for line in f:
            if line.startswith("# EXTRA_LIBS:"):
                libs += "," + line.split(":", 1)[1].strip()
                break
    obe = os.path.join(out, "files", name + ".obe")
    cmd = [obc, "-src", src, "-lib", libs, "-opt", "s3", "-dest", obe] + (["-debug"] if debug else [])
    try:
        p = subprocess.run(cmd, cwd=bin_dir, stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL, timeout=300)
    except subprocess.TimeoutExpired:
        return name, False
    if p.returncode != 0 or not os.path.exists(obe):
        return name, False
    with open(obe, "rb") as f:
        image = decode(f.read())
    with open(os.path.join(out, "images", name + ".img"), "wb") as f:
        f.write(image)
    # Small seeds cut from the image: the program's own classes (compiled
    # last) and a few library classes picked by the program's name, so the
    # slices of 800 programs are not 800 copies of the same library class.
    _, classes, _ = obe_image.parse(image)
    n = len(classes)
    pick = random.Random(name)
    cuts = [([n - 1], 2), ([n - 1], 1000), (list(range(max(n - 2, 0), n)), 4),
            (pick.sample(range(n), min(3, n)), 3), (pick.sample(range(n), 1), 1000)]
    for i, (indices, methods) in enumerate(cuts):
        piece = obe_image.slice_image(image, indices, methods, strip_strings=(i != 1))
        with open(os.path.join(out, "slices", f"{name}.{i}.img"), "wb") as f:
            f.write(piece)
    return name, True


def main():
    args = sys.argv[1:]
    jobs = os.cpu_count() or 4
    if "-j" in args:
        i = args.index("-j")
        jobs = int(args[i + 1])
        del args[i:i + 2]
    if len(args) != 2:
        print(__doc__)
        return 2
    bin_dir = os.path.abspath(args[0])
    out = os.path.abspath(args[1])
    obc = os.path.join(bin_dir, "obc" + EXE)
    os.environ["OBJECK_LIB_PATH"] = os.path.join(os.path.dirname(bin_dir), "lib") + os.sep
    os.makedirs(os.path.join(out, "files"), exist_ok=True)
    os.makedirs(os.path.join(out, "images"), exist_ok=True)
    os.makedirs(os.path.join(out, "slices"), exist_ok=True)

    sources = sorted(os.path.join(REGRESSION, n) for n in os.listdir(REGRESSION) if n.endswith(".obs"))
    work = [(s, False) for s in sources] + [(s, True) for s in sources[::4]]
    ok = 0
    with concurrent.futures.ThreadPoolExecutor(jobs) as pool:
        for name, done in pool.map(lambda w: compile_one(obc, bin_dir, w[0], out, w[1]), work):
            ok += done
    print(f"{ok} of {len(work)} compiled into {out}")
    return 0 if ok else 1


if __name__ == "__main__":
    sys.exit(main())
