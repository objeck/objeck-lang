#!/usr/bin/env python3
"""Compile every example that ships in this folder.

The library list for each example is read from README.md rather than kept here,
because README.md is what a reader follows and a second copy would drift from
it silently. A row whose '-lib' line is wrong fails this script, which is the
point: the table is executable documentation rather than a claim.

  python build_examples.py                 compile all, report
  python build_examples.py --keep          leave the .obe files behind
  python build_examples.py --only json     compile the examples matching a name
  python build_examples.py --opt s0,s3     compile at each optimizer level
  python build_examples.py --run           also run them and check what they print

--run compares each program's output with the 'output:' block in the comment at
the top of its own source (the text a reader sees first), so the documentation
of what an example prints is the test of it. A line that is just '...' stands
for any number of lines, a '...' inside a line stands for any text in it, and <any>
and <int> stand for one varying field (any text, a whole number); the rest of the
line is compared exactly. A program that is run must have an output block, or
'# output: none' if it prints nothing. The examples that need a display, a database, a key or the network are not run;
the report says which and why.

Exit code is 0 only when every example compiled (and, with --run, printed what
its header says).
"""

import argparse
import glob
import os
import re
import shutil
import subprocess
import sys
import tempfile

HERE = os.path.dirname(os.path.abspath(__file__))
README = os.path.join(HERE, "README.md")

# examples that cannot run where this script does, and why. They are still compiled.
NOT_RUN = {
    "game_31.obs": "needs a display",
    "odbc_21.obs": "needs an ODBC data source",
    "llm_29.obs": "needs a Gemini API key",
    "http_client_22.obs": "needs the internet",
    "rss_reader_24.obs": "needs the internet",
}

# | [`name.obs`](name.obs) | `-lib a,b` | description |
ROW = re.compile(r"^\|\s*\[`(?P<name>[A-Za-z0-9_]+\.obs)`\][^|]*\|(?P<rest>[^|]*)\|")
LIBS = re.compile(r"`-lib\s+(?P<libs>[A-Za-z0-9_,@]+)`")


def find_obc():
    """The obc beside this tree if there is one, else whatever is on PATH.

    'deploy-x64' is not hardcoded: CI names the tree deploy-<arch> when that
    exists and plain 'deploy' otherwise, so both are globbed.
    """
    exe = "obc.exe" if os.name == "nt" else "obc"

    # shipped layout first: examples/ sits beside bin/
    shipped = os.path.join(HERE, "..", "bin", exe)
    if os.path.isfile(shipped):
        return os.path.abspath(shipped)

    # repo layout, whatever the deploy tree is called. The most recently built
    # wins: a machine can hold several trees (deploy-msys2-ucrt, deploy-x64t), and
    # taking the first alphabetically compiled against a months-old toolchain that
    # rejected every newer example.
    pattern = os.path.join(HERE, "..", "..", "core", "release", "deploy*", "bin", exe)
    candidates = [c for c in glob.glob(pattern) if os.path.isfile(c)]
    if candidates:
        return os.path.abspath(max(candidates, key=os.path.getmtime))

    found = shutil.which(exe)
    if found:
        return found
    sys.exit("obc not found: put it on PATH, or run this from a deploy tree")


def library_env(obc):
    """obc resolves its .obl set relative to the CWD unless told otherwise, so
    running it from anywhere but bin/ fails on lang.obl before it reads a line
    of source. OBJECK_LIB_PATH is the documented way to say it outright."""
    env = dict(os.environ)
    if "OBJECK_LIB_PATH" not in env:
        lib = os.path.abspath(os.path.join(os.path.dirname(obc), "..", "lib"))
        if os.path.isdir(lib):
            env["OBJECK_LIB_PATH"] = lib

    # the machine-learning examples load native libraries that sit in lib/native
    # (--run starts programs, and the loader has to find them)
    native = os.path.join(env.get("OBJECK_LIB_PATH", ""), "native")
    if os.path.isdir(native):
        var = "PATH" if os.name == "nt" else "LD_LIBRARY_PATH"
        env[var] = native + os.pathsep + env.get(var, "")
    return env


def read_table():
    """[(example, [libs]), ...] in the order README.md lists them.

    welcome.obs is the index and is documented in prose above the table, not as
    a row, so it is added explicitly rather than being quietly skipped.
    """
    if not os.path.isfile(README):
        sys.exit("README.md not found beside this script")

    entries = []
    with open(README, encoding="utf-8") as handle:
        for line in handle:
            m = ROW.match(line)
            if not m:
                continue
            libs = LIBS.search(m.group("rest"))
            entries.append((m.group("name"),
                            libs.group("libs").split(",") if libs else []))

    if os.path.isfile(os.path.join(HERE, "welcome.obs")):
        entries.insert(0, ("welcome.obs", ["json", "net", "cipher"]))
    return entries


def find_obr(obc):
    """obr sits beside obc in every layout, so no second search is needed."""
    exe = "obr.exe" if os.name == "nt" else "obr"
    beside = os.path.join(os.path.dirname(obc), exe)
    if os.path.isfile(beside):
        return beside
    found = shutil.which(exe)
    if found:
        return found
    sys.exit("obr not found beside obc: --run needs it")


def expected_output(path):
    """The expected output lines from the 'output' block of the file's opening comment.

    Returns None when the file declares no block, which --run treats as a failure: an
    example that is run must say what it prints. The block is the comment lines after
    '# output...:', each written '#   text' (a '#' and three spaces; a bare '#' is an
    empty line), up to the first comment line that is not indented that way or the closing
    '~#'. An example that prints nothing says so with '# output: none', which returns an
    empty list (and then any output at all fails).

    Output that differs from run to run is not a reason to skip the comparison: the fixed
    text around it is still checked, and the variable fields are written as placeholders
    (see line_pattern).
    """
    with open(path, encoding="utf-8-sig") as handle:
        lines = handle.read().splitlines()
    heading = None
    block = []
    for line in lines:
        if line.strip() == "~#":
            break
        if heading is None:
            if re.match(r"^#\s*output[^:]*:\s*$", line) or re.match(r"^#\s*output:\s*none\s*$", line):
                heading = line
                if "none" in line.split(":", 1)[1]:
                    return []
            continue
        if line.startswith("#   "):
            block.append(line[4:])
        elif line.strip() == "#":
            block.append("")
        else:
            break    # prose after the output (a comment line that is not indented)
    if heading is None or not trim(block):
        return None
    return block


# what a placeholder stands for. They are scoped to one field of one line: the text
# around them is still compared exactly, so a placeholder cannot hide a wrong line.
PLACEHOLDERS = {"<any>": ".+", "<int>": "-?[0-9]+", "...": ".*"}
TOKEN = re.compile(r"(<any>|<int>|\.\.\.)")


def line_pattern(want):
    """A compiled pattern for one expected line, or None for a plain comparison.

    Inside a line, <any> is one or more characters, <int> a whole number, and '...' any
    text (including none), so a header can cut a long line short ('{"time":"Wednesday, ...')
    or leave a piece out of the middle. Everything else must match exactly."""
    parts = TOKEN.split(want)
    if len(parts) == 1:
        return None
    return re.compile("".join(PLACEHOLDERS[p] if p in PLACEHOLDERS else re.escape(p) for p in parts), re.S)


def fits(want, pattern, got):
    return pattern.fullmatch(got) is not None if pattern else got == want


def trim(lines):
    """The lines without the blank ones at the end, which no header writes down."""
    lines = list(lines)
    while lines and not lines[-1].strip():
        lines.pop()
    return lines


def matches(expected, actual):
    """True when actual (a list of lines) fits expected.

    A line that is just '...' stands for any number of lines, none included. Where it
    stands the match is not decided by the first line that fits: 'item ...' may fit
    several lines, and only the lines expected after it say which one was meant. So every
    way of matching is kept (a set of positions in the actual output) until the expected
    lines run out."""
    expected, actual = trim(expected), trim(actual)
    positions = {0}
    for want in expected:
        if want == "...":
            positions = set(range(min(positions), len(actual) + 1))
            continue
        pattern = line_pattern(want)
        positions = {at + 1 for at in positions if at < len(actual) and fits(want, pattern, actual[at])}
        if not positions:
            return False
    return len(actual) in positions


def judge(want, code, got):
    """(ok, note) for one run: exit code, then the output against the header's block."""
    if code != 0:
        return False, "exit %s: %s" % (code, " | ".join(got[-3:]))
    if want is None:
        return False, "no 'output:' block in the header (write one, or '# output: none' for a silent program)"
    if not matches(want, got):
        return False, "output differs from the header: " + describe(want, got)
    return True, "ran, output matches its header"


def run_example(obr, obe, env):
    """(exit code, output lines) of one run; a hang is a failure after two minutes."""
    try:
        proc = subprocess.run([obr, obe], capture_output=True, text=True, cwd=HERE,
                              env=env, timeout=120)
    except subprocess.TimeoutExpired:
        return None, ["timed out after 120 seconds"]
    return proc.returncode, [l.rstrip() for l in (proc.stdout or "").splitlines()]


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--keep", action="store_true",
                    help="leave the compiled .obe files in place")
    ap.add_argument("--only", metavar="TEXT",
                    help="only examples whose name contains TEXT")
    ap.add_argument("--opt", metavar="LEVELS", default="",
                    help="compile at each of these optimizer levels, e.g. s0,s3 (default: obc's own)")
    ap.add_argument("--run", action="store_true",
                    help="also run each example and compare its output with its header")
    args = ap.parse_args()

    obc = find_obc()
    obr = find_obr(obc) if args.run else None
    env = library_env(obc)
    entries = read_table()
    if args.only:
        entries = [e for e in entries if args.only in e[0]]
    if not entries:
        sys.exit("no examples matched")
    levels = [l for l in args.opt.split(",") if l] or [None]

    out_dir = tempfile.mkdtemp(prefix="obc-examples-") if not args.keep else HERE
    print("obc     : %s" % obc)
    print("lib     : %s" % env.get("OBJECK_LIB_PATH", "(from the working directory)"))
    print("examples: %d%s\n" % (len(entries),
          "  at -opt %s" % ",".join(l for l in levels if l) if levels != [None] else ""))

    failed, missing, skipped, ran = [], [], [], 0
    for name, libs in entries:
        src = os.path.join(HERE, name)
        if not os.path.isfile(src):
            # a documented example that is not here is a README error, not a
            # compile error, so it is reported separately
            missing.append(name)
            print("  MISSING  %-26s (in README.md, not on disk)" % name)
            continue

        for level in levels:
            tag = "" if level is None else "-" + level
            dest = os.path.join(out_dir, name[:-4] + tag + ".obe")
            cmd = [obc, "-src", src, "-dest", dest]
            if level is not None:
                cmd += ["-opt", level]
            if libs:
                cmd += ["-lib", ",".join(libs)]

            proc = subprocess.run(cmd, capture_output=True, text=True, cwd=HERE, env=env)
            out = (proc.stdout or "") + (proc.stderr or "")
            label = "%s%s" % (name, " (%s)" % level if level else "")
            if proc.returncode != 0 or "Wrote target" not in out:
                failed.append(label)
                first = next((l for l in out.splitlines()
                              if "rror" in l or "nable" in l), out.strip().splitlines()[-1:] or [""])
                print("  FAILED   %-26s %s" % (label, first if isinstance(first, str) else first[0]))
                # the compiler names the library it wants; pass that straight through
                for line in out.splitlines():
                    if "-lib" in line and "Add it with" in line:
                        print("           %s" % line.strip())
                continue

            status = "ok"
            note = ",".join(libs) or "(no libraries)"
            if args.run:
                if name in NOT_RUN:
                    skipped.append(name)
                    status, note = "compiled", "not run: %s" % NOT_RUN[name]
                else:
                    want = expected_output(src)
                    code, got = run_example(obr, dest, env)
                    ran += 1
                    ok, note = judge(want, code, got)
                    status = "ok" if ok else "FAILED"
                    if not ok:
                        failed.append(label)
            print("  %-8s %-26s %s" % (status, label, note))

    if not args.keep:
        shutil.rmtree(out_dir, ignore_errors=True)

    print("\n%d builds ok, %d failed, %d missing" %
          (len(entries) * len(levels) - len(failed) - len(missing), len(failed), len(missing)))
    if args.run:
        print("ran %d; not run (display, database, key or network): %s" %
              (ran, ", ".join(sorted(set(skipped))) or "none"))
    if failed:
        print("failed: %s" % ", ".join(failed))
        print("A compile failure usually means README.md's '-lib' column is wrong for that row.")
    return 1 if (failed or missing) else 0


def describe(expected, actual):
    """Where the output stops fitting, for the failure message: the longest prefix of the
    expected lines that can still be matched, and the first expected line that cannot."""
    expected, actual = trim(expected), trim(actual)
    for n in range(len(expected), 0, -1):
        if matches(expected[:n] + ["..."], actual):
            if n == len(expected):
                return "the output has extra line(s) after the header's last"
            return "the header's line %d, '%s', was not found where expected" % (n + 1, expected[n])
    return "the first line is '%s', the header says '%s'" % (actual[0] if actual else "(nothing)", expected[0] if expected else "(nothing)")


if __name__ == "__main__":
    sys.exit(main())
