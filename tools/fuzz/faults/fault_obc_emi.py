#!/usr/bin/env python3
"""A deliberately broken compiler, to prove emi.py catches a compiler that
takes a dead EMI guard.

Stands in for obc: forwards every argument to the real compiler named by
FUZZ_REAL_OBC, except that compiles at the opt levels listed in
FUZZ_FAULT_OPTS (comma separated, default "s3") are fed a copy of the source
whose EmiGuardZq->Dead() returns true -- the effect of a folder or emitter
that wrongly decides a runtime-false condition.

  FUZZ_FAULT_OPTS=s3     only the s3 configurations split from the reference
  FUZZ_FAULT_OPTS=s0,s3  every configuration agrees on the wrong output: a
                         uniform divergence, as a front-end bug would give
"""

import os
import subprocess
import sys


def main():
    real = os.environ.get("FUZZ_REAL_OBC")
    if not real:
        print("fault_obc_emi.py: set FUZZ_REAL_OBC to the real obc", file=sys.stderr)
        return 2
    opts = {o.strip() for o in os.environ.get("FUZZ_FAULT_OPTS", "s3").split(",") if o.strip()}
    args = sys.argv[1:]
    if "-opt" in args and args[args.index("-opt") + 1] in opts and "-src" in args:
        i = args.index("-src") + 1
        with open(args[i], "r", encoding="utf-8") as f:
            text = f.read()
        faulty = args[i] + ".fault.obs"
        with open(faulty, "w", encoding="utf-8", newline="\n") as f:
            f.write(text.replace("return @dead;", "return true;", 1))
        args[i] = faulty
    return subprocess.call([real] + args)


if __name__ == "__main__":
    sys.exit(main())
