#!/usr/bin/env python3
"""Zero-false-refusal gate: compile every program of a tree and one whole-library
program per library with the tree's own toolchain, then load each image with
OBJECK_VERIFY_ONLY=1. Nothing compiled here is run.

It is also the measure that obc and obr count the operand stack alike. obc reports
an expression whose method needs more operand stack than the loader allows
(core/compiler/stack_depth.h), from its own count of the words; a count that ran
high would refuse a program the loader accepts. So each image is compiled and
loaded with OBJECK_STACK_DEPTH_OUT, under which each writes every method's depth,
and the two lists must be equal: every method, the same number, no method obc
could not count.

usage: corpus_gate.py <tree> <deploy dir> <out dir> [jobs]
"""
import collections
import json
import os
import re
import subprocess
import sys
import zlib
from concurrent.futures import ThreadPoolExecutor

tree, deploy, out = sys.argv[1:4]
jobs = int(sys.argv[4]) if len(sys.argv) > 4 else 8
exe = ".exe" if os.name == "nt" else ""
obc = os.path.join(deploy, "bin", "obc" + exe)
obr = os.path.join(deploy, "bin", "obr" + exe)
libdir = os.path.join(deploy, "lib")
env = dict(os.environ, OBJECK_LIB_PATH=libdir)
verify_env = dict(env, OBJECK_VERIFY_ONLY="1")
ALL = ",".join(sorted(f[:-4] for f in os.listdir(libdir) if f.endswith(".obl") and f != "lang.obl"))
sys.path.insert(0, os.path.join(tree, "tools", "fuzz", "loader"))
import obe_image  # noqa: E402

os.makedirs(os.path.join(out, "img"), exist_ok=True)
os.makedirs(os.path.join(out, "cover"), exist_ok=True)
# An out dir that was used before holds images of another tree, numbered by its sources: they
# would be loaded as this run's, and they have no depths to compare.
for sub in ("img", "cover"):
    for name in os.listdir(os.path.join(out, sub)):
        if name.endswith((".obe", ".cdepth", ".rdepth")):
            os.remove(os.path.join(out, sub, name))

# ---- the control: the VM must honour OBJECK_VERIFY_ONLY, or programs would run ----
ctl_src = os.path.join(out, "control.obs")
with open(ctl_src, "w") as f:
    f.write('class Control { function : Main(args : String[]) ~ Nil { "ran"->PrintLine(); } }\n')
ctl = os.path.join(out, "control.obe")
subprocess.run([obc, "-src", ctl_src, "-dest", ctl], capture_output=True, env=env, timeout=120)
p = subprocess.run([obr, ctl], capture_output=True, env=verify_env, timeout=60, stdin=subprocess.DEVNULL)
if p.returncode != 0 or p.stdout.strip() or p.stderr.strip():
    print("control failed: rc=%d out=%r err=%r" % (p.returncode, p.stdout[:100], p.stderr[:200]))
    sys.exit(2)
p = subprocess.run([obr, ctl], capture_output=True, env=env, timeout=60, stdin=subprocess.DEVNULL)
if b"ran" not in p.stdout:
    print("control failed: the program does not run without OBJECK_VERIFY_ONLY")
    sys.exit(2)

# ---- every program in the tree ----
srcs = []
for sub in ("programs", "tools", "docs"):
    for root, _, files in os.walk(os.path.join(tree, sub)):
        srcs += [os.path.join(root, f) for f in files if f.endswith(".obs") and not f.startswith("bad_")]
srcs.sort()


def obc_env(dest):
    """obc writes its count of each method's operand-stack depth beside the image"""
    depth = dest + ".cdepth"
    if os.path.exists(depth):
        os.remove(depth)
    return dict(env, OBJECK_STACK_DEPTH_OUT=depth)


def compile_one(args):
    idx, src = args
    try:
        text = open(src, encoding="utf-8", errors="replace").read()
    except OSError:
        return 0
    libs = "cipher,collect,xml,json"
    m = re.search(r"# EXTRA_LIBS:\s*(\S+)", text)
    if m:
        libs += "," + m.group(1)
    # every level for every program: a pass that only -opt s1 and s2 end with, or that a later
    # level's pass happens to repair, writes an image the other levels never show
    variants = [(opt, ["-opt", opt]) for opt in ("s3", "s0", "s1", "s2")]
    if idx % 4 == 0:
        variants.append(("dbg", ["-debug"]))
    made = 0
    for tag, flags in variants:
        dest = os.path.join(out, "img", "%05d__%s.obe" % (idx, tag))
        for use in (libs, ALL):
            try:
                p = subprocess.run([obc, "-src", src, "-lib", use] + flags + ["-dest", dest], capture_output=True,
                                   timeout=300, env=obc_env(dest))
            except subprocess.TimeoutExpired:
                continue
            if p.returncode == 0 and os.path.exists(dest):
                made += 1
                break
        else:
            # no image: nothing to hold its depths against
            if os.path.exists(dest + ".cdepth"):
                os.remove(dest + ".cdepth")
    return made


with ThreadPoolExecutor(jobs) as ex:
    images = sum(ex.map(compile_one, enumerate(srcs)))
print("sources %d, images %d" % (len(srcs), images))

# ---- one program per library that names every documented class ----
api = json.load(open(os.path.join(tree, "tools", "lsp", "server", "objk_apis.json"), encoding="utf-8"))
by_file = collections.defaultdict(set)
for short, places in api["class-bundle-names"].items():
    for place in places:
        by_file[place["file"]].add(place["bundle"] + "." + short)
documented = linked = 0
for lib, names in sorted(by_file.items()):
    keep = sorted(names)
    dest = None
    for _ in range(60):
        src = os.path.join(out, "cover", "cover_%s.obs" % lib[:-4])
        with open(src, "w") as f:
            f.write("class Cover {\n  function : Main(args : String[]) ~ Nil {\n" +
                    "".join("    v%d : %s;\n" % (i, n) for i, n in enumerate(keep)) + "  }\n}\n")
        ok = True
        for opt in ("s0", "s1", "s2", "s3"):
            dest = src[:-4] + "__%s.obe" % opt
            p = subprocess.run([obc, "-src", src, "-lib", ALL, "-opt", opt, "-dest", dest], capture_output=True,
                               env=obc_env(dest), timeout=900)
            ok = ok and p.returncode == 0
        if ok:
            break
        text = (p.stdout + p.stderr).decode("utf-8", "replace")
        bad = {int(m) for m in re.findall(r"\.obs:\((\d+),", text)}
        if not bad:
            keep = []
            break

        def widen(n):
            if "<" not in n:
                return n + "<System.String>"
            if n.endswith("<System.String>"):
                return n[:-1] + ",System.String>"
            return None
        keep = [n for n in ((widen(n) if (i + 3) in bad else n) for i, n in enumerate(keep)) if n]
    documented += len(names)
    if keep and dest and os.path.exists(dest):
        image = zlib.decompress(open(dest, "rb").read()[4:])
        have = {c["name"] for c in obe_image.parse(image)[1]}
        linked += sum(1 for n in keep if n.split("<")[0] in have)
print("library classes documented %d, linked whole %d" % (documented, linked))
# The whole-library half can stop running without a refusal to show for it: an API index
# whose classes name no library file puts every class in one program, which does not compile.
cover_failed = documented > 0 and linked == 0
if cover_failed:
    print("  NO LIBRARY LINKED WHOLE: the index names %d library file(s) for %d classes; "
          "the libraries' own code was not loaded" % (len([f for f in by_file if f]), documented))

# ---- load every image ----
files = []
for sub in ("img", "cover"):
    files += [os.path.join(out, sub, f) for f in sorted(os.listdir(os.path.join(out, sub))) if f.endswith(".obe")]


def depths(path):
    try:
        with open(path, "rb") as f:
            return sorted(f.read().split(b"\n")[:-1])
    except OSError:
        return None


def load(path):
    loaded = path + ".rdepth"
    if os.path.exists(loaded):
        os.remove(loaded)
    try:
        p = subprocess.run([obr, path], capture_output=True, timeout=300, env=dict(verify_env, OBJECK_STACK_DEPTH_OUT=loaded),
                           stdin=subprocess.DEVNULL)
    except subprocess.TimeoutExpired:
        return path, -1, "TIMEOUT", None
    # the two counts of the image's methods: (methods, lines that differ, a few of them)
    compiled, ran = depths(path + ".cdepth"), depths(loaded)
    count = None
    if p.returncode == 0:
        if compiled is None or ran is None:
            count = (0, 1, ["no depths written by %s" % ("obc" if compiled is None else "obr")])
        else:
            differ = sorted(set(compiled) ^ set(ran))
            unknown = [line for line in compiled if line.endswith(b"\t?")]
            shown = [line.decode("utf-8", "replace") for line in (differ + unknown)[:4]]
            count = (len(ran), len(differ) + len(unknown) + abs(len(compiled) - len(ran)), shown)
    for name in (path + ".cdepth", loaded):
        if os.path.exists(name):
            os.remove(name)
    return path, p.returncode, (p.stdout + p.stderr).decode("utf-8", "replace").strip(), count


refused = 0
rules = collections.Counter()
depth_images = depth_methods = depth_differ = 0
with ThreadPoolExecutor(jobs) as ex:
    for path, rc, text, count in ex.map(load, files):
        if count:
            depth_images += 1
            depth_methods += count[0]
            if count[1]:
                depth_differ += 1
                if depth_differ <= 12:
                    print("  DEPTH DIFFERS", os.path.basename(path), "|", " ; ".join(count[2]).encode("ascii", "replace").decode())
        if rc == 0 and not text:
            continue
        refused += 1
        m = re.search(r": ([^:]*?) <<<", text)
        rules[re.sub(r"\d+", "N", m.group(1)) if m else text[:80]] += 1
        if refused <= 12:
            print("  REFUSED", os.path.basename(path), "|", text[:220].encode("ascii", "replace").decode())
print("images loaded %d, refused or failing %d" % (len(files), refused))
for rule, n in rules.most_common():
    print("%6d  %s" % (n, rule))
print("operand-stack depth: %d images, %d methods counted by obc and by obr, %d images where the counts differ" %
      (depth_images, depth_methods, depth_differ))
sys.exit(1 if refused or cover_failed or depth_differ or not depth_images else 0)
