#!/usr/bin/env python3
"""A stand-in obr for tests that need no Objeck build (pairs with fake_obc.py,
whose ".obe" is the program source).

It prints one digest line per top-level function `Fi` ("Fi=1"), the same in
every configuration, and exits 0. FAKE_OBR_MODE picks how it treats --jit=1:

  honest      (default) with OBJECK_JIT_REPORT set, reports every method of
              the program as "[jit] Class:Method:: compiled", as the real VM
              does for each method it compiles;
  ignore-jit  prints no report at all: a VM that never compiles, whose run
              under --jit=1 is the interpreter again;
  diverge     like honest, but under --jit=1 F0 prints a wrong digest (a
              JIT miscompile), so every program is a finding;
  partial     like honest, but reports only the program's first method, so
              coverage is low without being zero.
"""

import os
import re
import sys

CLASS_RE = re.compile(r"^class (\w+)")
METHOD_RE = re.compile(r"^\s*(?:function|method)\s*:\s*(?:(?:public|private|virtual)\s*:\s*)*(\w+)\(")


def methods(text):
    cls = None
    for line in text.split("\n"):
        m = CLASS_RE.match(line)
        if m:
            cls = m.group(1)
            continue
        m = METHOD_RE.match(line)
        if m and cls:
            yield "%s:%s" % (cls, m.group(1))


def main(argv):
    mode = os.environ.get("FAKE_OBR_MODE", "honest")
    jit1 = "--jit=1" in argv
    obe = [a for a in argv if not a.startswith("--")][-1]
    with open(obe, encoding="utf-8") as f:
        text = f.read()
    names = sorted(set(re.findall(r"function : (F\d+)\(", text)), key=lambda n: int(n[1:]))
    for name in names:
        wrong = mode == "diverge" and jit1 and name == "F0"
        sys.stdout.write("%s=%d\n" % (name, 2 if wrong else 1))
    if jit1 and os.environ.get("OBJECK_JIT_REPORT") and mode in ("honest", "diverge", "partial"):
        reported = list(methods(text))
        if mode == "partial":
            reported = reported[:1]
        for m in reported:
            sys.stderr.write("[jit] %s:: compiled\n" % m)
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
