#!/usr/bin/env python3
"""A deliberately broken VM, to prove the fuzzer catches JIT miscompiles.

Stands in for obr: runs the real VM named by FUZZ_REAL_OBR and, when the
arguments ask for --jit=1, adds one to the last digit of the first digest
line it prints -- a wrong answer from compiled code. Every other run is passed
through untouched, so exactly the jit1 configurations should split off.
"""

import os
import re
import subprocess
import sys


def main():
    real = os.environ.get("FUZZ_REAL_OBR")
    if not real:
        print("fault_obr.py: set FUZZ_REAL_OBR to the real obr", file=sys.stderr)
        return 2
    p = subprocess.run([real] + sys.argv[1:], capture_output=True)
    out = p.stdout.decode("utf-8", "replace")
    if "--jit=1" in sys.argv[1:]:
        m = re.search(r"^(F\d+=-?\d*)(\d)", out, re.M)
        if m:
            digit = str((int(m.group(2)) + 1) % 10)
            out = out[:m.start(2)] + digit + out[m.end(2):]
    sys.stdout.write(out)
    sys.stdout.flush()
    sys.stderr.write(p.stderr.decode("utf-8", "replace"))
    return p.returncode


if __name__ == "__main__":
    sys.exit(main())
