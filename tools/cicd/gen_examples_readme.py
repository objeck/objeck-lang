#!/usr/bin/env python3
"""Generate programs/deploy/README.md from welcome.obs.

`welcome.obs` is the index program that ships with the examples, and
`check_examples_index.py` already asserts it stays in step with the folder. It
carries three parallel arrays -- file names, library lists and one-line
descriptions -- so it is the only place those three facts live together.

A hand-written README beside it would be a fourth copy of the same data with
nothing keeping it honest, which is exactly how "33 demos" sat wrong in two
public files for five releases. This derives the README from that source
instead, so the pair cannot drift: regenerate it, or let CI tell you it is
stale.

  write:  python3 tools/cicd/gen_examples_readme.py
  check:  python3 tools/cicd/gen_examples_readme.py --check
"""
import io
import os
import re
import sys

ROOT = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
INDEX = os.path.join(ROOT, "programs", "deploy", "welcome.obs")
README = os.path.join(ROOT, "programs", "deploy", "README.md")


def _array(src, name):
    """Pull one `name := [ "a", "b" ];` array out of the source, in order."""
    m = re.search(re.escape(name) + r"\s*:=\s*\[(.*?)\];", src, re.S)
    if not m:
        raise SystemExit("could not find the '%s' array in welcome.obs" % name)
    return re.findall('"([^"]*)"', m.group(1))


def build():
    src = io.open(INDEX, encoding="utf-8-sig").read()
    files, libs, about = _array(src, "files"), _array(src, "libs"), _array(src, "about")
    if not (len(files) == len(libs) == len(about)):
        raise SystemExit(
            "welcome.obs arrays disagree: %d files, %d libs, %d descriptions"
            % (len(files), len(libs), len(about))
        )

    out = []
    out.append("# Objeck example programs\n")
    out.append(
        "\nThe programs in this folder ship with every Objeck release, in the\n"
        "`examples/` directory of the distribution. `welcome.obs` prints this same\n"
        "index at the terminal:\n"
    )
    out.append("\n```\nobc -src welcome.obs -lib json,net,cipher\nobr welcome.obe\n```\n")
    out.append(
        "\nEach program is self-contained and opens with a comment giving its exact\n"
        "compile and run lines. Run them from this folder (or from `examples/` in an\n"
        "install): two of them read files from `data/`, which ships alongside.\n"
    )
    out.append("\n| Example | Libraries | What it shows |\n|---|---|---|\n")
    for f, l, a in zip(files, libs, about):
        libcell = "`-lib %s`" % l if l else "none"
        out.append("| [`%s`](%s) | %s | %s |\n" % (f, f, libcell, a))

    out.append(
        "\n## Building one\n"
        "\nWith no libraries:\n"
        "\n```\nobc -src hello_0.obs\nobr hello_0.obe\n```\n"
        "\nWith them, naming the list from the table above:\n"
        "\n```\nobc -src json_stream_23.obs -lib json_stream\nobr json_stream_23.obe\n```\n"
    )
    out.append(
        "\n## Notes\n"
        "\n- `2d_game_13.obs` and `3d_gl_24.obs` open a window and need SDL2; the rest\n"
        "  run in a terminal.\n"
        "- `odbc_select_11.obs` expects an ODBC datasource named `foo`.\n"
        "- `gemini_22.obs` needs an API key, and the network examples need a\n"
        "  connection.\n"
        "- `neural_21.obs` trains and stores a model on its first run, then tests it\n"
        "  when run again with `brun`.\n"
    )
    out.append(
        "\n---\n"
        "\n*Generated from `welcome.obs` by `tools/cicd/gen_examples_readme.py`.\n"
        "Edit the arrays there, not this file, and regenerate.*\n"
    )
    return "".join(out)


def main():
    text = build()
    check = "--check" in sys.argv
    current = io.open(README, encoding="utf-8").read() if os.path.exists(README) else None
    if check:
        if current != text:
            print("programs/deploy/README.md is stale.")
            print("Regenerate it:  python3 tools/cicd/gen_examples_readme.py")
            return 1
        print("programs/deploy/README.md is current.")
        return 0
    io.open(README, "w", encoding="utf-8", newline="\n").write(text)
    print("wrote programs/deploy/README.md")
    return 0


if __name__ == "__main__":
    sys.exit(main())
