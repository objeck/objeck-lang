#!/usr/bin/env python3
"""
obc writes the same file for the same source, with -debug as without.

Usage: check_obc_debug_reproducible.py <bin_dir>

<bin_dir> holds obc; the libraries are read from the sibling lib/ directory.

A debug image carries a line number for every instruction. A character literal's token never had its line
set: it kept the line of whatever the scanner's look-ahead slot held before, and inside an interpolation
("{$'z'}", scanned by a Scanner of its own) the slot was new and the number was uninitialized memory. So
'obc -debug' on a program with a character literal in an interpolation wrote a different file on every
run (found by comparing the regression corpus's images between two compilers; core_string_interp_literal.obs
was the one that never matched itself).

The fixture below has that shape and the other places a literal can stand. It is compiled five times with
-debug and five times without, at -opt s0 and s3; within each set every file must be byte for byte the first.
The debug build is then run, so the positions are not bought with wrong code.
"""
import os
import subprocess
import sys
import tempfile
import zlib

EXE = ".exe" if os.name == "nt" else ""
RUNS = 5

FIXTURE = r'''
class Reproducible {
	function : Show(c : Char, s : String) ~ String {
		return "{$c}{$s}";
	}

	function : Main(args : String[]) ~ Nil {
		"{$'z'}"->PrintLine();
		"[{$'a'}] [{$'\n'}] [{$'\t'}] [{$'\\'}]"->Size()->PrintLine();
		"{$0x1F} {$-7} {$2.5} {$true}"->PrintLine();
		c := 'q';
		d := 'A';
		Show(c, "{$d}")->PrintLine();
		Show('x', "y")->PrintLine();
		if(c = 'q' & d <> '\0') {
			"{$c}{$'!'}"->PrintLine();
		};
		select(c) {
			label 'q': {
				"{$'s'}"->PrintLine();
			}

			other: {
				"other"->PrintLine();
			}
		};
		chars := ['a', 'b', 'c'];
		"{$chars[1]}{$'-'}{$chars->Size()}"->PrintLine();
	}
}
'''


def main():
    if len(sys.argv) != 2:
        print(__doc__)
        return 2
    bin_dir = os.path.abspath(sys.argv[1])
    obc = os.path.join(bin_dir, "obc" + EXE)
    env = dict(os.environ)
    env["OBJECK_LIB_PATH"] = os.path.join(os.path.dirname(bin_dir), "lib")
    failures = []

    def check(name, cond, detail=""):
        print("  [%s] %s%s" % ("PASS" if cond else "FAIL", name, "" if cond else "\n         " + str(detail)[-600:]))
        if not cond:
            failures.append(name)

    with tempfile.TemporaryDirectory(prefix="reproducible-") as work:
        src = os.path.join(work, "reproducible.obs")
        with open(src, "w", encoding="utf-8", newline="\n") as f:
            f.write(FIXTURE)

        for opt in ("s0", "s3"):
            for debug in (True, False):
                label = "-opt %s%s" % (opt, " -debug" if debug else "")
                images = []
                for run in range(RUNS):
                    obe = os.path.join(work, "out.obe")
                    if os.path.exists(obe):
                        os.remove(obe)
                    cmd = [obc, "-src", src, "-dest", obe, "-opt", opt] + (["-debug"] if debug else [])
                    p = subprocess.run(cmd, cwd=work, env=env, capture_output=True, timeout=120)
                    if p.returncode != 0 or not os.path.exists(obe):
                        check("%s: the fixture compiles" % label, False, (p.stdout + p.stderr).decode("utf-8", "replace"))
                        images = None
                        break
                    with open(obe, "rb") as f:
                        images.append(f.read())
                if images is None:
                    continue
                differing = [i + 1 for i in range(1, RUNS) if images[i] != images[0]]
                detail = ""
                if differing:
                    a, b = zlib.decompress(images[0][4:]), zlib.decompress(images[differing[0] - 1][4:])
                    where = [i for i in range(min(len(a), len(b))) if a[i] != b[i]][:8]
                    detail = "run(s) %s differ from run 1; first differing image offsets %s (sizes %d, %d)" % (differing, where, len(a), len(b))
                check("%s: %d compiles write the same file" % (label, RUNS), not differing, detail)

        # the fixture runs and prints what it should, so the positions fixed are not bought with wrong code
        obr = os.path.join(bin_dir, "obr" + EXE)
        obe = os.path.join(work, "run.obe")
        p = subprocess.run([obc, "-src", src, "-dest", obe, "-debug"], cwd=work, env=env, capture_output=True, timeout=120)
        if p.returncode == 0 and os.path.exists(obr):
            r = subprocess.run([obr, obe], cwd=work, env=env, capture_output=True, timeout=120)
            out = r.stdout.decode("utf-8", "replace").replace("\r\n", "\n")
            nl = chr(10)
            want = "z" + nl + "15" + nl + "31 -7 2.500000 true" + nl + "qA" + nl + "xy" + nl + "q!" + nl + "s" + nl + "b-3" + nl
            check("the debug build of the fixture runs and prints the expected text", r.returncode == 0 and out == want, repr(out))

    if failures:
        print("FAIL: %d check(s) failed" % len(failures))
        return 1
    print("PASS: obc output is reproducible, with and without -debug")
    return 0


if __name__ == "__main__":
    sys.exit(main())
