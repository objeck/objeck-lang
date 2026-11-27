#!/usr/bin/env python3
"""check_windows_shards.py must fail on a lost, short or failing shard, not only on a bad count."""
import os
import subprocess
import sys
import tempfile
import unittest

HERE = os.path.dirname(os.path.abspath(__file__))
SCRIPT = os.path.join(HERE, "check_windows_shards.py")
PLATFORM = "windows-x64"


T0_SOURCE = "# SKIP_ALLOWED: no GPU" + chr(10) + '"SKIP: no GPU"->PrintLine();' + chr(10)


def make(tmp, shards, total, tweak=None, logtweak=None, t0=T0_SOURCE):
    """A complete, passing set of shard results for `total` tests; tweak(i, pass, counts) edits it.

    Test t0 can print SKIP: (its source says so) and is the one each pass skips; every other test
    passes. logtweak(i, pass, lines) edits the lines of a shard's log."""
    tests = os.path.join(tmp, "tests")
    os.makedirs(tests)
    for n in range(total):
        with open(os.path.join(tests, "t%d.obs" % n), "w") as f:
            if n == 0:
                f.write(t0)
    for i in range(shards):
        d = os.path.join(tmp, "shards", "winshard-%s-%d" % (PLATFORM, i))
        os.makedirs(d)
        mine = list(range(i, total, shards))
        skips = 1 if 0 in mine else 0
        for p in ("default", "jit1"):
            c = dict(passed=len(mine) - skips, skipped=skips, failed=0, expected=len(mine))
            if tweak:
                c = tweak(i, p, c)
            if c is not None:
                with open(os.path.join(d, "counts_%s.txt" % p), "w") as f:
                    f.write(" ".join("%s=%s" % kv for kv in c.items()))
            lines = []
            for n in mine:
                lines.append("Running: t%d..." % n)
                lines.append("  SKIP" if n == 0 else "  PASS")
            if logtweak:
                lines = logtweak(i, p, lines)
            if lines is not None:
                with open(os.path.join(d, "regression_%s.log" % p), "w") as f:
                    f.write(chr(10).join(lines) + chr(10))
    return tests, os.path.join(tmp, "shards")


def run(tests, shards_dir, shards):
    return subprocess.run([sys.executable, "-I", SCRIPT, "--platform", PLATFORM, "--shards", str(shards),
                           "--tests-dir", tests, "--shards-dir", shards_dir],
                          capture_output=True, text=True)


class Shards(unittest.TestCase):
    def go(self, tweak=None, shards=4, total=10, expect=0, logtweak=None, t0=T0_SOURCE):
        with tempfile.TemporaryDirectory() as tmp:
            tests, sd = make(tmp, shards, total, tweak, logtweak, t0)
            r = run(tests, sd, shards)
            self.assertEqual(r.returncode, expect, r.stdout + r.stderr)
            return r.stdout

    def test_all_present_passes(self):
        self.go()

    def test_lost_shard_fails(self):
        out = self.go(lambda i, p, c: None if (i, p) == (2, "jit1") else c, expect=1)
        self.assertIn("shard 2: counts file missing", out)

    def test_failed_test_fails(self):
        self.go(lambda i, p, c: dict(c, passed=c["passed"] - 1, failed=1) if (i, p) == (1, "default") else c, expect=1)

    def test_short_shard_fails(self):
        self.go(lambda i, p, c: dict(c, passed=c["passed"] - 1) if i == 0 else c, expect=1)

    def test_shard_handed_too_few_fails(self):
        self.go(lambda i, p, c: dict(passed=0, skipped=0, failed=0, expected=0) if i == 3 else c, expect=1)

    # The counts alone cannot tell a run that executed the suite from one that did not.

    def test_everything_skipped_fails(self):
        # zero passed, zero failed, the expected number "accounted for" as skips
        def counts(i, p, c):
            return dict(c, passed=0, skipped=c["expected"])
        def log(i, p, lines):
            return [("  SKIP" if x == "  PASS" else x) for x in lines]
        out = self.go(counts, logtweak=log, expect=1)
        self.assertIn("was skipped, and nothing in it says it can be", out)
        self.assertIn("no test passed", out)

    # A test is allowed to skip by a directive, not by mentioning the marker.

    def test_marker_in_a_comment_is_not_a_declaration(self):
        # t0 skips (a shared helper printed SKIP: and the test exited 0) but only a comment names it
        out = self.go(t0='# this test could print "SKIP: no GPU" one day' + chr(10), expect=1)
        self.assertIn("t0 was skipped, and nothing in it says it can be", out)

    def test_helper_printed_skip_without_directive_fails(self):
        out = self.go(t0="Setup->Run();" + chr(10), expect=1)
        self.assertIn("t0 was skipped", out)

    def test_directive_without_a_reason_declares_nothing(self):
        out = self.go(t0="# SKIP_ALLOWED:" + chr(10) + '"SKIP: x"->PrintLine();' + chr(10), expect=1)
        self.assertIn("t0 was skipped", out)

    def test_directive_after_the_header_declares_nothing(self):
        late = chr(10).join(["# filler"] * 25) + chr(10) + "# SKIP_ALLOWED: late" + chr(10) + '"SKIP: x"->PrintLine();' + chr(10)
        self.assertIn("t0 was skipped", self.go(t0=late, expect=1))

    def test_directive_with_a_helper_that_prints_passes(self):
        # the declaration is the directive; a helper may print the marker
        out = self.go(t0="# SKIP_ALLOWED: no GPU" + chr(10) + "Setup->Run();" + chr(10))
        self.assertIn("Skipped (each declares SKIP_ALLOWED): t0 (no GPU)", out)

    def lint(self, t0):
        with tempfile.TemporaryDirectory() as tmp:
            tests, _ = make(tmp, 2, 4, t0=t0)
            r = subprocess.run([sys.executable, "-I", SCRIPT, "--lint-skips", "--tests-dir", tests], capture_output=True, text=True)
            return r.returncode, r.stdout

    def test_lint_accepts_a_declared_skip(self):
        self.assertEqual(self.lint(T0_SOURCE)[0], 0)

    def test_lint_rejects_a_print_with_no_directive(self):
        rc, out = self.lint('"SKIP: no GPU"->PrintLine();' + chr(10))
        self.assertEqual(rc, 1)
        self.assertIn("t0.obs prints SKIP: but has no", out)

    def test_lint_rejects_a_marker_only_in_a_comment(self):
        rc, out = self.lint('# "SKIP: no GPU" is printed by Setup' + chr(10))
        self.assertEqual(rc, 0, out)  # a comment prints nothing and declares nothing

    def test_lint_rejects_a_directive_with_nothing_to_print(self):
        rc, out = self.lint("# SKIP_ALLOWED: no GPU" + chr(10) + "x := 1;" + chr(10))
        self.assertEqual(rc, 1)
        self.assertIn("declares SKIP_ALLOWED but no line of it prints SKIP:", out)

    def test_skip_of_a_test_that_can_skip_passes(self):
        self.go()  # t0 skips in both passes and says it can

    def test_missing_log_fails(self):
        out = self.go(logtweak=lambda i, p, lines: None if (i, p) == (1, "default") else lines, expect=1)
        self.assertIn("log missing", out)

    def test_other_tests_than_the_suite_fail(self):
        # the right number of tests ran, but not these
        out = self.go(logtweak=lambda i, p, lines: [x.replace("t1...", "zz...") for x in lines], expect=1)
        self.assertIn("t1 never ran", out)
        self.assertIn("zz ran but is not a test in the suite", out)

    def test_a_test_run_twice_fails(self):
        out = self.go(logtweak=lambda i, p, lines: lines + ["Running: t0...", "  SKIP"] if (i, p) == (3, "jit1") else lines, expect=1)
        self.assertIn("t0 ran twice", out)

    def test_a_test_that_never_finished_fails(self):
        out = self.go(logtweak=lambda i, p, lines: lines[:-1] if (i, p) == (2, "default") else lines, expect=1)
        self.assertIn("started and never finished", out)

    def test_log_that_disagrees_with_counts_fails(self):
        out = self.go(logtweak=lambda i, p, lines: [x.replace("  PASS", "  FAIL") for x in lines] if (i, p) == (1, "jit1") else lines, expect=1)
        self.assertIn("the counts say", out)

    def test_unparseable_fails(self):
        self.go(lambda i, p, c: dict(passed="missing", skipped=0, failed=0, expected=1) if i == 1 else c, expect=1)

    def test_extra_shard_count_expected_fails(self):
        with tempfile.TemporaryDirectory() as tmp:
            tests, sd = make(tmp, 3, 9)
            self.assertEqual(run(tests, sd, 4).returncode, 1)


if __name__ == "__main__":
    unittest.main()
