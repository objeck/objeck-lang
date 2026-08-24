#!/usr/bin/env python3
"""Assert that every library-list in the tree names every library in the tree.

There are five hard-coded lists of `core/compiler/lib_src/*.obs`, in five files,
that all have to agree: two API-doc builds, the local doc build, and the two LSP
doc-index generators. Nothing connects them, so adding a library means editing
five places and forgetting one is silent -- the doc or the editor index simply
omits a bundle and every check still passes.

They had already drifted before anyone was watching. When this script was written
`ci-build.yml` was missing `concurrent`, still named a deleted `math`, and -- like
three of the other four -- had never heard of `sdl_gl`, so `Game.OpenGL` shipped
with no API documentation and no editor completion.

A count comparison does not catch that: `ci-build.yml` was one short and one
stale, which is the same count. Hence a set difference, naming the entries.

Run from anywhere:  python3 tools/cicd/check_doc_lists.py
Exit 0 when every list agrees with lib_src, 1 otherwise.
"""
import os
import re
import sys

# (path relative to repo root, regex locating the command line/block to read)
#
# Scoped to the command itself. A grep over a whole workflow file also finds the
# lib_src paths in the library-BUILD steps, which inflates the list and hides a
# genuine omission in the doc step.
TARGETS = [
    (".github/workflows/release-build.yml", r"obr code_doc\.obe"),
    (".github/workflows/ci-build.yml", r"obr code_doc\.obe"),
    ("core/release/code_doc64.in", r"code_doc\.obe .*templates"),
    ("tools/lsp/server/doc_json/gen_json.cmd", r"doc_json\.obe templates"),
    ("tools/lsp/server/doc_json/gen_json.sh", r"doc_json\.obe templates"),
]

# Libraries a list is allowed to omit, with the reason. Keep this empty unless
# there is a real one -- an entry here is a documented hole, not a silent one.
EXEMPT = {}

NAME = re.compile(r"([A-Za-z_0-9]+)\.obs")
CONTINUES = re.compile(r"[\\^]\s*$")


def repo_root():
    # tools/cicd/check_doc_lists.py -> tools/cicd -> tools -> repo root
    here = os.path.dirname(os.path.abspath(__file__))
    return os.path.dirname(os.path.dirname(here))


def read_block(path, start_pattern):
    """The matching line plus any lines it continues onto via \\ or ^."""
    with open(path, encoding="utf-8", errors="replace") as fh:
        lines = fh.read().splitlines()
    for i, line in enumerate(lines):
        if re.search(start_pattern, line):
            block, j = [line], i
            while CONTINUES.search(lines[j]) and j + 1 < len(lines):
                j += 1
                block.append(lines[j])
            return block
    return None


def main():
    root = repo_root()
    lib_src = os.path.join(root, "core", "compiler", "lib_src")
    actual = sorted(f[:-4] for f in os.listdir(lib_src) if f.endswith(".obs"))
    print("core/compiler/lib_src: %d libraries" % len(actual))

    failures = []
    for rel, pattern in TARGETS:
        path = os.path.join(root, rel)
        if not os.path.exists(path):
            failures.append("%s: file not found" % rel)
            continue

        block = read_block(path, pattern)
        if block is None:
            # Worth failing on: a renamed command means this script is no longer
            # reading what it claims to read, and would pass on anything.
            failures.append("%s: no line matching /%s/ -- has the command been "
                            "renamed? This check is not reading it." % (rel, pattern))
            continue

        listed = set(n for line in block for n in NAME.findall(line))
        exempt = EXEMPT.get(rel, set())
        missing = [n for n in actual if n not in listed and n not in exempt]
        stale = [n for n in sorted(listed) if n not in actual]

        status = "ok" if not (missing or stale) else "MISMATCH"
        print("  %-46s %2d listed  %s" % (rel, len(listed), status))
        if missing:
            failures.append("%s: missing %s" % (rel, ", ".join(missing)))
        if stale:
            failures.append("%s: names %s, which is not in lib_src"
                            % (rel, ", ".join(stale)))

    if failures:
        print()
        print("Library lists disagree with core/compiler/lib_src:")
        for f in failures:
            print("  - %s" % f)
        print()
        print("Add the library to every list above, or add a documented EXEMPT")
        print("entry in %s." % os.path.relpath(__file__, root))
        return 1

    print("All library lists agree with lib_src.")
    return 0


if __name__ == "__main__":
    sys.exit(main())
