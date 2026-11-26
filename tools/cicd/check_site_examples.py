#!/usr/bin/env python3
"""
check_site_examples.py -- the code samples on objeck.org compile, run, and print what the page shows.

The samples live in docs/site/examples/NAME.obs. Next to each is NAME.out, the console transcript the pages
show (the obc and obr command lines, then the program's output), and docs/site/examples/libs.txt names the
`-lib` list each sample compiles with (`name = lib,lib`). tools/docs/guides_to_html.py builds the snippets on
the pages from these files, so a page cannot show code that was never compiled.

    python tools/cicd/check_site_examples.py                       static: every sample has a .out and a libs line,
                                                                   every {{example:NAME}} on a page names a sample
    python tools/cicd/check_site_examples.py --toolchain DIR       also compile and run each sample with DIR/bin/obc
                                                                   and DIR/bin/obr (DIR/lib holds the libraries) and
                                                                   compare the output with NAME.out
    python tools/cicd/check_site_examples.py --toolchain DIR --update   rewrite the .out files from a real run

Exit 0 = consistent, 1 = something to fix.
"""
import glob
import os
import re
import subprocess
import sys
import tempfile

ROOT = os.path.abspath(os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", ".."))
EX = os.path.join(ROOT, "docs", "site", "examples")


def read_libs():
    libs = {}
    with open(os.path.join(EX, "libs.txt"), encoding="utf-8") as f:
        for ln in f:
            ln = ln.strip()
            if ln and not ln.startswith("#"):
                name, _, val = ln.partition("=")
                libs[name.strip()] = val.strip()
    return libs


def samples():
    return sorted(os.path.basename(p)[:-4] for p in glob.glob(os.path.join(EX, "*.obs")))


def transcript(name, libs, out):
    cmd = "$ obc -src %s.obs%s\n$ obr %s.obe\n" % (name, " -lib " + libs[name] if libs.get(name) else "", name)
    return cmd + out.replace("\r\n", "\n").rstrip("\n") + "\n"


def run(name, libs, toolchain):
    exe = ".exe" if os.name == "nt" else ""
    obc, obr = (os.path.join(toolchain, "bin", t + exe) for t in ("obc", "obr"))
    with tempfile.TemporaryDirectory() as tmp:
        dest = os.path.join(tmp, name + ".obe")
        c = [obc, "-src", os.path.join(EX, name + ".obs"), "-dest", dest]
        if libs.get(name):
            c += ["-lib", libs[name]]
        p = subprocess.run(c, capture_output=True, text=True, timeout=120)
        if p.returncode != 0 or not os.path.exists(dest):
            return None, "compile failed:\n" + p.stdout + p.stderr
        p = subprocess.run([obr, dest], capture_output=True, timeout=120)
        p.stdout = p.stdout.decode("utf-8", "replace").replace(chr(13), "")
        p.stderr = p.stderr.decode("utf-8", "replace")
        if p.returncode != 0:
            return None, "run failed (%d):\n%s%s" % (p.returncode, p.stdout, p.stderr)
        return p.stdout, None


def main():
    args = sys.argv[1:]
    toolchain = args[args.index("--toolchain") + 1] if "--toolchain" in args else None
    update = "--update" in args
    libs = read_libs()
    problems = []
    names = samples()
    for n in names:
        if n not in libs:
            problems.append("%s.obs has no line in libs.txt" % n)
        if not os.path.exists(os.path.join(EX, n + ".out")) and not update:
            problems.append("%s.obs has no %s.out" % (n, n))
    for n in libs:
        if n not in names:
            problems.append("libs.txt names %s, which has no .obs" % n)
    used = set()
    for md in glob.glob(os.path.join(ROOT, "docs", "site", "*.md")):
        with open(md, encoding="utf-8") as f:
            for n in re.findall(r"\{\{example:(\w+)\}\}", f.read()):
                used.add(n)
                if n not in names:
                    problems.append("%s uses {{example:%s}}, which does not exist" % (os.path.basename(md), n))
    idx = os.path.join(ROOT, "docs", "web", "index.html")
    with open(idx, encoding="utf-8") as f:
        for n in re.findall(r"<!-- example:(\w+):start -->", f.read()):
            used.add(n)
            if n not in names:
                problems.append("index.html uses example %s, which does not exist" % n)
    if toolchain:
        for n in names:
            out, err = run(n, libs, toolchain)
            if err:
                problems.append("%s: %s" % (n, err))
                continue
            want = transcript(n, libs, out)
            path = os.path.join(EX, n + ".out")
            have = open(path, encoding="utf-8", newline="").read().replace("\r\n", "\n") if os.path.exists(path) else None
            if have != want:
                if update:
                    with open(path, "w", encoding="utf-8", newline="\n") as f:
                        f.write(want)
                    print("updated", n + ".out")
                else:
                    problems.append("%s.out differs from what the program printed:\n%s" % (n, want))
            else:
                print("ok", n)
    for p in problems:
        print("FAIL:", p)
    print("%d sample(s), %d problem(s)" % (len(names), len(problems)))
    sys.exit(1 if problems else 0)


main()
