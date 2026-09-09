#!/usr/bin/env python3
"""Keep the shipped example index honest.

`programs/deploy/welcome.obs` is the index of the examples that ship in a
release. It is the only map a user gets: the deploy scripts copy every `.obs`
in that folder into `examples/`, and nothing else there describes what any of
them do.

An index nobody checks goes stale. This folder has already demonstrated how:
two examples were numbered 19, a scratch file and a JIT bug reproduction
shipped as teaching material for months, and one example's own documented
build command omitted a library it needs, so the command did not work.

So this asserts, for `programs/deploy`:

1. Every `.obs` in the folder appears in the index (`welcome.obs` itself is the
   index, so it is excluded).
2. Every file the index names exists.
3. The three parallel arrays -- files, libraries, descriptions -- are the same
   length, so no row is silently truncated.
4. No description is empty.

It does NOT check that the library lists compile: that needs a built toolchain.
The lists were verified by compiling all of them when the index was written; if
you change one, compile that example before pushing.

Run from anywhere:  python3 tools/cicd/check_examples_index.py
Exit 0 when the index conforms, 1 with one line per problem otherwise.
"""
import os
import re
import sys

INDEX = os.path.join("programs", "deploy", "welcome.obs")
EXAMPLES_DIR = os.path.join("programs", "deploy")


def repo_root():
    here = os.path.dirname(os.path.abspath(__file__))
    return os.path.abspath(os.path.join(here, "..", ".."))


def array_literal(source, name):
    """Return the quoted strings of `name := [ ... ];`, or None if absent."""
    match = re.search(re.escape(name) + r"\s*:=\s*\[(.*?)\];", source, re.DOTALL)
    if not match:
        return None
    return re.findall(r'"([^"]*)"', match.group(1))


def main():
    root = repo_root()
    index_path = os.path.join(root, INDEX)
    problems = []

    if not os.path.isfile(index_path):
        print("%s: missing -- the shipped examples would have no index" % INDEX)
        return 1

    with open(index_path, "r", encoding="utf-8") as handle:
        source = handle.read()

    files = array_literal(source, "files")
    libs = array_literal(source, "libs")
    about = array_literal(source, "about")

    for name, value in (("files", files), ("libs", libs), ("about", about)):
        if value is None:
            problems.append("%s: cannot find the '%s' array" % (INDEX, name))
    if problems:
        for problem in problems:
            print(problem)
        return 1

    if not (len(files) == len(libs) == len(about)):
        problems.append(
            "%s: the arrays disagree -- files=%d, libs=%d, about=%d; every example needs all three"
            % (INDEX, len(files), len(libs), len(about)))

    for position, description in enumerate(about):
        if not description.strip():
            named = files[position] if position < len(files) else "row %d" % position
            problems.append("%s: %s has an empty description" % (INDEX, named))

    listed = set(files)
    on_disk = set()
    for entry in sorted(os.listdir(os.path.join(root, EXAMPLES_DIR))):
        if entry.endswith(".obs") and entry != "welcome.obs":
            on_disk.add(entry)

    for missing in sorted(on_disk - listed):
        problems.append(
            "%s: %s ships but is not in the index -- add a row, or it reaches users undocumented"
            % (INDEX, missing))

    for phantom in sorted(listed - on_disk):
        problems.append(
            "%s: the index lists %s, which is not in %s" % (INDEX, phantom, EXAMPLES_DIR))

    duplicates = sorted({name for name in files if files.count(name) > 1})
    for duplicate in duplicates:
        problems.append("%s: %s is listed more than once" % (INDEX, duplicate))

    if problems:
        for problem in problems:
            print(problem)
        return 1

    print("%d shipped example(s), every one indexed with a library list and a description."
          % len(files))
    return 0


if __name__ == "__main__":
    sys.exit(main())
