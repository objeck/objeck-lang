#!/usr/bin/env python3
"""
RQ5 and the scanner gate (docs/design/MCP_FIRST_CLASS.md): the doc-comment tags must not change what a program
without a tag compiles to, and no input may crash or hang the compiler.

Mutates real sources (truncate, delete or duplicate a slice, insert a tag or a nested '#~ #~ @tool ~#' anywhere, CRLF,
Unicode, an unterminated comment, a tag in a string, resource and prompt tags) and compiles every mutant with the
reference compiler and, twice, with the new one. A mutant without a tag must give exactly the reference's bytes, text
and exit code; no mutant may crash, hang, or answer differently on its second compile. A mutant the reference itself
answers differently from run to run is counted apart (a pre-existing uninitialised read, not this change).

Usage: scanner_fuzz.py <repo_root> <reference_bin_dir> <new_bin_dir> <work_dir> [seed=1] [mutants=600] [workers=12]

Both bin dirs need the same sibling lib/ contents (the mcp library must be there). Exit status 1 on any problem.
"""
import hashlib, os, random, subprocess, sys
from concurrent.futures import ThreadPoolExecutor

EXE = ".exe" if os.name == "nt" else ""
LIBS = "gen_collect,xml,json,cipher,net,net_server,misc,mcp,collect"
NL, BS = "\n", chr(92)


def mutate(rnd, text):
    kind = rnd.choice(["trunc", "del", "dup", "tag", "crlf", "uni", "nest", "tagfn", "strtag", "unterm", "res", "prm", "resnest"])
    n = len(text)
    a = rnd.randrange(max(1, n))
    b = min(n, a + rnd.randrange(1, 200))
    if kind == "trunc":
        return kind, text[:a]
    if kind == "del":
        return kind, text[:a] + text[b:]
    if kind == "dup":
        return kind, text[:b] + text[a:b] + text[b:]
    if kind == "tag":
        return kind, text[:a] + NL + "#~" + NL + "@tool" + NL + "~#" + NL + text[a:]
    if kind == "crlf":
        return kind, text.replace(NL, "\r\n")
    if kind == "uni":
        return kind, text[:a] + "\u00fc\u4e16\U0001F600" + text[a:]
    if kind == "nest":
        return kind, text[:a] + NL + "#~ #~ @tool ~#" + NL + text[a:]
    if kind == "strtag":
        return kind, text[:a] + '"@tool #~ ~#"' + text[a:]
    if kind == "res":
        return kind, text[:a] + NL + "#~" + NL + "@resource a://{x}" + NL + "~#" + NL + text[a:]
    if kind == "prm":
        return kind, text[:a] + NL + "#~" + NL + "@prompt" + NL + "~#" + NL + text[a:]
    if kind == "resnest":
        return kind, text[:a] + NL + "#~ #~ @resource x ~#" + NL + "@prompt ~#" + NL + text[a:]
    if kind == "unterm":
        return kind, text[:a] + NL + "#~" + NL + "@tool" + NL + text[a:]
    return kind, text.replace("function :", "#~" + NL + "@tool" + NL + "~#" + NL + "function :", 1)


def main():
    if len(sys.argv) < 5:
        print(__doc__)
        return 2
    root, ref, new, work = sys.argv[1:5]
    seed = int(sys.argv[5]) if len(sys.argv) > 5 else 1
    count = int(sys.argv[6]) if len(sys.argv) > 6 else 600
    workers = int(sys.argv[7]) if len(sys.argv) > 7 else 12
    rnd = random.Random(seed)
    os.makedirs(work, exist_ok=True)

    pool = []
    for d in ("programs/regression", "programs/deploy"):
        for f in sorted(os.listdir(os.path.join(root, d))):
            p = os.path.join(root, d, f)
            if f.endswith(".obs") and 300 < os.path.getsize(p) < 12000:
                pool.append(p)
    tagged = [os.path.join(root, "programs", "deploy", f) for f in sorted(os.listdir(os.path.join(root, "programs", "deploy"))) if f.startswith("mcp_") and f.endswith(".obs")]

    def compile_it(bin_dir, tag, path, index):
        env = dict(os.environ, OBJECK_LIB_PATH=os.path.join(os.path.dirname(bin_dir), "lib"))
        dest = os.path.join(work, "%s_%d.obe" % (tag, index))
        if os.path.exists(dest):
            os.remove(dest)
        try:
            p = subprocess.run([os.path.join(bin_dir, "obc" + EXE), "-src", path, "-lib", LIBS, "-dest", dest], capture_output=True, env=env, timeout=60)
        except subprocess.TimeoutExpired:
            return ("HANG",)
        digest = hashlib.sha1(open(dest, "rb").read()).hexdigest() if os.path.exists(dest) else None
        return (p.returncode, (p.stdout + p.stderr).decode("utf-8", "replace").replace(dest, "D").replace(path, "P"), digest)

    cases = []
    for i in range(count):
        src = rnd.choice(tagged if i % 5 == 0 and tagged else pool)
        kind, text = mutate(rnd, open(src, encoding="utf-8", errors="replace").read())
        path = os.path.join(work, "m%d.obs" % i)
        open(path, "w", encoding="utf-8", newline="").write(text)
        cases.append((i, kind, path, any(t in text for t in ("@tool", "@resource", "@prompt"))))

    def one(case):
        i, kind, path, tagged_mutant = case
        return (i, kind, tagged_mutant, compile_it(ref, "ref", path, i), compile_it(ref, "ref", path, i + 200000),
                compile_it(new, "new", path, i), compile_it(new, "new", path, i + 100000))

    with ThreadPoolExecutor(max_workers=workers) as ex:
        results = list(ex.map(one, cases))

    problems = pre_existing = 0
    for i, kind, tagged_mutant, a, a2, b, b2 in results:
        crashed = b[0] == "HANG" or (isinstance(b[0], int) and (b[0] < 0 or b[0] > 255))
        if b != b2 and a != a2:
            pre_existing += 1
        elif b != b2:
            print("DIFFERENT BETWEEN TWO RUNS", i, kind)
            problems += 1
        elif crashed:
            print("CRASH OR HANG", i, kind, b[0])
            problems += 1
        elif not tagged_mutant and a != b:
            print("DIFFERENT FROM THE REFERENCE (no tag)", i, kind, a[0], b[0])
            problems += 1
    untagged = sum(1 for r in results if not r[2])
    print("mutants %d (without a tag %d, with %d), problems %d, nondeterministic in the reference too %d" % (len(results), untagged, len(results) - untagged, problems, pre_existing))
    return 1 if problems else 0


if __name__ == "__main__":
    sys.exit(main())
