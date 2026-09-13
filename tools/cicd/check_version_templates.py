#!/usr/bin/env python3
"""Assert that every file update_version.ps1 generates matches its .in template.

core/release/update_version.ps1 runs at the start of every Windows deploy -- CI,
release-build and release-publish alike -- and regenerates tracked files from
`.in` templates before anything reads them. An edit made to a generated file
instead of its template therefore looks right in the repository and is erased by
every build that matters.

That happened to code_doc64.cmd. The check that search_index.json parses, added
after an invalid index shipped for seven releases, went into the .cmd and never
into code_doc64.in, so no Windows deploy ever ran it. A local deploy showed it:
the regenerated .cmd came back twelve lines shorter than the committed one.

The template list, the substitutions and the version are all read from
update_version.ps1 itself, so this script cannot drift from the generator. Line
endings and trailing newlines are ignored: Set-Content writes CRLF, and the
script then rewrites most of its outputs LF.

Run from anywhere:  python3 tools/cicd/check_version_templates.py
Exit 0 when every generated file matches its rendered template, 1 otherwise.
"""
import difflib
import os
import re
import sys

ROOT = os.path.abspath(os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", ".."))
PS1_DIR = os.path.join(ROOT, "core", "release")
PS1 = os.path.join(PS1_DIR, "update_version.ps1")

# One generator line:
#   (Get-Content code_doc64.in) | ForEach-Object { $_ -replace "@VERSION@", $version } | ... | Set-Content code_doc64.cmd
LINE = re.compile(r"^\(Get-Content\s+(\S+\.in)\)\s*(.*?)\|\s*Set-Content\s+(\S+)\s*$", re.M)
REPLACE = re.compile(r'-replace\s+"(@[A-Z_]+@)",\s*\$(\w+)')
PLACEHOLDER = re.compile(r"@[A-Z][A-Z_]*@")


def read(path):
    # latin-1 round-trips every byte, which is what Get-Content/Set-Content do
    # with the ANSI codepage; the comparison is on bytes, not characters.
    with open(path, "rb") as f:
        return f.read().decode("latin-1").replace("\r\n", "\n").rstrip("\n")


def resolve(ps_relative):
    # Paths in the .ps1 are relative to core/release and Windows-style, with the
    # odd doubled separator ("..\..\\programs").
    parts = [p for p in re.split(r"[\\/]+", ps_relative) if p]
    return os.path.normpath(os.path.join(PS1_DIR, *parts))


def rel(path):
    return os.path.relpath(path, ROOT).replace(os.sep, "/")


def main():
    text = read(PS1)

    scalars = dict(re.findall(r'^\$(year_end|month_end|version)\s*=\s*"(\d+)"', text, re.M))
    missing = {"year_end", "month_end", "version"} - scalars.keys()
    if missing:
        print(f"ERROR: could not read {', '.join(sorted(missing))} from {rel(PS1)} -- has its format changed?")
        return 1
    version = "{year_end}.{month_end}.{version}".format(**scalars)
    values = {
        "version": version,
        "year_end": scalars["year_end"],
        "month_end": scalars["month_end"],
        "version_number": version.replace(".", ""),
        "version_windows": version.replace(".", ","),
    }

    jobs = LINE.findall(text)
    if not jobs:
        print(f"ERROR: found no '(Get-Content X.in) ... | Set-Content Y' lines in {rel(PS1)} -- has its format changed?")
        return 1

    failures = 0
    for template, pipeline, output in jobs:
        tin, out = resolve(template), resolve(output)
        subs = REPLACE.findall(pipeline)
        unknown = sorted({var for _, var in subs if var not in values})
        if unknown:
            print(f"ERROR  {rel(out)}: update_version.ps1 substitutes ${', $'.join(unknown)}, which this check does not compute")
            failures += 1
            continue
        if not os.path.isfile(tin) or not os.path.isfile(out):
            print(f"ERROR  {rel(out)}: template exists={os.path.isfile(tin)}, output exists={os.path.isfile(out)}")
            failures += 1
            continue

        rendered = read(tin)
        for token, var in subs:
            rendered = rendered.replace(token, values[var])
        committed = read(out)
        leftover = sorted(set(PLACEHOLDER.findall(rendered)))

        if rendered == committed and not leftover:
            print(f"ok     {rel(out)}  <-  {rel(tin)}")
            continue

        failures += 1
        print(f"DRIFT  {rel(out)} does not match {rel(tin)} rendered at {version}")
        if leftover:
            print(f"       unreplaced placeholders: {' '.join(leftover)}")
        diff = list(difflib.unified_diff(rendered.split("\n"), committed.split("\n"),
                                         f"{rel(tin)} (rendered)", f"{rel(out)} (committed)", n=1, lineterm=""))
        for line in diff[:40]:
            print("       " + line)
        if len(diff) > 40:
            print(f"       ... {len(diff) - 40} more diff lines")

    if failures:
        print()
        print(f"{failures} generated file(s) differ from their templates. Every Windows deploy regenerates")
        print("them with update_version.ps1, so a change made only to the output is thrown away.")
        print("Make the change in the .in template, then regenerate (or apply it to both).")
        return 1

    print(f"all {len(jobs)} generated files match their templates (version {version})")
    return 0


if __name__ == "__main__":
    sys.exit(main())
