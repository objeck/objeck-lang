#!/usr/bin/env python3
"""
RQ5 (docs/design/MCP_FIRST_CLASS.md): a program that does not use the feature compiles to the same bytes.

Compiles every .obs of programs/regression, programs/deploy, programs/examples and tools with two compilers, at
-opt s0 and s3, with the file's own library list and with a wide one, and compares the .obe bytes, the compiler's
text and its exit code. Files that mention a tag may differ (the reference has no McpTools); every other file must not.

Usage: corpus_identity.py <repo_root> <reference_bin_dir> <new_bin_dir> <work_dir> [workers]

Each bin_dir holds obc and has a sibling lib/. Give BOTH compilers the same libraries (copy one lib/ next to both
bin directories), or the libraries themselves will differ. The reference is the same source tree with the compiler's
scanner and parser reverted to the commit before the feature. Exit status 1 if any untagged file differs.
"""
import hashlib, os, re, subprocess, sys
from concurrent.futures import ThreadPoolExecutor

EXE = ".exe" if os.name == "nt" else ""
WIDE = "gen_collect,xml,json,cipher,net,net_server,misc,mcp"
TAG = re.compile(r"^\s*@(tool|resource|prompt)\b", re.M)


def libs_own(path):
    libs = "cipher,collect,xml,json"
    try:
        for line in open(path, encoding="utf-8", errors="replace"):
            m = re.match(r"\s*#\s*EXTRA_LIBS:\s*(\S+)", line)
            if m:
                libs += "," + m.group(1)
    except OSError:
        pass
    return libs


def compile_one(bin_dir, tag, work, path, opt, libs, index):
    d = os.path.join(work, tag)
    os.makedirs(d, exist_ok=True)
    dest = os.path.join(d, "%d.obe" % index)
    if os.path.exists(dest):
        os.remove(dest)
    env = dict(os.environ, OBJECK_LIB_PATH=os.path.join(os.path.dirname(bin_dir), "lib"))
    try:
        p = subprocess.run([os.path.join(bin_dir, "obc" + EXE), "-src", path, "-lib", libs, "-opt", opt, "-dest", dest],
                           capture_output=True, env=env, cwd=os.path.dirname(path), timeout=300)
    except subprocess.TimeoutExpired:
        return ("timeout", "", None)
    text = (p.stdout + p.stderr).decode("utf-8", "replace").replace(dest, "DEST")
    digest = hashlib.sha1(open(dest, "rb").read()).hexdigest() if os.path.exists(dest) else None
    return (p.returncode, text, digest)


def main():
    if len(sys.argv) < 5:
        print(__doc__)
        return 2
    root, ref, new, work = sys.argv[1:5]
    workers = int(sys.argv[5]) if len(sys.argv) > 5 else 8
    sources = []
    for d in ("programs/regression", "programs/deploy", "programs/examples", "tools"):
        for base, _, files in os.walk(os.path.join(root, d)):
            if "findings" in base:
                continue
            sources += [os.path.join(base, f) for f in files if f.endswith(".obs")]
    sources.sort()

    jobs = [(i, p, opt, kind, libs_own(p) if kind == "own" else WIDE)
            for i, p in enumerate(sources) for opt in ("s0", "s3") for kind in ("own", "wide")]

    def one(job):
        i, path, opt, kind, libs = job
        k = i * 4 + (0 if opt == "s0" else 2) + (0 if kind == "own" else 1)
        a = compile_one(ref, "ref", work, path, opt, libs, k)
        b = compile_one(new, "new", work, path, opt, libs, k)
        tagged = bool(TAG.search(open(path, encoding="utf-8", errors="replace").read()))
        return path, opt, kind, a == b, a, b, tagged

    with ThreadPoolExecutor(max_workers=workers) as pool:
        results = list(pool.map(one, jobs))

    identical = [r for r in results if r[3]]
    built = [r for r in identical if r[4][0] == 0]
    different = [r for r in results if not r[3]]
    untagged = [r for r in different if not r[6]]
    print("sources %d, compilations %d: identical %d (built and byte-equal %d, same failure %d), different %d (in files with a tag %d, without %d)"
          % (len(sources), len(results), len(identical), len(built), len(identical) - len(built), len(different), len(different) - len(untagged), len(untagged)))
    for r in untagged:
        print("DIFFERENT", r[0], r[1], r[2], r[4][0], r[5][0])
    return 1 if untagged else 0


if __name__ == "__main__":
    sys.exit(main())
