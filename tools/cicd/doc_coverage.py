#!/usr/bin/env python3
"""
doc_coverage.py -- how much of the standard library's public API has a doc comment.

Reads core/compiler/lib_src/*.obs and counts, per library, the public classes,
interfaces, enums, consts, methods, functions and constructors that carry a
`#~ ... ~#` doc comment immediately before them. It changes nothing; it is a
measurement, not a gate.

    python tools/cicd/doc_coverage.py                 print the report (Markdown)
    python tools/cicd/doc_coverage.py --write docs/DOC_COVERAGE.md
    python tools/cicd/doc_coverage.py --json          the same numbers as JSON
    python tools/cicd/doc_coverage.py --self-test     check the counter on a small sample

What counts as public follows the compiler (core/compiler/parser.cpp, ParseClass and
ParseMethod) and code_doc (core/lib/code_doc/doc_parser.obs):

  * a class or interface is public unless declared `class : private : Name`;
  * `function` is public unless declared `function : private :`;
  * `method` is PRIVATE unless declared `method : public :`;
  * `New` is public unless declared `New : private (...)`;
  * enums and consts count, except inside a private class (code_doc skips those);
  * everything inside a private class is skipped (code_doc lists no private class).

A "library" is one .obl, i.e. the set of lib_src files core/compiler/build_libs.sh
passes to one `obc ... -dest ../lib/<name>.obl` (lang.obl is built from lang.obs by the
bootstrap compiler and is added by hand).

What this does not do: it does not check that a comment is accurate, that its @param
tags name the real parameters, or that the comment parses (tools/cicd/check_doc_fences.py
and code_doc's own ERROR lines cover parsing). A comment placed before the wrong
declaration counts for that declaration.
"""
import argparse
import collections
import json
import os
import re
import subprocess
import sys

ROOT = os.path.abspath(os.path.join(os.path.dirname(__file__), "..", ".."))
LIB_SRC = os.path.join(ROOT, "core", "compiler", "lib_src")
BUILD_LIBS = os.path.join(ROOT, "core", "compiler", "build_libs.sh")

KINDS = ("class", "enum", "method", "function", "constructor")


def mask(text):
    """Return (masked, doc_ends).

    masked has the same length as text, with comments, string literals and character
    literals blanked (newlines kept), so braces and keywords inside them are invisible.
    doc_ends is the set of offsets one past the closing `~#` of every doc comment.
    The file header `#~~ ... ~~#` is a comment but not a doc comment.
    """
    out = list(text)
    doc_ends = set()
    i, n = 0, len(text)

    def blank(a, b):
        for k in range(a, b):
            if out[k] != "\n":
                out[k] = " "

    while i < n:
        c = text[i]
        if c == "#":
            if text.startswith("#~~", i):
                # the scanner ends any `#~` comment at the first `~#`, so a header
                # written `#~~ ... ~#` ends there as well as one written `... ~~#`
                j = text.find("~#", i + 3)
                j = n if j < 0 else j + 2
                blank(i, j)
                i = j
            elif text.startswith("#~", i):
                j = text.find("~#", i + 2)
                j = n if j < 0 else j + 2
                blank(i, j)
                doc_ends.add(j)
                i = j
            else:
                j = text.find("\n", i)
                j = n if j < 0 else j
                blank(i, j)
                i = j
        elif c == '"':
            j = i + 1
            while j < n and text[j] != '"':
                j += 2 if text[j] == "\\" else 1
            blank(i, min(j + 1, n))
            i = j + 1
        elif c == "'":
            j = i + 1
            while j < n and text[j] != "'" and text[j] != "\n":
                j += 2 if text[j] == "\\" else 1
            blank(i, min(j + 1, n))
            i = j + 1
        else:
            i += 1
    return "".join(out), doc_ends


CLASS_RE = re.compile(r"(?<![\w.])(class|interface)\s*((?::\s*(?:public|private)\s*:\s*)*)(\$?[A-Za-z_][\w.]*)")
ENUM_RE = re.compile(r"(?<![\w.])(enum|consts)\s*((?::\s*(?:public|private)\s*:\s*)*)([A-Za-z_]\w*)")
MEMBER_RE = re.compile(
    r"(?<![\w.@$])(function|method)\s*((?::\s*(?:public|private|virtual|native)\s*)*):\s*([A-Za-z_]\w*)"
)
NEW_RE = re.compile(r"(?<![\w.@$])New\s*(?::\s*(private|public)\s*)?\(")


def documented(text, doc_ends, pos):
    """True when only whitespace separates the end of a doc comment from pos."""
    k = pos
    while k > 0 and text[k - 1] in " \t\r\n":
        k -= 1
    return k in doc_ends


def starts_decl(masked, pos):
    """A declaration follows a '{', '}' or ';' (comments are blanked), or begins the file."""
    k = pos
    while k > 0 and masked[k - 1] in " \t\r\n":
        k -= 1
    return k == 0 or masked[k - 1] in "{};"


def scan(text):
    """Yield (kind, owner_class, name, is_documented, line) for each public declaration."""
    masked, doc_ends = mask(text)
    # a stack of (kind, name, is_private, depth_of_body); depth counts '{'
    stack = []
    depth = 0
    items = []
    # positions of declarations, in order, to merge with brace walking
    decls = []
    for m in CLASS_RE.finditer(masked):
        if not starts_decl(masked, m.start()):
            continue
        decls.append((m.start(), "class", m.group(3).lstrip("$"), "private" in m.group(2), m))
    for m in ENUM_RE.finditer(masked):
        if not starts_decl(masked, m.start()):
            continue
        decls.append((m.start(), "enum", m.group(3), "private" in m.group(2), m))
    for m in MEMBER_RE.finditer(masked):
        if not starts_decl(masked, m.start()):
            continue
        flags = m.group(2)
        is_function = m.group(1) == "function"
        private = ("private" in flags) if is_function else ("public" not in flags)
        decls.append((m.start(), "function" if is_function else "method", m.group(3), private, m))
    for m in NEW_RE.finditer(masked):
        if not starts_decl(masked, m.start()):
            continue
        decls.append((m.start(), "constructor", "New", m.group(1) == "private", m))
    decls.sort(key=lambda d: d[0])

    # walk braces; at each declaration decide whether it belongs to the class on top
    pending = None  # a class/enum whose '{' has not been seen yet
    di = 0
    i, n = 0, len(masked)
    while i < n:
        while di < len(decls) and decls[di][0] <= i:
            pos, kind, name, private, m = decls[di]
            di += 1
            top = stack[-1] if stack else None
            in_class_body = top is not None and top["kind"] == "class" and depth == top["depth"]
            line = text.count("\n", 0, pos) + 1
            if kind in ("class", "enum"):
                owner_private = any(s["private"] for s in stack if s["kind"] == "class")
                counted = not private and not owner_private
                if counted:
                    items.append(("class" if kind == "class" else "enum", top["name"] if in_class_body else "",
                                  name, documented(text, doc_ends, pos), line))
                pending = {"kind": kind, "name": name, "private": private}
            elif in_class_body and not top["private"]:
                if not private:
                    items.append((kind, top["name"], name, documented(text, doc_ends, pos), line))
        c = masked[i]
        if c == "{":
            depth += 1
            if pending is not None:
                pending["depth"] = depth
                stack.append(pending)
                pending = None
        elif c == "}":
            while stack and stack[-1]["depth"] == depth:
                stack.pop()
            depth -= 1
        elif c == ";" and pending is not None and pending["kind"] == "class":
            pending = None  # a forward mention, not a body
        i += 1
    return items


def library_map():
    """{lib name: [source file names]} from build_libs.sh, plus lang."""
    libs = collections.OrderedDict()
    libs["lang"] = ["lang.obs"]
    text = open(BUILD_LIBS, encoding="utf-8").read()
    for line in text.splitlines():
        m = re.search(r"-src\s+(\S+).*-dest\s+\.\./lib/(\w+)\.obl", line)
        if not m:
            continue
        files = [os.path.basename(p) for p in m.group(1).split(",")]
        libs[m.group(2)] = files
    return libs


def measure():
    libs = library_map()
    on_disk = sorted(f for f in os.listdir(LIB_SRC) if f.endswith(".obs"))
    mapped = {f for files in libs.values() for f in files}
    unmapped = [f for f in on_disk if f not in mapped]
    result = collections.OrderedDict()
    classes = collections.OrderedDict()
    for lib, files in libs.items():
        counts = {k: [0, 0] for k in KINDS}  # kind -> [total, documented]
        for f in files:
            path = os.path.join(LIB_SRC, f)
            if not os.path.exists(path):
                continue
            text = open(path, encoding="utf-8-sig").read()
            for kind, owner, name, doc, line in scan(text):
                counts[kind][0] += 1
                counts[kind][1] += 1 if doc else 0
                if not doc:
                    key = (lib, f, owner or "(bundle level)")
                    classes.setdefault(key, []).append((kind, name, line))
        result[lib] = counts
    return result, classes, unmapped


def pct(doc, total):
    return "-" if total == 0 else "%d%%" % round(100.0 * doc / total)


def render(result, classes, unmapped, rev):
    lines = []
    w = lines.append
    w("# Documentation coverage of the standard libraries")
    w("")
    w("Measured by `tools/cicd/doc_coverage.py` against `core/compiler/lib_src/` (last changed in `%s`)." % rev)
    w("This is a measurement only: no library source was edited. Writing the missing comments is a")
    w("change to `core/compiler/lib_src/*.obs`, which is reviewed apart from this documentation work.")
    w("(A comment-only edit does not change the compiled library: `obc -tar lib -opt s3` produced a")
    w("byte-identical `.obl` for a sample class with and without doc comments, because line numbers")
    w("are written to an `.obl` only with `--debug`, see `IntermediateInstruction::Write` in")
    w("`core/compiler/emit.cpp`. Confirm it again with a rebuild when the comments are written.)")
    w("")
    w("## How it was measured")
    w("")
    w("A declaration counts as documented when only whitespace separates it from the end of a")
    w("`#~ ... ~#` comment, which is how `code_doc` (`core/lib/code_doc/doc_parser.obs`) attaches a")
    w("comment to the declaration after it. Comments, string literals and character literals are")
    w("blanked first, so a brace or keyword inside one is not seen. The file header `#~~ ... ~~#` is")
    w("not a doc comment.")
    w("")
    w("What is public follows the compiler (`ParseClass` and `ParseMethod` in")
    w("`core/compiler/parser.cpp`):")
    w("")
    w("- a class or interface is public unless declared `class : private : Name`;")
    w("- `function` is public unless declared `function : private :`;")
    w("- `method` is private unless declared `method : public :`;")
    w("- `New` is public unless declared `New : private (...)`;")
    w("- enums and consts count, except inside a private class;")
    w("- nothing inside a private class counts, as `code_doc` lists no private class.")
    w("")
    w("A library is one `.obl`: the files `core/compiler/build_libs.sh` hands to one `obc` call")
    w("(`lang` is `lang.obs`, built by the bootstrap compiler). Fields, enum items and parameters are")
    w("not counted. The script does not judge whether a comment is accurate or complete.")
    w("")
    tot = {k: [0, 0] for k in KINDS}
    rows = []
    for lib, counts in result.items():
        t = sum(c[0] for c in counts.values())
        d = sum(c[1] for c in counts.values())
        for k in KINDS:
            tot[k][0] += counts[k][0]
            tot[k][1] += counts[k][1]
        rows.append((lib, counts, t, d))
    w("## By library")
    w("")
    w("`doc/total` per kind; \"all\" is every public declaration in the library. Sorted by the number")
    w("of undocumented declarations, most first.")
    w("")
    w("| Library | Classes | Enums | Methods | Functions | Constructors | All | Missing |")
    w("|---|---|---|---|---|---|---|---|")
    def cell(c):
        return "%d/%d" % (c[1], c[0]) if c[0] else "-"
    for lib, counts, t, d in sorted(rows, key=lambda r: (-(r[2] - r[3]), r[0])):
        w("| `%s` | %s | %s | %s | %s | %s | %s (%s) | %d |" % (
            lib, cell(counts["class"]), cell(counts["enum"]), cell(counts["method"]),
            cell(counts["function"]), cell(counts["constructor"]), "%d/%d" % (d, t), pct(d, t), t - d))
    all_t = sum(tot[k][0] for k in KINDS)
    all_d = sum(tot[k][1] for k in KINDS)
    w("| **all** | %s | %s | %s | %s | %s | **%d/%d (%s)** | **%d** |" % (
        cell(tot["class"]), cell(tot["enum"]), cell(tot["method"]), cell(tot["function"]),
        cell(tot["constructor"]), all_d, all_t, pct(all_d, all_t), all_t - all_d))
    w("")
    if unmapped:
        w("Source files that `build_libs.sh` does not compile into a library, and so are not counted: " +
          ", ".join("`%s`" % f for f in unmapped) + ".")
        w("")
    w("## The ten worst gaps")
    w("")
    w("The ten classes (or bundle-level declarations) with the most undocumented public members,")
    w("which is where documentation work pays off first.")
    w("")
    w("| # | Library | Class | File | Missing | Of which |")
    w("|---|---|---|---|---|---|")
    ranked = sorted(classes.items(), key=lambda kv: (-len(kv[1]), kv[0]))[:10]
    for n, ((lib, f, owner), miss) in enumerate(ranked, 1):
        kinds = collections.Counter(k for k, _, _ in miss)
        of = ", ".join("%d %s" % (v, k) for k, v in sorted(kinds.items(), key=lambda kv: -kv[1]))
        w("| %d | `%s` | `%s` | `%s` | %d | %s |" % (n, lib, owner, f, len(miss), of))
    w("")
    w("## Reproduce")
    w("")
    w("```")
    w("python tools/cicd/doc_coverage.py                       # print this report")
    w("python tools/cicd/doc_coverage.py --write docs/DOC_COVERAGE.md")
    w("python tools/cicd/doc_coverage.py --json                # raw numbers, per library and per class")
    w("```")
    w("")
    return "\n".join(lines)


SAMPLE = '''#~~
header opened with two tildes, closed with one ~#
use System;
#~
Bundle doc
~#
bundle Demo {
	#~
	Documented class
	~#
	class A {
		@x : Int; # a { brace in a comment
		#~
		doc
		~#
		New() { s := "}{ not code"; }
		New(a : Int) { }
		New : private (a : Int, b : Int) { }
		#~
		doc
		~#
		function : F() ~ Nil { }
		function : private : G() ~ Nil { }
		method : H() ~ Nil { }
		#~
		doc
		~#
		method : public : I() ~ Nil { c := '{'; }
		method : public : J() ~ Nil { }
		method : public : native : K() ~ Nil;
	}

	class : private : P {
		method : public : Q() ~ Nil { }
	}

	enum E { X, Y }
	class B { function : Z() ~ Nil { } }
}
'''


def self_test():
    got = collections.Counter()
    for kind, owner, name, doc, line in scan(SAMPLE):
        got[(kind, owner, name, doc)] += 1
    want = collections.Counter({
        ("class", "", "A", True): 1,
        ("constructor", "A", "New", True): 1,
        ("constructor", "A", "New", False): 1,
        ("function", "A", "F", True): 1,
        ("method", "A", "I", True): 1,
        ("method", "A", "J", False): 1,
        ("method", "A", "K", False): 1,
        ("enum", "", "E", False): 1,
        ("class", "", "B", False): 1,
        ("function", "B", "Z", False): 1,
    })
    if got != want:
        print("doc_coverage self-test FAILED")
        for k in sorted(set(got) | set(want), key=str):
            if got[k] != want[k]:
                print("  %s: got %d, want %d" % (k, got[k], want[k]))
        return 1
    print("doc_coverage self-test passed (%d declarations)" % sum(got.values()))
    return 0


def git_rev():
    try:
        out = subprocess.run(["git", "log", "-1", "--format=%h", "--", "core/compiler/lib_src"], cwd=ROOT,
                             stdout=subprocess.PIPE, stderr=subprocess.DEVNULL, universal_newlines=True)
        return out.stdout.strip() or "unknown"
    except OSError:
        return "unknown"


def main(argv):
    ap = argparse.ArgumentParser(description=__doc__.split("\n")[1])
    ap.add_argument("--write", metavar="FILE", help="write the Markdown report to FILE")
    ap.add_argument("--json", action="store_true", help="print the numbers as JSON")
    ap.add_argument("--self-test", action="store_true", help="check the counter on a built-in sample")
    args = ap.parse_args(argv)
    if args.self_test:
        return self_test()
    result, classes, unmapped = measure()
    if args.json:
        print(json.dumps({
            "libraries": result,
            "undocumented": [{"library": l, "file": f, "class": o, "items": [list(i) for i in v]}
                             for (l, f, o), v in classes.items()],
            "unmapped_sources": unmapped,
        }, indent=1))
        return 0
    report = render(result, classes, unmapped, git_rev())
    if args.write:
        with open(os.path.join(ROOT, args.write) if not os.path.isabs(args.write) else args.write,
                  "w", encoding="utf-8", newline="\n") as fh:
            fh.write(report)
        print("wrote " + args.write)
    else:
        sys.stdout.write(report)
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
