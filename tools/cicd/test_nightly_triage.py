#!/usr/bin/env python3
"""Tests for nightly_triage.py (standard library only).

Run:  python3 -m unittest tools/cicd/test_nightly_triage.py
"""
import io
import json
import os
import shutil
import sys
import tempfile
import unittest
from contextlib import redirect_stdout

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import nightly_triage as nt  # noqa: E402

PY = sys.executable

SH_LOG = """\
Running: alpha...
  [PASS] (0s)
Running: beta_gc...
  [FAIL] runtime error (exit 1)
  --- output ---
  FAIL: tree checksum 12 != 13
  more output
  --------------
Running: gamma...
  [PASS] (1s)

========================================
  Results: 2 passed, 0 skipped, 1 failed
========================================

Failed tests:
  ✗ beta_gc — runtime error (exit 1)
"""

CMD_LOG = """\
Running: alpha...
  PASS
Running: delta...
  FAIL (runtime error)
  --- output ---
Invalid object cast: 'Foo'
  --------------
Running: eps...
  FAIL (compilation error)

Failed tests:
  x delta - runtime error (exit 1)
"""


def quiet(fn, *a, **k):
    with redirect_stdout(io.StringIO()):
        return fn(*a, **k)


def write_json(path, data):
    os.makedirs(os.path.dirname(path), exist_ok=True)
    with open(path, "w", encoding="utf-8") as f:
        json.dump(data, f)


def result(leg, step, status="pass", failures=None, config="", exit_code=0):
    return {"schema": 1, "leg": leg, "step": step, "config": config,
            "status": status, "exit_code": exit_code, "duration_s": 1.0,
            "log": step + ".log", "failures": failures or []}


class ParserTests(unittest.TestCase):
    def test_posix_regression_log(self):
        fs = nt.parse_regression_log(SH_LOG, "s3/default")
        self.assertEqual(len(fs), 1)
        self.assertEqual(fs[0]["test"], "beta_gc")
        self.assertEqual(fs[0]["message"], "runtime error (exit 1)")
        self.assertEqual(fs[0]["config"], "s3/default")
        # The test's own FAIL line is output, not a second failure.
        self.assertIn("tree checksum", fs[0]["detail"])

    def test_windows_regression_log(self):
        fs = nt.parse_regression_log(CMD_LOG)
        self.assertEqual([f["test"] for f in fs], ["delta", "eps"])
        self.assertEqual(fs[0]["message"], "runtime error")
        self.assertIn("Invalid object cast", fs[0]["detail"])
        self.assertEqual(fs[1]["message"], "compilation error")

    def test_generic_log(self):
        log = ("checking 40 tests\n"
               "FAILED: 0\n"
               "reseeded the pool\n"
               "MISMATCH tco_receiver s3/jit1: stdout differs from s0/off\n"
               "  line 3: 42 vs 41\n"
               "seed: 20260915-17\n"
               "reduced: fuzz-out/r17.obs\n"
               "[CRASH] prog_9: obr exited 139\n")
        fs = nt.parse_generic_log(log, "diff", default_seed="20260915")
        self.assertEqual(len(fs), 2)
        self.assertEqual(fs[0]["test"], "tco_receiver")
        self.assertEqual(fs[0]["config"], "s3/jit1")
        self.assertEqual(fs[0]["seed"], "20260915-17")
        self.assertEqual(fs[0]["reduced"], "fuzz-out/r17.obs")
        self.assertIn("42 vs 41", fs[0]["detail"])
        self.assertEqual(fs[1]["test"], "prog_9")
        self.assertEqual(fs[1]["config"], "diff")
        self.assertEqual(fs[1]["seed"], "20260915")


class SignatureTests(unittest.TestCase):
    def test_stable_across_addresses_and_counts(self):
        a = nt.signature("linux-arm64", "stress", "jit=1", "t", "bad ptr 0x7ff01 at run 3")
        b = nt.signature("linux-arm64", "stress", "jit=1", "t", "bad ptr 0x1a2b at run 17")
        self.assertEqual(a, b)

    def test_leg_and_config_distinguish(self):
        a = nt.signature("linux-arm64", "stress", "jit=1", "t", "m")
        self.assertNotEqual(a, nt.signature("linux-x64", "stress", "jit=1", "t", "m"))
        self.assertNotEqual(a, nt.signature("linux-arm64", "stress", "jit=off", "t", "m"))


class ClassifyTests(unittest.TestCase):
    def setUp(self):
        self.dir = tempfile.mkdtemp()
        self.results = os.path.join(self.dir, "results")
        self.known = os.path.join(self.dir, "known.json")
        write_json(self.known, {"signatures": [
            {"id": "k816", "leg": "arm64$", "config": "jit=1",
             "message": "Invalid object cast", "issue": "#816"}]})

    def tearDown(self):
        shutil.rmtree(self.dir, ignore_errors=True)

    def put(self, r):
        write_json(os.path.join(self.results, r["leg"], r["step"] + ".json"), r)

    def test_three_classes_and_missing_step(self):
        self.put(result("linux-x64", "stress"))
        self.put(result("linux-arm64", "stress", "fail", [nt.make_failure(
            "map_insert", "exit code 1", "jit=1", detail="Invalid object cast: '>>>'")]))
        self.put(result("macos-arm64", "stress", "fail", [nt.make_failure(
            "(step)", "exit code 1", "jit=1", detail="E: Failed to fetch http://x")]))
        self.put(result("windows-x64", "stress", "fail", [nt.make_failure(
            "jit_gc_stress", "exit code 3", "jit=off")]))
        rep = nt.classify(self.results, self.known,
                          ["linux-x64", "linux-arm64", "macos-arm64", "windows-x64",
                           "windows-arm64"], ["stress"])
        classes = {(i["leg"], i["class"]) for i in rep["items"]}
        self.assertIn(("linux-arm64", "known"), classes)
        self.assertIn(("macos-arm64", "infra"), classes)
        self.assertIn(("windows-x64", "new"), classes)
        self.assertIn(("windows-arm64", "infra"), classes)  # no result file
        self.assertNotIn("linux-x64", {i["leg"] for i in rep["items"]})
        self.assertEqual(rep["status"], "new")
        known = [i for i in rep["items"] if i["class"] == "known"][0]
        self.assertEqual(known["known"]["issue"], "#816")

    def test_known_needs_every_field(self):
        # Right message, wrong leg: still new.
        self.put(result("linux-x64", "stress", "fail", [nt.make_failure(
            "map_insert", "Invalid object cast", "jit=1")]))
        rep = nt.classify(self.results, self.known, ["linux-x64"], ["stress"])
        self.assertEqual(rep["items"][0]["class"], "new")

    def test_known_by_exact_id(self):
        f = nt.make_failure("t", "boom", "jit=off")
        self.put(result("linux-x64", "fuzz", "fail", [f]))
        sig = nt.signature("linux-x64", "fuzz", "jit=off", "t", "boom")
        write_json(self.known, [{"id": sig}])
        rep = nt.classify(self.results, self.known, ["linux-x64"], ["fuzz"])
        self.assertEqual(rep["status"], "known")

    def test_green(self):
        self.put(result("linux-x64", "fuzz"))
        rep = nt.classify(self.results, self.known, ["linux-x64"], ["fuzz"])
        self.assertEqual((rep["status"], rep["items"]), ("green", []))

    def test_infra_only(self):
        rep = nt.classify(self.results, self.known, ["linux-x64"], ["fuzz"])
        self.assertEqual(rep["status"], "infra")

    def test_fail_status_without_failures_is_new(self):
        self.put(result("linux-x64", "fuzz", "fail", [], exit_code=2))
        rep = nt.classify(self.results, self.known, ["linux-x64"], ["fuzz"])
        self.assertEqual(rep["status"], "new")
        self.assertEqual(rep["items"][0]["message"], "exit code 2")

    def test_missing_known_file_is_new_not_green(self):
        self.put(result("linux-x64", "fuzz"))
        rep = nt.classify(self.results, os.path.join(self.dir, "nope.json"),
                          ["linux-x64"], ["fuzz"])
        self.assertEqual(rep["status"], "new")
        self.assertIn("known-signature file missing", rep["items"][0]["message"])

    def test_bad_regex_is_new(self):
        write_json(self.known, [{"message": "("}])
        self.put(result("linux-x64", "fuzz"))
        rep = nt.classify(self.results, self.known, ["linux-x64"], ["fuzz"])
        self.assertEqual(rep["status"], "new")
        self.assertIn("bad message regex", rep["items"][0]["message"])


class IssueBodyTests(unittest.TestCase):
    def item(self, **kw):
        base = {"id": "abc1234567", "leg": "linux-arm64", "step": "fuzz",
                "config": "s3/jit1", "test": "prog_17", "message": "digest 4 differs",
                "detail": "```\nf4: 1 vs 2\n", "seed": "20260915-17",
                "reduced": "fuzz-out/r17.obs", "count": 2, "log": "linux-arm64/fuzz.log",
                "class": "new"}
        base.update(kw)
        return base

    def test_body_has_what_a_fixer_needs(self):
        body = nt.issue_body(self.item(), "https://github.com/o/r/actions/runs/9")
        for needle in ("abc1234567", "linux-arm64", "s3/jit1", "prog_17",
                       "`20260915-17`", "`fuzz-out/r17.obs`", "`nightly-linux-arm64`",
                       "linux-arm64/fuzz.log", "actions/runs/9", "tools/fuzz/known.json",
                       "digest 4 differs"):
            self.assertIn(needle, body)
        self.assertIn("~~~\n```\nf4: 1 vs 2\n~~~", body)
        snippet = body.split("~~~json\n")[1].split("\n~~~")[0]
        entry = json.loads(snippet)
        self.assertEqual(entry["id"], "abc1234567")

    def test_body_without_seed_or_reduction(self):
        body = nt.issue_body(self.item(seed="", reduced="", detail=""))
        self.assertIn("**Seed:** none recorded", body)
        self.assertIn("**Reduced program:** none recorded", body)
        self.assertNotIn("### Output", body)

    def test_long_output_is_truncated(self):
        detail = "\n".join("line %d" % i for i in range(500))
        body = nt.issue_body(self.item(detail=detail))
        self.assertIn("more lines in the log artifact", body)
        self.assertNotIn("line 499", body)

    def test_title_is_stable_and_unique(self):
        self.assertEqual(nt.short_title(self.item()),
                         "Nightly hardening [abc1234567] linux-arm64 fuzz prog_17")


class TriageCommandTests(unittest.TestCase):
    def setUp(self):
        self.dir = tempfile.mkdtemp()
        self.results = os.path.join(self.dir, "results")
        self.out = os.path.join(self.dir, "out")
        self.known = os.path.join(self.dir, "known.json")
        write_json(self.known, [])
        self.gh = os.path.join(self.dir, "gh_output")

    def tearDown(self):
        shutil.rmtree(self.dir, ignore_errors=True)

    def triage(self, legs, steps, max_issues=5):
        return quiet(nt.main, ["triage", "--results", self.results, "--known", self.known,
                               "--legs", legs, "--steps", steps, "--out", self.out,
                               "--run-url", "https://x/runs/1", "--github-output", self.gh,
                               "--max-issues", str(max_issues)])

    def outputs(self):
        with open(self.gh, encoding="utf-8") as f:
            return dict(line.rstrip("\n").split("=", 1) for line in f)

    def test_new_failures_write_issue_files_and_outputs(self):
        fails = [nt.make_failure("t%d" % i, "exit code 1", "jit=1") for i in range(3)]
        write_json(os.path.join(self.results, "linux-x64", "stress.json"),
                   result("linux-x64", "stress", "fail", fails))
        self.assertEqual(self.triage("linux-x64", "stress", max_issues=2), 0)
        out = self.outputs()
        self.assertEqual(out["status"], "new")
        self.assertEqual(out["new_count"], "3")
        issues = json.loads(out["new_issues"])
        self.assertEqual(len(issues), 2)
        for issue in issues:
            self.assertTrue(issue["workflow"].startswith("Nightly hardening ["))
            self.assertTrue(os.path.exists(os.path.join(self.out, issue["body_file"])))
        with open(os.path.join(self.out, "tracking.md"), encoding="utf-8") as f:
            self.assertIn("1 more new signatures", f.read())

    def test_green_outputs(self):
        write_json(os.path.join(self.results, "linux-x64", "stress.json"),
                   result("linux-x64", "stress"))
        self.triage("linux-x64", "stress")
        out = self.outputs()
        self.assertEqual((out["status"], out["new_count"], out["new_issues"]),
                         ("green", "0", "[]"))


class RunCommandTests(unittest.TestCase):
    def setUp(self):
        self.dir = tempfile.mkdtemp()

    def tearDown(self):
        shutil.rmtree(self.dir, ignore_errors=True)

    def run_step(self, extra, command, step="s"):
        rc = quiet(nt.main, ["run", "--leg", "leg", "--step", step, "--out", self.dir] +
                   extra + ["--"] + command)
        with open(os.path.join(self.dir, "leg", step + ".json"), encoding="utf-8") as f:
            return rc, json.load(f)

    def test_missing_tool_fails_not_skips(self):
        rc, r = self.run_step(["--require", os.path.join(self.dir, "no_such_tool.py")],
                              [PY, "-c", "pass"])
        self.assertEqual(rc, 1)
        self.assertEqual(r["status"], "fail")
        self.assertIn("required tool missing", r["failures"][0]["message"])

    def test_missing_knob_in_binary_fails(self):
        fake = os.path.join(self.dir, "obr.bin")
        with open(fake, "wb") as f:
            f.write(b"\x00OBJECK_JIT_THRESHOLD\x00")
        rc, r = self.run_step(["--require-text", fake + ":OBJECK_GC_VERIFY"],
                              [PY, "-c", "pass"])
        self.assertEqual((rc, r["status"]), (1, "fail"))
        self.assertIn("does not contain OBJECK_GC_VERIFY", r["failures"][0]["message"])
        rc, r = self.run_step(["--require-text", fake + ":OBJECK_JIT_THRESHOLD"],
                              [PY, "-c", "pass"])
        self.assertEqual((rc, r["status"]), (0, "pass"))

    def test_clean_pass(self):
        rc, r = self.run_step([], [PY, "-c", "print('all good')"])
        self.assertEqual((rc, r["status"], r["failures"]), (0, "pass", []))
        with open(os.path.join(self.dir, "leg", "s.log"), encoding="utf-8") as f:
            self.assertIn("all good", f.read())

    def test_printed_fail_with_exit_zero_still_fails(self):
        rc, r = self.run_step([], [PY, "-c", "print('FAIL t1 s3/off: x')"])
        self.assertEqual((rc, r["status"]), (1, "fail"))
        self.assertEqual(r["failures"][0]["test"], "t1")

    def test_nonzero_exit_without_lines(self):
        rc, r = self.run_step(["--default-seed", "99"],
                              [PY, "-c", "import sys; print('boom'); sys.exit(3)"])
        self.assertEqual(rc, 1)
        f = r["failures"][0]
        self.assertEqual((f["message"], f["seed"]), ("exit code 3", "99"))
        self.assertIn("boom", f["detail"])

    def test_timeout_is_a_failure(self):
        rc, r = self.run_step(["--timeout", "1"], [PY, "-c", "import time; time.sleep(30)"])
        self.assertEqual(rc, 1)
        self.assertIn("timed out", r["failures"][0]["message"])

    def test_regression_parser_through_run(self):
        script = os.path.join(self.dir, "fake_runner.py")
        with open(script, "w", encoding="utf-8") as f:
            f.write("import sys\nsys.stdout.write(%r)\nsys.exit(1)\n" % CMD_LOG)
        rc, r = self.run_step(["--parser", "regression", "--config", "verify=1"],
                              [script], step="verify-major")
        self.assertEqual(rc, 1)
        self.assertEqual([f["test"] for f in r["failures"]], ["delta", "eps"])
        self.assertEqual(r["failures"][0]["config"], "verify=1")


class LoopTests(unittest.TestCase):
    def test_aggregates_failures_per_test_and_mode(self):
        calls = {"n": 0}

        def compile_fn(test):
            if test == "broken":
                return False, "error: bad", None
            return True, "", test + ".obe"

        def run_fn(program, flags):
            calls["n"] += 1
            if program == "flaky.obe" and flags == ["--jit=1"] and calls["n"] % 2 == 0:
                return 1, "FAIL: corruption"
            if program == "hangs.obe" and flags == ["--jit=off"]:
                return None, "partial"
            return 0, "PASS"

        fs = nt.run_loop(["ok", "flaky", "hangs", "broken"], 4, ["off", "1"],
                         compile_fn, run_fn, log=lambda m: None)
        got = {(f["test"], f["config"], f["message"]): f["count"] for f in fs}
        self.assertEqual(got[("flaky", "jit=1", "exit code 1")], 2)
        self.assertEqual(got[("hangs", "jit=off", "timed out")], 4)
        self.assertEqual(got[("broken", "compile", "compilation failed")], 1)
        self.assertNotIn("ok", {f["test"] for f in fs})
        flaky = [f for f in fs if f["test"] == "flaky"][0]
        self.assertIn("failed 2 of 4 runs", flaky["detail"])
        self.assertIn("corruption", flaky["detail"])

    def test_loop_missing_binaries_fails(self):
        d = tempfile.mkdtemp()
        try:
            rc = quiet(nt.main, ["loop", "--leg", "leg", "--out", d, "--bin",
                                 os.path.join(d, "bin"), "--tests", "minor_gc_stress",
                                 "--runs", "1"])
            self.assertEqual(rc, 1)
            with open(os.path.join(d, "leg", "stress.json"), encoding="utf-8") as f:
                r = json.load(f)
            self.assertIn("required tool missing", r["failures"][0]["message"])
        finally:
            shutil.rmtree(d, ignore_errors=True)


if __name__ == "__main__":
    unittest.main()
