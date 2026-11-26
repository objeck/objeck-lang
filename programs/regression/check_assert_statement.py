#!/usr/bin/env python3
"""
The `assert` statement: a failed assertion must say where and why, and exit non-zero.

Usage: check_assert_statement.py <bin_dir>

The regression runners judge a test by its exit code alone, so the one property that matters is
that a failing `assert` makes `obr` exit with code 1 and print the `FAIL:` line (file:line, the
source text of the expression, the label, and both operand values of a comparison). A passing
program cannot show that; this compiles the failing shapes in assert_statement/ and checks each:

  fail_compare     a comparison with a label: both operands, exit 1, nothing runs after it
  fail_call        an operand that is a call is evaluated once (the call prints once)
  fail_bool        a bare Bool expression: no operand line
  fail_keep_going  AssertCheck->KeepGoing(true): every failure is reported, AssertCheck->Finish() exits 1

Each runs with the default JIT and with OBJECK_JIT_THRESHOLD=1, and with obc at -opt s0 and s3.
The programs that must be rejected (bad_assert_*.obs) are in the regression suite proper.
"""
import os, subprocess, sys, tempfile

EXE = ".exe" if os.name == "nt" else ""
HERE = os.path.dirname(os.path.abspath(__file__))
FIXTURES = os.path.join(HERE, "assert_statement")

# name -> (lines that must appear in order in stdout, lines that must not appear)
CASES = {
    "fail_compare": (
        ["FAIL: fail_compare.obs:5: assert x + 1 = 5 : x plus one is five",
         "      left = 4, right = 5"],
        ["NOT REACHED"]),
    "fail_call": (
        ["made",
         "FAIL: fail_call.obs:7: assert Make(2) = 5",
         "      left = 4, right = 5"],
        ["NOT REACHED"]),
    "fail_bool": (
        ["FAIL: fail_bool.obs:4: assert ok : flag is off"],
        ["left ="]),
    "fail_keep_going": (
        ["FAIL: fail_keep_going.obs:4: assert 1 = 2 : first",
         "      left = 1, right = 2",
         "between",
         "FAIL: fail_keep_going.obs:6: assert 3 > 4 : second",
         "      left = 3, right = 4",
         "before finish",
         "FAIL: 2 assertion(s) failed"],
        ["NOT REACHED", "holds"]),
}


def run(cmd, env, cwd):
    p = subprocess.run(cmd, cwd=cwd, env=env, capture_output=True, timeout=300)
    return p.returncode, (p.stdout + p.stderr).decode("utf-8", "replace").replace("\r\n", "\n")


def main():
    if len(sys.argv) != 2:
        print(__doc__)
        return 2
    bin_dir = os.path.abspath(sys.argv[1])
    obc = os.path.join(bin_dir, "obc" + EXE)
    obr = os.path.join(bin_dir, "obr" + EXE)
    env = dict(os.environ)
    env["OBJECK_LIB_PATH"] = os.path.join(os.path.dirname(bin_dir), "lib")
    failures = []
    with tempfile.TemporaryDirectory(prefix="assert-statement-") as work:
        for name, (expected, forbidden) in CASES.items():
            for opt in ("s0", "s3"):
                exe = os.path.join(work, "%s_%s.obe" % (name, opt))
                code, out = run([obc, "-src", name + ".obs", "-dest", exe, "-opt", opt],
                                env, FIXTURES)
                if code != 0 or not os.path.isfile(exe):
                    failures.append("%s (%s): did not compile:\n%s" % (name, opt, out))
                    continue
                for jit in ("default", "jit1"):
                    run_env = dict(env)
                    if jit == "jit1":
                        run_env["OBJECK_JIT_THRESHOLD"] = "1"
                    code, out = run([obr, exe], run_env, work)
                    tag = "%s (%s, %s)" % (name, opt, jit)
                    if code != 1:
                        failures.append("%s: exit code %d, expected 1:\n%s" % (tag, code, out))
                        continue
                    lines = out.split("\n")
                    at = 0
                    for want in expected:
                        try:
                            at = lines.index(want, at) + 1
                        except ValueError:
                            failures.append("%s: missing line %r (in order):\n%s" % (tag, want, out))
                            break
                    for bad in forbidden:
                        if any(bad in line for line in lines):
                            failures.append("%s: unexpected %r:\n%s" % (tag, bad, out))
    if failures:
        print("assert statement check FAILED (%d):" % len(failures))
        for f in failures:
            print("-", f)
        return 1
    print("assert statement check passed (%d programs x 2 optimizer levels x 2 JIT modes)" % len(CASES))
    return 0


if __name__ == "__main__":
    sys.exit(main())
