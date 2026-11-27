#!/usr/bin/env python3
"""gate_manifest.py: the manifest must make each way a gate can go green without its cases
running a failure. Fixture logs in both runners' formats; nothing built."""
import json
import os
import subprocess
import sys
import tempfile
import unittest

HERE = os.path.dirname(os.path.abspath(__file__))
TOOL = os.path.join(HERE, "gate_manifest.py")

SH_LOG = """========================================
  Objeck Regression Test Suite
========================================

Running: alpha...
  [PASS] (1s)
Running: beta...
  [PASS] (runtime error as expected, 0s)
Running: gamma...
  [SKIP] SKIP: no thing here
Running: delta...
  [FAIL] runtime error (exit 1)
  --- output ---
    Results: 9 passed, 9 skipped, 9 failed
  --------------

========================================
  Results: 2 passed, 1 skipped, 1 failed
========================================
"""
BETA_CMD = "Running: beta...\n  PASS (runtime error as expected)\n"
CMD_LOG = ("Running: alpha...\n  PASS\n" + BETA_CMD + "Running: gamma...\n  SKIP\n"
           "========================================\n"
           "  Results: 2 passed, 1 skipped, 0 failed\n"
           "========================================\n")
ALLOW = {"format": 1, "entries": [
    {"test": "gamma", "class": "skip_allowed", "platforms": ["*"], "modes": ["*"], "reason": "no thing"},
    {"test": "only_win", "class": "skip_allowed", "platforms": ["windows-*"], "modes": ["default"], "reason": "r"}]}


def run(*args):
    r = subprocess.run([sys.executable, "-I", TOOL] + list(args), capture_output=True, text=True)
    return r.returncode, r.stdout + r.stderr


class Base(unittest.TestCase):
    def setUp(self):
        self.tmp = tempfile.TemporaryDirectory()
        self.addCleanup(self.tmp.cleanup)
        self.allow = self.write("allow.json", json.dumps(ALLOW))

    def write(self, name, text):
        p = os.path.join(self.tmp.name, name)
        with open(p, "w", newline="") as f:
            f.write(text)
        return p

    def collect(self, text, name="m.json", mode="default", platform="linux-x64"):
        log = self.write(name + "." + mode + ".log", text)
        out = os.path.join(self.tmp.name, name)
        rc, msg = run("collect", "--log", log, "--platform", platform, "--mode", mode, "--out", out)
        return rc, msg, out

    def manifest(self, tests, name, platform="linux-x64", mode="default"):
        """tests: {name: outcome}"""
        p = os.path.join(self.tmp.name, name)
        body = {"sources": [], "counts": {}, "tests": {n: {"outcome": o, "reason": ""} for n, o in tests.items()}}
        with open(p, "w") as f:
            json.dump({"format": 1, "platform": platform, "modes": {mode: body}}, f)
        return p


class Collect(Base):
    def test_sh_log(self):
        rc, msg, out = self.collect(SH_LOG)
        self.assertEqual(rc, 0, msg)
        t = json.load(open(out))["modes"]["default"]["tests"]
        self.assertEqual({n: v["outcome"] for n, v in t.items()},
                         {"alpha": "PASS", "beta": "PASS", "gamma": "SKIP", "delta": "FAIL"})

    def test_cmd_log(self):
        rc, msg, out = self.collect(CMD_LOG)
        self.assertEqual(rc, 0, msg)
        self.assertEqual(json.load(open(out))["modes"]["default"]["counts"], {"passed": 2, "skipped": 1, "failed": 0})

    def test_modes_merge_into_one_file(self):
        _, _, out = self.collect(CMD_LOG)
        self.collect(CMD_LOG, name="m.json", mode="jit1")
        self.assertEqual(sorted(json.load(open(out))["modes"]), ["default", "jit1"])

    def test_empty_log_fails(self):
        rc, msg, _ = self.collect("nothing ran\n")
        self.assertNotEqual(rc, 0)
        self.assertIn("no test results", msg)

    def test_missing_results_line_fails(self):
        rc, msg, _ = self.collect(CMD_LOG.replace("Results:", "Totals:"))
        self.assertNotEqual(rc, 0)
        self.assertIn("Results", msg)

    def test_counts_that_disagree_fail(self):
        rc, msg, _ = self.collect(CMD_LOG.replace("2 passed, 1 skipped", "3 passed, 1 skipped"))
        self.assertNotEqual(rc, 0)
        self.assertIn("Results lines say", msg)

    def test_test_that_never_reported_fails(self):
        rc, msg, _ = self.collect(CMD_LOG.replace("Running: gamma...\n  SKIP\n", "Running: gamma...\n"))
        self.assertNotEqual(rc, 0)
        self.assertIn("never reported", msg)

    def test_test_run_twice_fails(self):
        rc, msg, _ = self.collect("Running: a...\n  PASS\nRunning: a...\n  PASS\n  Results: 2 passed, 0 skipped, 0 failed\n")
        self.assertNotEqual(rc, 0)
        self.assertIn("ran twice", msg)

    def test_missing_log_fails(self):
        rc, msg = run("collect", "--log", os.path.join(self.tmp.name, "nope.log"), "--platform", "p", "--mode", "default",
                      "--out", os.path.join(self.tmp.name, "o.json"))
        self.assertNotEqual(rc, 0)


class Compare(Base):
    def setUp(self):
        super().setUp()
        self.base = self.manifest({"a": "PASS", "b": "PASS", "gamma": "SKIP"}, "base.json")

    def cmp(self, cand, *extra):
        return run("compare", self.base, cand, "--allowlist", self.allow, *extra)

    def test_identical_passes(self):
        rc, out = self.cmp(self.manifest({"a": "PASS", "b": "PASS", "gamma": "SKIP"}, "c.json"))
        self.assertEqual(rc, 0, out)

    def test_missing_test_fails_even_when_a_new_one_keeps_the_count(self):
        rc, out = self.cmp(self.manifest({"a": "PASS", "gamma": "SKIP", "z": "PASS"}, "c.json"))
        self.assertNotEqual(rc, 0)
        self.assertIn("b is in the baseline and missing", out)

    def test_pass_lost_fails(self):
        rc, out = self.cmp(self.manifest({"a": "PASS", "b": "FAIL", "gamma": "SKIP"}, "c.json"))
        self.assertNotEqual(rc, 0)
        self.assertIn("b was PASS and is FAIL", out)

    def test_pass_to_allowed_skip_still_fails(self):
        base = self.manifest({"gamma": "PASS"}, "b5.json")
        rc, out = run("compare", base, self.manifest({"gamma": "SKIP"}, "c5.json"), "--allowlist", self.allow)
        self.assertNotEqual(rc, 0)
        self.assertIn("gamma was PASS and is SKIP", out)

    def test_unlisted_skip_fails(self):
        base = self.manifest({"a": "SKIP"}, "base2.json")
        rc, out = run("compare", base, self.manifest({"a": "SKIP"}, "c2.json"), "--allowlist", self.allow)
        self.assertNotEqual(rc, 0)
        self.assertIn("a was skipped", out)

    def test_skip_on_other_platform_fails(self):
        base = self.manifest({"only_win": "SKIP"}, "b3.json", platform="windows-x64")
        ok, out = run("compare", base, self.manifest({"only_win": "SKIP"}, "c3.json", platform="windows-x64"), "--allowlist", self.allow)
        self.assertEqual(ok, 0, out)
        base = self.manifest({"only_win": "SKIP"}, "b4.json", platform="linux-x64")
        rc, out = run("compare", base, self.manifest({"only_win": "SKIP"}, "c4.json", platform="linux-x64"), "--allowlist", self.allow)
        self.assertNotEqual(rc, 0)
        self.assertIn("only_win was skipped", out)

    def test_skip_in_other_mode_fails(self):
        base = self.manifest({"only_win": "SKIP"}, "b6.json", platform="windows-x64", mode="jit1")
        rc, out = run("compare", base, self.manifest({"only_win": "SKIP"}, "c6.json", platform="windows-x64", mode="jit1"),
                      "--allowlist", self.allow)
        self.assertNotEqual(rc, 0)

    def test_platform_mismatch_fails(self):
        rc, out = self.cmp(self.manifest({"a": "PASS", "b": "PASS", "gamma": "SKIP"}, "c.json", platform="macos-arm64"))
        self.assertNotEqual(rc, 0)
        self.assertIn("platform differs", out)

    def test_missing_mode_fails(self):
        rc, out = self.cmp(self.manifest({"a": "PASS", "b": "PASS", "gamma": "SKIP"}, "c.json", mode="jit1"))
        self.assertNotEqual(rc, 0)
        self.assertIn("mode default is in the baseline", out)

    def test_empty_candidate_fails(self):
        rc, out = self.cmp(self.manifest({}, "c.json"))
        self.assertNotEqual(rc, 0)

    def test_additions_reported_not_failed_without_a_file(self):
        rc, out = self.cmp(self.manifest({"a": "PASS", "b": "PASS", "gamma": "SKIP", "new": "PASS"}, "c.json"))
        self.assertEqual(rc, 0, out)
        self.assertIn("+ default: new", out)
        self.assertIn("report only", out)

    def test_additions_must_be_reviewed_when_a_file_is_given(self):
        cand = self.manifest({"a": "PASS", "b": "PASS", "gamma": "SKIP", "new": "PASS"}, "c.json")
        empty = self.write("rev0.txt", "# nothing reviewed\n")
        rc, out = self.cmp(cand, "--reviewed-additions", empty)
        self.assertNotEqual(rc, 0)
        self.assertIn("new is new and not in the reviewed-additions file", out)
        listed = self.write("rev1.txt", "new  # reviewed 2026-11-27\n")
        rc, out = self.cmp(cand, "--reviewed-additions", listed)
        self.assertEqual(rc, 0, out)

    def test_end_to_end_log_against_itself_and_doctored(self):
        _, _, same = self.collect(CMD_LOG, name="real.json")
        rc, out = run("compare", same, same, "--allowlist", self.allow)
        self.assertEqual(rc, 0, out)
        doctored_log = CMD_LOG.replace(BETA_CMD, "").replace("2 passed", "1 passed")
        _, _, doctored = self.collect(doctored_log, name="doctored.json")
        rc, out = run("compare", same, doctored, "--allowlist", self.allow)
        self.assertNotEqual(rc, 0)
        self.assertIn("beta is in the baseline and missing", out)


class Judge(Base):
    def judge(self, log_text, exit_code=0, baseline=None, mode="default", platform="linux-x64"):
        log = self.write("j.log", log_text)
        args = ["judge", "--log", log, "--exit-code", str(exit_code), "--platform", platform, "--mode", mode,
                "--out", os.path.join(self.tmp.name, "j.json"), "--allowlist", self.allow]
        if baseline:
            args += ["--baseline", baseline]
        return run(*args)

    def test_counts_not_found_on_exit_zero_is_a_fail(self):
        rc, out = self.judge("Running: a...\n  PASS\n")
        self.assertNotEqual(rc, 0)
        self.assertTrue(out.startswith("FAIL: counts not found"), out)

    def test_empty_log_on_exit_zero_is_a_fail(self):
        rc, out = self.judge("")
        self.assertNotEqual(rc, 0)

    def test_nonzero_exit_is_a_fail_with_counts(self):
        rc, out = self.judge(CMD_LOG, exit_code=1)
        self.assertNotEqual(rc, 0)
        self.assertIn("exit 1; 2 passed, 1 skipped, 0 failed", out)

    def test_good_run_without_baseline_says_so(self):
        rc, out = self.judge(CMD_LOG)
        self.assertEqual(rc, 0, out)
        self.assertTrue(out.startswith("PASS: 2 passed, 1 skipped, 0 failed"), out)
        self.assertIn("no baseline manifest", out)

    def test_unallowed_skip_is_a_fail(self):
        rc, out = self.judge(CMD_LOG.replace("gamma", "stranger"))
        self.assertNotEqual(rc, 0)
        self.assertIn("stranger was skipped", out)

    def test_baseline_is_enforced_when_it_exists(self):
        _, _, base = self.collect(CMD_LOG, name="base.json")
        rc, out = self.judge(CMD_LOG, baseline=base)
        self.assertEqual(rc, 0, out)
        self.assertIn("agrees with the baseline", out)
        rc, out = self.judge(CMD_LOG.replace(BETA_CMD, "").replace("2 passed", "1 passed"), baseline=base)
        self.assertNotEqual(rc, 0)
        self.assertIn("beta is in the baseline and missing", out)

    def test_absent_baseline_file_is_not_an_error(self):
        rc, out = self.judge(CMD_LOG, baseline=os.path.join(self.tmp.name, "no_such_baseline.json"))
        self.assertEqual(rc, 0, out)

    def test_both_verify_scripts_use_judge_and_neither_passes_without_counts(self):
        for name in ("verify_platform.sh", "verify_platform.cmd"):
            with open(os.path.join(HERE, name), encoding="utf-8") as f:
                text = f.read()
            self.assertIn("gate_manifest.py", text, name)
            self.assertIn("judge", text, name)
            self.assertNotIn("counts not found}", text, name + " still falls back to PASS (counts not found)")
            self.assertIn("manifest=", text, name + " does not record the manifest path")


class Check(Base):
    def test_unlisted_skip_and_fail(self):
        rc, out = run("check", self.manifest({"a": "PASS", "zzz": "SKIP", "q": "FAIL"}, "c.json"), "--allowlist", self.allow)
        self.assertNotEqual(rc, 0)
        self.assertIn("zzz was skipped", out)
        self.assertIn("q FAILED", out)

    def test_allowed_skip_passes(self):
        rc, out = run("check", self.manifest({"a": "PASS", "gamma": "SKIP"}, "c.json"), "--allowlist", self.allow)
        self.assertEqual(rc, 0, out)

    def test_empty_mode_fails(self):
        rc, out = run("check", self.manifest({}, "c.json"), "--allowlist", self.allow)
        self.assertNotEqual(rc, 0)


class Lint(Base):
    E = {"test": "gamma", "class": "skip_allowed", "platforms": ["*"], "modes": ["*"], "reason": "r"}

    def lint(self, files, entries):
        d = os.path.join(self.tmp.name, "tests")
        os.makedirs(d, exist_ok=True)
        for n, text in files.items():
            with open(os.path.join(d, n), "w") as f:
                f.write(text)
        allow = self.write("a2.json", json.dumps({"format": 1, "entries": entries}))
        return run("lint", "--tests-dir", d, "--allowlist", allow)

    def test_agreeing_passes(self):
        rc, out = self.lint({"gamma.obs": "# SKIP_ALLOWED: r\nx\n"}, [self.E])
        self.assertEqual(rc, 0, out)

    def test_declaration_without_entry_fails(self):
        rc, out = self.lint({"gamma.obs": "# SKIP_ALLOWED: r\nx\n"}, [])
        self.assertNotEqual(rc, 0)
        self.assertIn("gamma declares", out)

    def test_entry_without_declaration_fails(self):
        rc, out = self.lint({"gamma.obs": "x\n"}, [self.E])
        self.assertNotEqual(rc, 0)
        self.assertIn("does not carry that declaration", out)

    def test_marker_without_entry_fails(self):
        rc, out = self.lint({"gamma.obs": "# GC_STRESS_SKIP\n# reason: r\n"}, [])
        self.assertNotEqual(rc, 0)
        self.assertIn("GC_STRESS_SKIP", out)

    def test_entry_without_reason_fails(self):
        rc, out = self.lint({"gamma.obs": "# SKIP_ALLOWED: r\n"}, [dict(self.E, reason="")])
        self.assertNotEqual(rc, 0)
        self.assertIn("no reason", out)

    def test_repository_allowlist_is_current(self):
        rc, out = run("lint")
        self.assertEqual(rc, 0, out)


if __name__ == "__main__":
    unittest.main()
