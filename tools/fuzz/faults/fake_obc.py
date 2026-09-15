#!/usr/bin/env python3
"""A stand-in obc for tests that need no Objeck build: `-src S -opt O -dest D`
copies the source to D, so fake_obr.py can read the program back."""

import shutil
import sys


def main(argv):
    opts = dict(zip(argv[0::2], argv[1::2]))
    if "-src" not in opts or "-dest" not in opts:
        print("fake_obc.py: need -src and -dest", file=sys.stderr)
        return 2
    shutil.copyfile(opts["-src"], opts["-dest"])
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
