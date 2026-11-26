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


def make(tmp, shards, total, tweak=None):
    """A complete, passing set of shard results for `total` tests; tweak(i, pass, counts) edits it."""
    tests = os.path.join(tmp, "tests")
    os.makedirs(tests)
    for n in range(total):
        open(os.path.join(tests, "t%d.obs" % n), "w").close()
    for i in range(shards):
        d = os.path.join(tmp, "shards", "winshard-%s-%d" % (PLATFORM, i))
        os.makedirs(d)
        mine = len(range(i, total, shards))
        for p in ("default", "jit1"):
            c = dict(passed=mine - 1, skipped=1, failed=0, expected=mine)
            if tweak:
                c = tweak(i, p, c)
            if c is not None:
                with open(os.path.join(d, "counts_%s.txt" % p), "w") as f:
                    f.write(" ".join("%s=%s" % kv for kv in c.items()))
    return tests, os.path.join(tmp, "shards")


def run(tests, shards_dir, shards):
    return subprocess.run([sys.executable, "-I", SCRIPT, "--platform", PLATFORM, "--shards", str(shards),
                           "--tests-dir", tests, "--shards-dir", shards_dir],
                          capture_output=True, text=True)


class Shards(unittest.TestCase):
    def go(self, tweak=None, shards=4, total=10, expect=0):
        with tempfile.TemporaryDirectory() as tmp:
            tests, sd = make(tmp, shards, total, tweak)
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

    def test_unparseable_fails(self):
        self.go(lambda i, p, c: dict(passed="missing", skipped=0, failed=0, expected=1) if i == 1 else c, expect=1)

    def test_extra_shard_count_expected_fails(self):
        with tempfile.TemporaryDirectory() as tmp:
            tests, sd = make(tmp, 3, 9)
            self.assertEqual(run(tests, sd, 4).returncode, 1)


if __name__ == "__main__":
    unittest.main()
