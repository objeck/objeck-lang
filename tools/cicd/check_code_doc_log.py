#!/usr/bin/env python3
"""A code_doc run must not have dropped anything.

Usage:
  check_code_doc_log.py <code_doc.log>      fail if the log holds a rejection line
  check_code_doc_log.py --self-test         the patterns, against sample lines (no toolchain)
  check_code_doc_log.py --fixture <bin-dir> compile code_doc with <bin-dir>/obc, run it on a
                                            deliberately bad fixture (must be flagged) and on
                                            every library source (must be clean)

code_doc reports a doc comment it cannot parse in several different ways and keeps going
with exit status 0, so the member (or the whole bundle or class) is simply missing from
the API reference. The guard used to grep for 'ERROR:' only; mcp_agent.obs then shipped a
bundle comment with a code fence, which prints '*** Unable to parse bundle block' and
passed CI. Every failure output of core/lib/code_doc/doc_parser.obs and doc_html.obs:

  ERROR: '<comment>'                         a member's doc comment
  *** Unable to parse bundle block ...       (also enum/consts, class, interface blocks)
  --- Unable to parse source file ---
  --- Unable load header CSS template ---
"""
import glob
import os
import re
import subprocess
import sys
import tempfile

ROOT = os.path.abspath(os.path.join(os.path.dirname(__file__), "..", ".."))
# not anchored to the line start: code_doc prints progress glyphs without a newline, so the
# message can follow one on the same line
BAD = re.compile(r"(?:^|[^A-Za-z0-9])(ERROR: '|\*\*\* Unable|--- Unable)")


def findings(text):
    return [l for l in text.splitlines() if BAD.search(l)]


def check_log(path):
    with open(path, encoding="utf-8", errors="replace") as f:
        bad = findings(f.read())
    if bad:
        print("code_doc rejected %d doc comment(s) or block(s); they would be missing from api.zip:" % len(bad))
        for l in bad[:20]:
            print("  " + l[:200])
        return 1
    print("code_doc log is clean")
    return 0


SAMPLES = ["ERROR: '#~", "\u00b7*** Unable to parse bundle block. text='x' ***", "*** Unable to parse bundle block. text='x' ***",
           "*** Unable to parse enum/consts block. Text='x' ***", "*** Unable to parse class block. Text='x' ***",
           "*** Unable to parse interface block. Text='x' ***", "--- Unable to parse source file ---",
           "--- Unable load header CSS template ---"]


def self_test():
    for s in SAMPLES:
        if not findings(s):
            print("FAIL: not flagged: " + s)
            return 1
    for s in ["\tProcessed class: name='Error'", "Writing API files...", "['../x.obs']"]:
        if findings(s):
            print("FAIL: flagged a normal line: " + s)
            return 1
    print("PASS: %d failure outputs flagged, normal lines not" % len(SAMPLES))
    return 0


FIXTURE = """#~
A bundle comment with a code fence: ParseDesc stops at the fence and the bundle is lost.

```
x := 1;
```

~#
bundle Fixture.Bad {
	class Holder {
		function : Id() ~ Int {
			return 1;
		}
	}
}
"""


def run_code_doc(bin_dir, work, sources):
    # absolute: code_doc runs with a different cwd below, which broke a relative bin dir
    bin_dir = os.path.abspath(bin_dir)
    obc = os.path.join(bin_dir, "obc")
    obr = os.path.join(bin_dir, "obr")
    for tool in (obc, obr):
        if not (os.path.exists(tool) or os.path.exists(tool + ".exe")):
            print("FAIL: %s not found; pass the deploy tree's bin directory" % tool)
            sys.exit(2)
    deploy = os.path.dirname(bin_dir)
    lib = os.path.join(deploy, "lib")
    tools = os.path.join(ROOT, "core", "lib", "code_doc")
    obe = os.path.join(work, "code_doc.obe")
    env = dict(os.environ, OBJECK_LIB_PATH=lib)
    r = subprocess.run([obc, "-src", os.path.join(tools, "doc_html.obs") + "," + os.path.join(tools, "doc_parser.obs"),
                        "-lib", "cipher,json,xml,misc,net", "-dest", obe], capture_output=True, text=True, encoding="utf-8", errors="replace", env=env)
    if r.returncode != 0:
        print(r.stdout + r.stderr)
        sys.exit(2)
    # code_doc writes ../html next to its cwd, so run it from a scratch bin directory
    cwd = os.path.join(work, "bin")
    os.makedirs(cwd, exist_ok=True)
    r = subprocess.run([obr, obe, os.path.join(tools, "templates"), "0.0.0"] + sources,
                       capture_output=True, text=True, encoding="utf-8", errors="replace", cwd=cwd, env=env)
    return r.stdout + r.stderr


def fixture_test(bin_dir):
    with tempfile.TemporaryDirectory() as work:
        bad = os.path.join(work, "bad_bundle.obs")
        with open(bad, "w") as f:
            f.write(FIXTURE)
        out = run_code_doc(bin_dir, work, [bad])
        if not findings(out):
            print("FAIL: the guard did not flag a bundle comment with a code fence. Output:\n" + out[-1500:])
            return 1
        print("PASS: bundle comment with a code fence is flagged: " + findings(out)[0][:80])
        srcs = sorted(glob.glob(os.path.join(ROOT, "core", "compiler", "lib_src", "*.obs")))
        out = run_code_doc(bin_dir, work, srcs)
        bad_lines = findings(out)
        if bad_lines:
            print("FAIL: the current tree is flagged:")
            for l in bad_lines[:10]:
                print("  " + l[:200])
            return 1
        print("PASS: all %d library sources are clean" % len(srcs))
    return 0


def main(argv):
    if argv[:1] == ["--self-test"]:
        return self_test()
    if argv[:1] == ["--fixture"] and len(argv) == 2:
        return self_test() or fixture_test(argv[1])
    if len(argv) == 1 and not argv[0].startswith("-"):
        return check_log(argv[0])
    print(__doc__)
    return 2


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
