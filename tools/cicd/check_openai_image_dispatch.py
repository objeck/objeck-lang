#!/usr/bin/env python3
"""API.OpenAI's image entry points must reach the image implementation.

usage: check_openai_image_dispatch.py <obc> [lib_dir]

`Respond` and `Complete` each have a text implementation and an image one whose
signatures differ only in a generic type argument. Overload resolution does not
tell `Vector<Pair<String|String>>` from `Vector<Pair<String|ImageQuery>>`, so an
image entry point whose call does not otherwise pin the image implementation
silently compiles to a call to the text one, and the image is never sent. Every
one of them did, until #901: the text version was billed and answered without
the picture.

Nothing at run time shows this without an API key, and nothing at compile time
shows it without reading the emitted calls, which is what this does: compile
openai.obs with -asm and follow every call an image entry point makes.

An image entry point is a Respond/Complete whose own signature mentions
ImageQuery. Its Respond/Complete calls must target a method that also mentions
ImageQuery, or CompleteImages, the named image implementation.
"""

import os
import re
import subprocess
import sys
import tempfile

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(os.path.dirname(HERE))
SRC = os.path.join(ROOT, "core", "compiler", "lib_src", "openai.obs")
LIBS = "json,net,net_server,cipher,misc"

METHOD = re.compile(r"(?m)^Method: id=[^;]*; name='([^']+)'")
CALL = re.compile(r"MTHD_CALL: method='([^']+)'")


def fail(message):
    print("FAIL: " + message)
    sys.exit(1)


def main():
    if len(sys.argv) < 2:
        print(__doc__)
        sys.exit(2)

    obc = os.path.abspath(sys.argv[1])
    lib_dir = os.path.abspath(sys.argv[2]) if len(sys.argv) > 2 else os.path.join(ROOT, "core", "lib")
    if not os.path.isfile(obc):
        fail("no obc at " + obc)
    if not os.path.isfile(SRC):
        fail("no openai.obs at " + SRC)

    env = dict(os.environ)
    env["OBJECK_LIB_PATH"] = lib_dir + os.sep

    with tempfile.TemporaryDirectory() as tmp:
        dest = os.path.join(tmp, "openai.obl")
        cmd = [obc, "-src", SRC, "-lib", LIBS, "-tar", "lib", "-opt", "s3", "-asm", "-dest", dest]
        p = subprocess.run(cmd, env=env, cwd=tmp, capture_output=True, text=True,
                           errors="replace", stdin=subprocess.DEVNULL, timeout=600)
        listing = os.path.join(tmp, "openai.obm")
        if p.returncode != 0 or not os.path.isfile(listing):
            fail("openai.obs did not compile with -asm (exit %s)\n%s" % (p.returncode, (p.stdout + p.stderr)[-800:]))

        with open(listing, encoding="utf-8", errors="replace") as fh:
            text = fh.read()

    blocks = METHOD.split(text)
    checked = 0
    wrong = []
    # split() gives [preamble, name, body, name, body, ...]
    for i in range(1, len(blocks) - 1, 2):
        name, body = blocks[i], blocks[i + 1]
        if "ImageQuery" not in name or not re.search(r":(Respond|Complete):", name):
            continue
        for target in CALL.findall(body):
            if not re.search(r":(Respond|Complete|CompleteImages):", target):
                continue
            checked += 1
            if "ImageQuery" not in target and ":CompleteImages:" not in target:
                wrong.append((name, target))

    if wrong:
        for name, target in wrong:
            print("  %s\n    -> %s" % (name, target))
        fail("%d image entry point call(s) reach a text implementation" % len(wrong))

    if checked == 0:
        fail("no image entry point calls found: the check is not looking at anything")

    print("PASS: %d call(s) from API.OpenAI image entry points reach the image implementation" % checked)


if __name__ == "__main__":
    main()
