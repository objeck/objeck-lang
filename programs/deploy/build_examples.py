#!/usr/bin/env python3
"""Compile every example that ships in this folder.

The library list for each example is read from README.md rather than kept here,
because README.md is what a reader follows and a second copy would drift from
it silently. A row whose '-lib' line is wrong fails this script, which is the
point: the table is executable documentation rather than a claim.

  python build_examples.py                 compile all, report
  python build_examples.py --keep          leave the .obe files behind
  python build_examples.py --only json     compile the examples matching a name

Exit code is 0 only when every example compiled.
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

    # repo layout, whatever the deploy tree is called
    pattern = os.path.join(HERE, "..", "..", "core", "release", "deploy*", "bin", exe)
    for candidate in sorted(glob.glob(pattern)):
        if os.path.isfile(candidate):
            return os.path.abspath(candidate)

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


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--keep", action="store_true",
                    help="leave the compiled .obe files in place")
    ap.add_argument("--only", metavar="TEXT",
                    help="only examples whose name contains TEXT")
    args = ap.parse_args()

    obc = find_obc()
    env = library_env(obc)
    entries = read_table()
    if args.only:
        entries = [e for e in entries if args.only in e[0]]
    if not entries:
        sys.exit("no examples matched")

    out_dir = tempfile.mkdtemp(prefix="obc-examples-") if not args.keep else HERE
    print("obc     : %s" % obc)
    print("lib     : %s" % env.get("OBJECK_LIB_PATH", "(from the working directory)"))
    print("examples: %d\n" % len(entries))

    failed, missing = [], []
    for name, libs in entries:
        src = os.path.join(HERE, name)
        if not os.path.isfile(src):
            # a documented example that is not here is a README error, not a
            # compile error, so it is reported separately
            missing.append(name)
            print("  MISSING  %-26s (in README.md, not on disk)" % name)
            continue

        dest = os.path.join(out_dir, name[:-4] + ".obe")
        cmd = [obc, "-src", src, "-dest", dest]
        if libs:
            cmd += ["-lib", ",".join(libs)]

        proc = subprocess.run(cmd, capture_output=True, text=True, cwd=HERE, env=env)
        out = (proc.stdout or "") + (proc.stderr or "")
        if proc.returncode == 0 and "Wrote target" in out:
            print("  ok       %-26s %s" % (name, ",".join(libs) or "(no libraries)"))
        else:
            failed.append(name)
            first = next((l for l in out.splitlines()
                          if "rror" in l or "nable" in l), out.strip().splitlines()[-1:] or [""])
            print("  FAILED   %-26s %s" % (name, first if isinstance(first, str) else first[0]))
            # the compiler names the library it wants; pass that straight through
            for line in out.splitlines():
                if "-lib" in line and "Add it with" in line:
                    print("           %s" % line.strip())

    if not args.keep:
        shutil.rmtree(out_dir, ignore_errors=True)

    print("\n%d compiled, %d failed, %d missing" %
          (len(entries) - len(failed) - len(missing), len(failed), len(missing)))
    if failed:
        print("failed: %s" % ", ".join(failed))
        print("A failure usually means README.md's '-lib' column is wrong for that row.")
    return 1 if (failed or missing) else 0


if __name__ == "__main__":
    sys.exit(main())
