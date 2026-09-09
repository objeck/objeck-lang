#!/usr/bin/env python3
"""Keep the JIT opt-out marker honest.

`# JIT_DISABLE` on a line of its own, at column 0, makes the regression runners
run a test with the JIT off. Two things went wrong with it before this check
existed (2026-09-09):

- The runners matched the text as a SUBSTRING, so five tests written to catch
  JIT bugs -- whose comments said "do NOT add a '# JIT_DISABLE' marker" -- ran
  interpreted on every CI leg for as long as that sentence had existed. The
  runners now match a directive line, but the Windows one can only anchor the
  beginning of a line (findstr cannot end-anchor LF-ended files), so a comment
  that starts a line with the marker text would still be taken as a directive.
- Twenty of twenty-two real directives never said why. Every one of them
  passed with the JIT on; they were opting out of coverage for no reason.

So this asserts, for every `programs/regression/*.obs`:

1. A directive line is exactly `# JIT_DISABLE` (no trailing whitespace: the
   POSIX runner is exact and would silently ignore a variant the Windows one
   accepts).
2. The line right after a directive starts with `# reason:` and says something.
3. No other line contains the text `# JIT_DISABLE`. Say "the JIT opt-out
   marker" in prose instead. (`OBJECK_JIT_DISABLE`, the environment variable,
   is fine.)

Run from anywhere:  python3 tools/cicd/check_jit_optouts.py
Exit 0 when the tree conforms, 1 with one line per problem otherwise.
"""
import os
import sys

MARKER = "# JIT_DISABLE"
REASON = "# reason:"


def repo_root():
    here = os.path.dirname(os.path.abspath(__file__))
    return os.path.abspath(os.path.join(here, "..", ".."))


def check_file(path):
    problems = []
    with open(path, encoding="utf-8", errors="replace") as fh:
        lines = fh.read().split("\n")
    for i, raw in enumerate(lines):
        line = raw.rstrip("\r")
        if line == MARKER:
            nxt = lines[i + 1].rstrip("\r") if i + 1 < len(lines) else ""
            if not nxt.startswith(REASON) or not nxt[len(REASON):].strip():
                problems.append((i + 1, "directive without a '# reason: ...' line right after it"))
            continue
        if line.rstrip() == MARKER:
            problems.append((i + 1, "directive with trailing whitespace: the POSIX runner would not see it"))
            continue
        if MARKER in line:
            problems.append((i + 1, "mentions the marker's literal text; say 'the JIT opt-out marker' instead"))
    return problems


def main():
    root = repo_root()
    reg = os.path.join(root, "programs", "regression")
    files = sorted(f for f in os.listdir(reg) if f.endswith(".obs"))
    failures = []
    directives = 0
    for name in files:
        path = os.path.join(reg, name)
        with open(path, encoding="utf-8", errors="replace") as fh:
            directives += sum(1 for l in fh.read().split("\n") if l.rstrip("\r") == MARKER)
        for lineno, what in check_file(path):
            failures.append("  programs/regression/%s:%d: %s" % (name, lineno, what))
    if failures:
        print("JIT opt-out marker problems:")
        print("\n".join(failures))
        print("A directive is exactly '# JIT_DISABLE' followed by a '# reason:' line;")
        print("prose must not quote the marker (see tools/cicd/check_jit_optouts.py).")
        return 1
    print("%d regression test(s): %d JIT opt-out(s), each with a stated reason; no stray mentions."
          % (len(files), directives))
    return 0


if __name__ == "__main__":
    sys.exit(main())
