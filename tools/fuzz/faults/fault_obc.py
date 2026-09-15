#!/usr/bin/env python3
"""A deliberately broken compiler, to prove the fuzzer catches miscompiles.

Stands in for obc: forwards every argument to the real compiler named by
FUZZ_REAL_OBC, except that an s3 compile is fed a copy of the source whose
first `% 1048573` reads `% 1048571` -- the kind of wrong constant a broken
folder produces. s0 output is untouched, so every s3 configuration should
split from the reference.
"""

import os
import subprocess
import sys


def main():
    real = os.environ.get("FUZZ_REAL_OBC")
    if not real:
        print("fault_obc.py: set FUZZ_REAL_OBC to the real obc", file=sys.stderr)
        return 2
    args = sys.argv[1:]
    if "-opt" in args and args[args.index("-opt") + 1] == "s3" and "-src" in args:
        i = args.index("-src") + 1
        with open(args[i], "r", encoding="utf-8") as f:
            text = f.read()
        faulty = args[i] + ".fault.obs"
        with open(faulty, "w", encoding="utf-8", newline="\n") as f:
            f.write(text.replace("% 1048573", "% 1048571", 1))
        args[i] = faulty
    return subprocess.call([real] + args)


if __name__ == "__main__":
    sys.exit(main())
