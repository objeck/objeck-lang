#!/usr/bin/env python3
"""
tsan_reports.py LOGDIR [--strip PREFIX]

Reads the ThreadSanitizer logs run_tsan.sh left in LOGDIR (one file per VM
process, named <test>.<pid>), sorts every report into one of two kinds, writes
LOGDIR/by_test.tsv (kind, test, summary; one line per distinct triple) and prints
the totals. Exits 1 if any report of kind "vm" was found.

The two kinds:

  program   a data race whose two accesses were BOTH made by the interpreter's
            field instructions (StackInterpreter::LoadClsInstIntVar,
            StorClsInstIntVar, CopyClsInstIntVar and the dispatch handlers of
            the *_VAR opcodes). Those functions touch a word of an object or of
            class memory only because the program's bytecode said to, so two of
            them racing is the program (or a library written in Objeck) reading
            and writing its own field from two threads without a lock. The
            language allows that. It is counted and listed, and it does not fail
            the run.

  vm        everything else, including a race with a field instruction on ONE
            side only: the other side is then the VM's own code (the collector
            moving or fixing up an object under a thread that was not stopped
            would look exactly like that), and that is a fault until shown
            otherwise.

A suppression cannot make this distinction: "race:NAME" in tsan.supp hides a
report when NAME is in either stack.
"""
import os
import re
import sys

# The top frame of an access made by a field instruction on the program's behalf.
FIELD_ACCESS = re.compile(
    r"Runtime::StackInterpreter::(Load|Stor|Copy)ClsInst\w*Var\(|\bHandle_(LOAD|STOR|COPY)_\w*VAR\b")
ACCESS = re.compile(r"^\s+(Previous )?(atomic )?(read|write) of size \d+", re.IGNORECASE)
FRAME0 = re.compile(r"^\s+#0 (.*)$")
SUMMARY = re.compile(r"^SUMMARY: ThreadSanitizer: (.*)$")


def reports(text):
    """The report blocks of one log file."""
    for block in text.split("==================\n"):
        if "WARNING: ThreadSanitizer" in block:
            yield block.splitlines()


def classify(lines):
    """("program" | "vm", summary) for one report."""
    summary = "(no summary line)"
    tops = []
    want_frame = False
    for line in lines:
        m = SUMMARY.match(line)
        if m:
            summary = m.group(1)
            continue
        if ACCESS.match(line):
            want_frame = True
            continue
        if want_frame:
            m = FRAME0.match(line)
            if m:
                tops.append(m.group(1))
            want_frame = False
    is_race = any("ThreadSanitizer: data race" in line for line in lines)
    if is_race and len(tops) == 2 and all(FIELD_ACCESS.search(top) for top in tops):
        return "program", summary
    return "vm", summary


def main(argv):
    if len(argv) < 2:
        print(__doc__)
        return 2
    logdir = argv[1]
    strip = argv[argv.index("--strip") + 1] if "--strip" in argv else ""
    rows = set()
    for name in sorted(os.listdir(logdir)):
        test, dot, pid = name.rpartition(".")
        if not dot or not pid.isdigit():
            continue
        with open(os.path.join(logdir, name), encoding="utf-8", errors="replace") as handle:
            text = handle.read()
        for lines in reports(text):
            kind, summary = classify(lines)
            if strip:
                summary = summary.replace(strip, "")
            summary = summary.replace("../posix/../../", "").replace("../../", "").replace("../", "")
            rows.add((kind, test, summary))

    with open(os.path.join(logdir, "by_test.tsv"), "w", encoding="utf-8", newline="\n") as out:
        for row in sorted(rows):
            out.write("\t".join(row) + "\n")

    for kind, title in (("vm", "reports against the VM"), ("program", "programs racing on their own fields (not counted)")):
        mine = [row for row in rows if row[0] == kind]
        print("%s: %d report(s) in %d test(s)" % (title, len(mine), len({row[1] for row in mine})))
        classes = {}
        for _, test, summary in mine:
            classes.setdefault(summary, set()).add(test)
        for summary, tests in sorted(classes.items(), key=lambda item: (-len(item[1]), item[0])):
            shown = sorted(tests)
            print("  %3d  %s" % (len(tests), summary))
            print("       %s%s" % (" ".join(shown[:6]), " ..." if len(shown) > 6 else ""))
    return 1 if any(row[0] == "vm" for row in rows) else 0


if __name__ == "__main__":
    sys.exit(main(sys.argv))
