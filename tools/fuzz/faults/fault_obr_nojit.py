#!/usr/bin/env python3
"""A VM that ignores --jit=1, to prove the JIT-coverage gate is not fooled.

Stands in for obr: runs the real VM named by FUZZ_REAL_OBR with every --jit=1
turned into --jit=off. Output is unchanged -- the interpreter agrees with
itself -- so only the coverage gate can notice that nothing was compiled.
"""

import os
import subprocess
import sys


def main():
    real = os.environ.get("FUZZ_REAL_OBR")
    if not real:
        print("fault_obr_nojit.py: set FUZZ_REAL_OBR to the real obr", file=sys.stderr)
        return 2
    args = ["--jit=off" if a == "--jit=1" else a for a in sys.argv[1:]]
    p = subprocess.run([real] + args, capture_output=True)
    sys.stdout.write(p.stdout.decode("utf-8", "replace"))
    sys.stdout.flush()
    sys.stderr.write(p.stderr.decode("utf-8", "replace"))
    return p.returncode


if __name__ == "__main__":
    sys.exit(main())
