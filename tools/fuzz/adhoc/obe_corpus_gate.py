#!/usr/bin/env python3
"""Zero-false-refusal gate: compile every program of a tree and one whole-library
program per library with the tree's own toolchain, then load each image with
OBJECK_VERIFY_ONLY=1. Nothing compiled here is run.

usage: obe_corpus_gate.py <tree> <deploy dir> <out dir> [jobs]
           [--expected-refusals FILE] [--exclusions FILE] [--ledger FILE]

Nothing is allowed to go unaccounted. Every (source, configuration) pair is attempted, and ends as
exactly one of

  loaded            compiled, then loaded and verified by the VM
  expected refusal  the compiler refused it, and obe_corpus_expected_refusals.json lists that source
                    (a glob) with that refusal text class (the first error, with names and numbers
                    abstracted) and a reason
  excluded          named in obe_corpus_exclusions.json with a reason; not attempted

and every library's whole-library program likewise (loaded for each -opt level, or excluded by
name). The run FAILS on: a loader refusal of anything the compiler wrote, a pair that is none of the
three (a compile that failed unannounced, a timeout, a missing image), a .obe left in the output
directory that this run did not write (the directory is deleted first, and only if it carries this
gate's marker or is empty), an image that was not loaded, and a library with no classes linked.
It prints attempted and completed counts and writes the whole ledger to <out>/ledger.json.

It is also the measure that obc and obr count the operand stack alike. obc reports an
expression whose method needs more operand stack than the loader allows
(core/compiler/stack_depth.h), from its own count of the words; a count that ran
high would refuse a program the loader accepts. So each image is compiled and
loaded with OBJECK_STACK_DEPTH_OUT, under which each writes every method's depth,
and the two lists must be equal: every method, the same number, no method obc
could not count.
"""
import argparse
import collections
import fnmatch
import json
import os
import re
import shutil
import subprocess
import sys
import zlib
from concurrent.futures import ThreadPoolExecutor

HERE = os.path.dirname(os.path.abspath(__file__))
DEFAULT_REFUSALS = os.path.join(HERE, "obe_corpus_expected_refusals.json")
DEFAULT_EXCLUSIONS = os.path.join(HERE, "obe_corpus_exclusions.json")
MARKER = ".obe_corpus_gate"
OPTS = ("s3", "s0", "s1", "s2")


def refusal_class(text):
    """The compiler's first error, with names and numbers abstracted, so one cause is one class."""
    for line in text.splitlines():
        line = line.strip()
        if not line:
            continue
        line = re.sub(r"^.*?\.obs:\(\d+,\d+\):\s*", "", line)
        line = re.sub(r"'[^']*'", "'X'", line)
        line = re.sub(r"\d+", "N", line)
        return line[:160]
    return "(no output)"


def load_entries(path, kind):
    if not path or not os.path.isfile(path):
        return []
    with open(path, encoding="utf-8") as f:
        data = json.load(f)
    if data.get("format") != 1 or not isinstance(data.get("entries"), list):
        raise SystemExit("%s: not a format-1 %s file" % (path, kind))
    for i, e in enumerate(data["entries"]):
        if not e.get("reason"):
            raise SystemExit("%s: entry %d has no reason" % (path, i))
        if not (e.get("source") or e.get("library")):
            raise SystemExit("%s: entry %d names no source or library" % (path, i))
        if kind == "expected refusals" and not e.get("refusal"):
            raise SystemExit("%s: entry %d has no refusal text class" % (path, i))
    return data["entries"]


def tag_matches(entry, tag):
    return any(fnmatch.fnmatchcase(tag, t) for t in entry.get("tags", ["*"]))


def excluded_source(exclusions, rel, tag):
    for e in exclusions:
        if e.get("source") and fnmatch.fnmatchcase(rel, e["source"]) and tag_matches(e, tag):
            return e
    return None


def excluded_library(exclusions, lib):
    for e in exclusions:
        if e.get("library") and fnmatch.fnmatchcase(lib, e["library"]):
            return e
    return None


def expected_refusal(refusals, rel, tag, cls):
    for e in refusals:
        if e.get("source") and fnmatch.fnmatchcase(rel, e["source"]) and tag_matches(e, tag) \
                and (cls == e["refusal"] or fnmatch.fnmatchcase(cls, e["refusal"])):
            return e
    return None


def fresh_out_dir(path, protected):
    """A FRESH output directory: images left by an earlier run would be loaded as if this run made
    them. An existing one is deleted only when it is empty or carries this gate's marker."""
    path = os.path.abspath(path)
    home = os.path.abspath(os.path.expanduser("~"))
    forbidden = {os.path.abspath(os.sep), home} | {os.path.abspath(p) for p in protected}
    if path in forbidden or os.path.dirname(path) == path:
        raise SystemExit("refusing to use %s as the output directory" % path)
    if os.path.isdir(path) and os.listdir(path):
        if not os.path.isfile(os.path.join(path, MARKER)):
            raise SystemExit("%s is not empty and was not made by this gate (no %s marker): refusing to delete it; "
                             "give a new or empty directory" % (path, MARKER))
        shutil.rmtree(path)
    os.makedirs(path, exist_ok=True)
    with open(os.path.join(path, MARKER), "w") as f:
        f.write("made by obe_corpus_gate.py; deleted at the start of the next run\n")
    stale = [os.path.join(r, f) for r, _, fs in os.walk(path) for f in fs if f.endswith(".obe")]
    if stale:
        raise SystemExit("stale .obe files in a fresh directory: %s" % stale[:3])


def obc_env(env, dest):
    """obc writes its count of each method's operand-stack depth beside the image"""
    depth = dest + ".cdepth"
    if os.path.exists(depth):
        os.remove(depth)
    return dict(env, OBJECK_STACK_DEPTH_OUT=depth)


def depths(path):
    try:
        with open(path, "rb") as f:
            return sorted(f.read().split(b"\n")[:-1])
    except OSError:
        return None


def variants_of(rel):
    # every level for every program: a pass that only -opt s1 and s2 end with, or that a later
    # level's pass happens to repair, writes an image the other levels never show. A quarter of the
    # sources also get -debug, picked by a hash of the path so adding a file moves no other.
    v = [(opt, ["-opt", opt]) for opt in OPTS]
    if zlib.crc32(rel.encode()) % 4 == 0:
        v.append(("dbg", ["-debug"]))
    return v


def classify_rows(rows):
    """Counts and the unaccounted rows: a pair is accounted for only as loaded, an expected refusal
    or an exclusion."""
    count = collections.Counter(r["outcome"] for r in rows)
    unaccounted = [r for r in rows if r["outcome"] not in ("loaded", "expected refusal", "excluded")]
    return count, unaccounted


def main():
    ap = argparse.ArgumentParser(description=__doc__.split("\n")[0], formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("tree")
    ap.add_argument("deploy")
    ap.add_argument("out")
    ap.add_argument("jobs", nargs="?", type=int, default=8)
    ap.add_argument("--expected-refusals", default=DEFAULT_REFUSALS)
    ap.add_argument("--exclusions", default=DEFAULT_EXCLUSIONS)
    ap.add_argument("--ledger", default=None, help="where to write the ledger (default <out>/ledger.json)")
    ap.add_argument("--sources", default=None,
                    help="limit to sources whose tree-relative path matches this glob (a trial run; the full "
                         "gate is run without it)")
    args = ap.parse_args()
    tree, deploy, out, jobs = args.tree, args.deploy, os.path.abspath(args.out), args.jobs
    refusals = load_entries(args.expected_refusals, "expected refusals")
    exclusions = load_entries(args.exclusions, "exclusions")

    exe = ".exe" if os.name == "nt" else ""
    obc = os.path.join(deploy, "bin", "obc" + exe)
    obr = os.path.join(deploy, "bin", "obr" + exe)
    libdir = os.path.join(deploy, "lib")
    env = dict(os.environ, OBJECK_LIB_PATH=libdir)
    verify_env = dict(env, OBJECK_VERIFY_ONLY="1")
    ALL = ",".join(sorted(f[:-4] for f in os.listdir(libdir) if f.endswith(".obl") and f != "lang.obl"))
    sys.path.insert(0, os.path.join(tree, "tools", "fuzz", "loader"))
    import obe_image

    fresh_out_dir(out, [tree, deploy])
    os.makedirs(os.path.join(out, "img"))
    os.makedirs(os.path.join(out, "cover"))

    # ---- the control: the VM must honour OBJECK_VERIFY_ONLY, or programs would run ----
    ctl_src = os.path.join(out, "control.obs")
    with open(ctl_src, "w") as f:
        f.write('class Control { function : Main(args : String[]) ~ Nil { "ran"->PrintLine(); } }\n')
    ctl = os.path.join(out, "control.obe")
    subprocess.run([obc, "-src", ctl_src, "-dest", ctl], capture_output=True, env=env, timeout=120)
    p = subprocess.run([obr, ctl], capture_output=True, env=verify_env, timeout=60, stdin=subprocess.DEVNULL)
    if p.returncode != 0 or p.stdout.strip() or p.stderr.strip():
        print("control failed: rc=%d out=%r err=%r" % (p.returncode, p.stdout[:100], p.stderr[:200]))
        return 2
    p = subprocess.run([obr, ctl], capture_output=True, env=env, timeout=60, stdin=subprocess.DEVNULL)
    if b"ran" not in p.stdout:
        print("control failed: the program does not run without OBJECK_VERIFY_ONLY")
        return 2
    os.remove(ctl)  # the control is not part of the corpus: only the images this gate accounts for stay

    # ---- every program in the tree ----
    srcs = []
    for sub in ("programs", "tools", "docs"):
        for root, _, files in os.walk(os.path.join(tree, sub)):
            srcs += [os.path.join(root, f) for f in files if f.endswith(".obs") and not f.startswith("bad_")]
    srcs.sort()
    rel_of = {s: os.path.relpath(s, tree).replace(os.sep, "/") for s in srcs}
    if args.sources:
        srcs = [s for s in srcs if fnmatch.fnmatchcase(rel_of[s], args.sources)]
    if not srcs:
        print("no .obs sources under %s/{programs,tools,docs}: nothing to gate" % tree)
        return 2

    def compile_pair(item):
        idx, src, tag, flags = item
        rel = rel_of[src]
        row = {"kind": "source", "source": rel, "tag": tag}
        ex = excluded_source(exclusions, rel, tag)
        if ex:
            row.update(outcome="excluded", reason=ex["reason"])
            return row
        try:
            with open(src, encoding="utf-8", errors="replace") as f:
                text = f.read()
        except OSError as e:
            row.update(outcome="unaccounted", detail="unreadable: %s" % e)
            return row
        libs = "cipher,collect,xml,json"
        m = re.search(r"# EXTRA_LIBS:\s*(\S+)", text)
        if m:
            libs += "," + m.group(1)
        dest = os.path.join(out, "img", "%05d__%s.obe" % (idx, tag))
        last = ""
        for use in (libs, ALL):
            try:
                p = subprocess.run([obc, "-src", src, "-lib", use] + flags + ["-dest", dest], capture_output=True,
                                   timeout=300, env=obc_env(env, dest))
            except subprocess.TimeoutExpired:
                row.update(outcome="unaccounted", detail="the compiler timed out (300 s) with -lib %s" % use[:40])
                return row
            if p.returncode == 0 and os.path.exists(dest):
                row.update(outcome="compiled", image=os.path.relpath(dest, out).replace(os.sep, "/"))
                return row
            if p.returncode == 0:
                row.update(outcome="unaccounted", detail="the compiler exited 0 and wrote no image")
                return row
            last = (p.stdout + p.stderr).decode("utf-8", "replace")
        if os.path.exists(dest + ".cdepth"):
            os.remove(dest + ".cdepth")  # no image: nothing to hold its depths against
        cls = refusal_class(last)
        e = expected_refusal(refusals, rel, tag, cls)
        if e:
            row.update(outcome="expected refusal", refusal=cls, reason=e["reason"])
        else:
            row.update(outcome="unaccounted", refusal=cls,
                       detail="the compiler refused it and no expected-refusals entry lists this source with this refusal class")
        return row

    work = []
    for idx, src in enumerate(srcs):
        for tag, flags in variants_of(rel_of[src]):
            work.append((idx, src, tag, flags))
    with ThreadPoolExecutor(jobs) as ex:
        ledger = list(ex.map(compile_pair, work))
    n_src_pairs = len(work)
    print("sources %d, (source, configuration) pairs %d, images %d" %
          (len(srcs), n_src_pairs, sum(1 for r in ledger if r["outcome"] == "compiled")))

    # ---- one program per library that names every documented class ----
    with open(os.path.join(tree, "tools", "lsp", "server", "objk_apis.json"), encoding="utf-8") as f:
        api = json.load(f)
    by_file = collections.defaultdict(set)
    for short, places in api["class-bundle-names"].items():
        for place in places:
            by_file[place["file"]].add(place["bundle"] + "." + short)
    documented = linked = 0
    cover_rows = []
    for lib, names in sorted(by_file.items()):
        row = {"kind": "library", "library": lib, "documented": len(names), "linked": 0, "images": []}
        documented += len(names)
        ex = excluded_library(exclusions, lib)
        if ex:
            row.update(outcome="excluded", reason=ex["reason"])
            cover_rows.append(row)
            continue
        keep = sorted(names)
        made = {}
        for _ in range(60):
            src = os.path.join(out, "cover", "cover_%s.obs" % lib[:-4])
            with open(src, "w") as f:
                f.write("class Cover {\n  function : Main(args : String[]) ~ Nil {\n" +
                        "".join("    v%d : %s;\n" % (i, n) for i, n in enumerate(keep)) + "  }\n}\n")
            made = {}
            ok = True
            for opt in OPTS:
                dest = src[:-4] + "__%s.obe" % opt
                p = subprocess.run([obc, "-src", src, "-lib", ALL, "-opt", opt, "-dest", dest], capture_output=True,
                                   env=obc_env(env, dest), timeout=900)
                ok = ok and p.returncode == 0
                if p.returncode == 0 and os.path.exists(dest):
                    made[opt] = dest
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
        if keep and len(made) == len(OPTS):
            with open(made["s3"], "rb") as f:
                image = zlib.decompress(f.read()[4:])
            have = {c["name"] for c in obe_image.parse(image)[1]}
            row["linked"] = sum(1 for n in keep if n.split("<")[0] in have)
            linked += row["linked"]
            row.update(outcome="compiled", images=[os.path.relpath(d, out).replace(os.sep, "/") for d in made.values()])
        else:
            # a library whose whole-library program does not compile at every level was not loaded whole
            row.update(outcome="unaccounted",
                       detail="its whole-library program did not compile at all of %s (%d of %d classes kept)"
                       % ("/".join(OPTS), len(keep), len(names)))
            for d in made.values():
                os.remove(d)
        cover_rows.append(row)
    print("library classes documented %d, linked whole %d" % (documented, linked))
    for r in cover_rows:
        if r["outcome"] == "compiled" and r["linked"] == 0:
            r.update(outcome="unaccounted", detail="no documented class of it linked into its whole-library program")
    if documented > 0 and linked == 0:
        print("  NO LIBRARY LINKED WHOLE: the index names %d library file(s) for %d classes; "
              "the libraries' own code was not loaded" % (len([f for f in by_file if f]), documented))

    # ---- every image on disk must be one this run wrote, and every one is loaded ----
    expected_images = {r["image"] for r in ledger if r["outcome"] == "compiled"}
    for r in cover_rows:
        expected_images.update(r.get("images", []))
    on_disk = set()
    for sub in ("img", "cover"):
        for f in os.listdir(os.path.join(out, sub)):
            if f.endswith(".obe"):
                on_disk.add("%s/%s" % (sub, f))
    stale = sorted(on_disk - expected_images)
    missing = sorted(expected_images - on_disk)

    def load(rel):
        path = os.path.join(out, rel)
        loaded_depth = path + ".rdepth"
        if os.path.exists(loaded_depth):
            os.remove(loaded_depth)
        try:
            p = subprocess.run([obr, path], capture_output=True, timeout=300, stdin=subprocess.DEVNULL,
                               env=dict(verify_env, OBJECK_STACK_DEPTH_OUT=loaded_depth))
        except subprocess.TimeoutExpired:
            return rel, -1, "TIMEOUT", None
        # the two counts of the image's methods: (methods, lines that differ, a few of them)
        compiled, ran = depths(path + ".cdepth"), depths(loaded_depth)
        count = None
        if p.returncode == 0:
            if compiled is None or ran is None:
                count = (0, 1, ["no depths written by %s" % ("obc" if compiled is None else "obr")])
            else:
                differ = sorted(set(compiled) ^ set(ran))
                unknown = [line for line in compiled if line.endswith(b"\t?")]
                shown = [line.decode("utf-8", "replace") for line in (differ + unknown)[:4]]
                count = (len(ran), len(differ) + len(unknown) + abs(len(compiled) - len(ran)), shown)
        for name in (path + ".cdepth", loaded_depth):
            if os.path.exists(name):
                os.remove(name)
        return rel, p.returncode, (p.stdout + p.stderr).decode("utf-8", "replace").strip(), count

    to_load = sorted(expected_images & on_disk)
    loaded = {}
    refused = 0
    rules = collections.Counter()
    depth_images = depth_methods = depth_differ = 0
    with ThreadPoolExecutor(jobs) as ex:
        for rel, rc, text, count in ex.map(load, to_load):
            loaded[rel] = (rc, text)
            if count:
                depth_images += 1
                depth_methods += count[0]
                if count[1]:
                    depth_differ += 1
                    if depth_differ <= 12:
                        print("  DEPTH DIFFERS", os.path.basename(rel), "|", " ; ".join(count[2]).encode("ascii", "replace").decode())
            if rc == 0 and not text:
                continue
            refused += 1
            m = re.search(r": ([^:]*?) <<<", text)
            rules[re.sub(r"\d+", "N", m.group(1)) if m else text[:80]] += 1
            if refused <= 12:
                print("  REFUSED", os.path.basename(rel), "|", text[:220].encode("ascii", "replace").decode())
    print("images loaded %d, refused or failing %d" % (len(to_load), refused))
    for rule, n in rules.most_common():
        print("%6d  %s" % (n, rule))
    print("operand-stack depth: %d images, %d methods counted by obc and by obr, %d images where the counts differ" %
          (depth_images, depth_methods, depth_differ))

    # ---- account for every pair ----
    def clean(rel):
        rc, text = loaded.get(rel, (None, "not loaded"))
        return rc == 0 and not text

    for r in ledger:
        if r["outcome"] == "compiled":
            r["outcome"] = "loaded" if clean(r["image"]) else "loader refused"
            if r["outcome"] == "loader refused":
                r["detail"] = loaded.get(r["image"], (None, "not loaded"))[1][:200]
    for r in cover_rows:
        if r["outcome"] == "compiled":
            bad = [i for i in r["images"] if not clean(i)]
            r["outcome"] = "loader refused" if bad else "loaded"
            if bad:
                r["detail"] = "refused: " + ", ".join(os.path.basename(b) for b in bad)
    rows = ledger + cover_rows
    count, unaccounted = classify_rows(rows)
    attempted = len(rows)
    accounted = count["loaded"] + count["expected refusal"] + count["excluded"]
    ledger_path = args.ledger or os.path.join(out, "ledger.json")
    with open(ledger_path, "w", encoding="utf-8") as f:
        json.dump({"attempted": attempted, "accounted": accounted, "counts": dict(count), "rows": rows,
                   "stale_images": stale, "missing_images": missing}, f, indent=1)
    print("pairs attempted %d (%d source configurations + %d libraries), completed %d: loaded %d, expected compile "
          "refusal %d, excluded %d, UNACCOUNTED %d" % (attempted, n_src_pairs, len(cover_rows), accounted, count["loaded"],
                                                       count["expected refusal"], count["excluded"], len(unaccounted)))
    for cls, n in collections.Counter(r["refusal"] for r in rows if r["outcome"] == "expected refusal").most_common(10):
        print("  expected refusal %5d  %s" % (n, cls))
    for r in unaccounted[:20]:
        print("  UNACCOUNTED %s %s [%s]: %s" % (r.get("source") or r.get("library"), r.get("tag", ""), r["outcome"],
                                                r.get("detail", "")[:200]))
    if stale:
        print("  STALE images in the output directory that this run did not write: %s" % ", ".join(stale[:5]))
    if missing:
        print("  MISSING images this run recorded and that are not on disk: %s" % ", ".join(missing[:5]))
    failed = bool(unaccounted or stale or missing or refused or depth_differ or not depth_images
                  or (documented > 0 and linked == 0))
    print("verdict: %s" % ("FAIL" if failed else "PASS"))
    return 1 if failed else 0


if __name__ == "__main__":
    sys.exit(main())
