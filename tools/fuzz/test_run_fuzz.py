"""run_fuzz.py against a fake toolchain (faults/fake_obc.py, faults/fake_obr.py):
no Objeck build needed, so these run anywhere the tools do.

  * the JIT-coverage gate fails a VM that ignores --jit=1 (no report lines)
    and passes one that reports every method compiled;
  * round trip with tools/cicd/nightly_triage.py: a finding run_fuzz records
    under the nightly wrapper is classified 'new' without a known.json entry
    and 'known' with one, and run_fuzz honours the very same file.
"""

import io
import json
import os
import re
import shutil
import sys
import tempfile
import unittest
from contextlib import redirect_stdout

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
sys.path.insert(0, os.path.join(os.path.dirname(HERE), "cicd"))
import known_schema    # noqa: E402
import nightly_triage  # noqa: E402
import run_fuzz        # noqa: E402

FAKE_OBC = os.path.join(HERE, "faults", "fake_obc.py")
FAKE_OBR = os.path.join(HERE, "faults", "fake_obr.py")
SEEDS = (500, 3)   # first seed, count


class FakeToolchainCase(unittest.TestCase):
    def setUp(self):
        self.dir = tempfile.mkdtemp(prefix="fuzz_fake_")
        self.bin = os.path.join(self.dir, "bin")
        os.makedirs(self.bin)
        self.saved_mode = os.environ.get("FAKE_OBR_MODE")

    def tearDown(self):
        if self.saved_mode is None:
            os.environ.pop("FAKE_OBR_MODE", None)
        else:
            os.environ["FAKE_OBR_MODE"] = self.saved_mode
        shutil.rmtree(self.dir, ignore_errors=True)

    def known(self, entries, name="known.json"):
        path = os.path.join(self.dir, name)
        with open(path, "w", encoding="utf-8") as f:
            json.dump({"schema": 1, "known": entries}, f)
        return path

    def argv(self, known, extra=()):
        return ["--bin", self.bin, "--count", str(SEEDS[1]), "--seed", str(SEEDS[0]), "-j", "2",
                "--features", "F2", "--obc", FAKE_OBC, "--obr", FAKE_OBR, "--known", known,
                "--out", os.path.join(self.dir, "out"), "--json", os.path.join(self.dir, "summary.json")] \
            + list(extra)

    def fuzz(self, mode, known, extra=()):
        os.environ["FAKE_OBR_MODE"] = mode
        buf = io.StringIO()
        with redirect_stdout(buf):
            status = run_fuzz.main(self.argv(known, extra))
        with open(os.path.join(self.dir, "summary.json"), encoding="utf-8") as f:
            return status, json.load(f), buf.getvalue()


class CoverageGateTest(FakeToolchainCase):
    def test_vm_that_ignores_jit1_fails_the_gate(self):
        status, s, out = self.fuzz("ignore-jit", self.known([]), ["--min-jit", "0.5"])
        self.assertEqual(s["stats"]["new"], 0, s["signatures"])   # outputs all agree
        self.assertGreater(s["jit_total"], 0)
        self.assertEqual(s["jit_compiled"], 0)
        self.assertIn("FAIL: JIT-compiled fraction 0.0%", out)
        self.assertEqual(status, 1)

    def test_vm_that_reports_compiles_passes_the_gate(self):
        status, s, out = self.fuzz("honest", self.known([]), ["--min-jit", "0.9"])
        self.assertEqual(s["jit_compiled"], s["jit_total"])
        self.assertNotIn("FAIL", out)
        self.assertEqual(status, 0)

    def test_off_schema_known_file_is_a_usage_error(self):
        path = os.path.join(self.dir, "old.json")
        with open(path, "w") as f:
            json.dump([{"signature": "x"}], f)
        with redirect_stdout(io.StringIO()):
            self.assertEqual(run_fuzz.main(self.argv(path)), 2)


class ReplayAndSampleTest(FakeToolchainCase):
    """A replay or a small sample is not judged on the coverage fraction, but
    still fails when nothing compiled or a finding is new."""

    def replay(self, mode, known, seed=SEEDS[0], extra=()):
        os.environ["FAKE_OBR_MODE"] = mode
        argv = ["--bin", self.bin, "--replay", str(seed), "-j", "1", "--features", "F2",
                "--obc", FAKE_OBC, "--obr", FAKE_OBR, "--known", known,
                "--out", os.path.join(self.dir, "out"),
                "--json", os.path.join(self.dir, "summary.json")] + list(extra)
        buf = io.StringIO()
        with redirect_stdout(buf):
            status = run_fuzz.main(argv)
        summary = None
        if os.path.exists(os.path.join(self.dir, "summary.json")):
            with open(os.path.join(self.dir, "summary.json"), encoding="utf-8") as f:
                summary = json.load(f)
        return status, summary, buf.getvalue()

    def test_small_sample_does_not_apply_the_floor(self):
        status, s, out = self.fuzz("partial", self.known([]), ["--min-jit", "0.9"])
        self.assertLess(s["jit_total"], 20)
        self.assertGreater(s["jit_compiled"], 0)
        self.assertLess(s["jit_compiled"], s["jit_total"])
        self.assertNotIn("FAIL", out)
        self.assertIn("not applied", out)
        self.assertEqual(status, 0)

    def test_large_enough_sample_applies_the_floor(self):
        status, s, out = self.fuzz("partial", self.known([]), ["--min-jit", "0.9", "--min-jit-sample", "10"])
        self.assertGreaterEqual(s["jit_total"], 10)
        self.assertIn("FAIL: JIT-compiled fraction", out)
        self.assertEqual(status, 1)

    def test_replay_runs_one_seed_without_the_floor(self):
        status, s, out = self.replay("partial", self.known([]), extra=["--min-jit-sample", "0"])
        self.assertEqual(s["stats"]["programs"], 1)
        self.assertLess(s["jit_fraction"], 0.70)
        self.assertIn("not applied (replay)", out)
        self.assertEqual(status, 0)

    def test_replay_still_fails_a_vm_that_compiles_nothing(self):
        status, s, out = self.replay("ignore-jit", self.known([]))
        self.assertEqual(s["jit_compiled"], 0)
        self.assertIn("FAIL: JIT-compiled fraction 0.0%", out)
        self.assertEqual(status, 1)

    def test_replay_still_reports_a_new_finding(self):
        status, s, out = self.replay("diverge", self.known([]))
        self.assertEqual((s["stats"]["programs"], s["stats"]["new"]), (1, 1))
        self.assertIn("FAIL fuzz: ", out)
        self.assertEqual(status, 1)

    def test_replay_excludes_seed_and_count(self):
        known = self.known([])
        for extra in (["--seed", "7"], ["--count", "3"]):
            status, _s, _out = self.replay("honest", known, extra=extra)
            self.assertEqual(status, 2, extra)


class TriageRoundTripTest(FakeToolchainCase):
    LEG = "linux-x64"

    def record(self, known):
        """Run run_fuzz under nightly_triage.py run exactly as the workflow does."""
        os.environ["FAKE_OBR_MODE"] = "diverge"
        results = os.path.join(self.dir, "results")
        cmd = ["run", "--leg", self.LEG, "--step", "fuzz", "--out", results, "--config", "fuzz",
               "--timeout", "600", "--parser", "generic", "--default-seed", str(SEEDS[0]),
               "--require", run_fuzz.__file__, "--",
               sys.executable, run_fuzz.__file__] + self.argv(known, ["--leg", self.LEG])
        with redirect_stdout(io.StringIO()):
            code = nightly_triage.main(cmd)
        return code, results

    def classify(self, results, known):
        return nightly_triage.classify(results, known, [self.LEG], ["fuzz"])

    def test_recorded_finding_is_new_then_known(self):
        empty = self.known([], "empty.json")
        code, results = self.record(empty)
        self.assertEqual(code, 1)
        with open(os.path.join(self.dir, "summary.json"), encoding="utf-8") as f:
            sigs = list(json.load(f)["signatures"])
        self.assertEqual(len(sigs), 1, sigs)
        sig = sigs[0]

        report = self.classify(results, empty)
        self.assertEqual(report["status"], "new")
        fuzz_items = [i for i in report["items"] if i["test"] == "fuzz"]
        self.assertTrue(fuzz_items)

        # one entry, written once, in the documented schema
        known = self.known([{"signature": "^%s$" % re.escape(sig), "note": "injected by fake_obr"}])
        report = self.classify(results, known)
        self.assertEqual(report["status"], "known", report["items"])
        self.assertEqual({i["class"] for i in report["items"]}, {"known"})

        # and the fuzzer reads the same file the same way
        status, s, _ = self.fuzz("diverge", known, ["--leg", self.LEG])
        self.assertEqual((status, s["stats"]["new"], s["stats"]["known"]), (0, 0, SEEDS[1]))

    def test_issue_snippet_is_a_valid_entry_that_matches(self):
        _code, results = self.record(self.known([], "empty.json"))
        report = self.classify(results, self.known([], "empty.json"))
        item = [i for i in report["items"] if i["test"] == "fuzz"][0]
        body = nightly_triage.issue_body(item)
        snippet = json.loads(body.split("~~~json\n")[1].split("\n~~~")[0])
        path = self.known([snippet])
        self.assertIsNotNone(known_schema.match(known_schema.load(path),
                                                dict(item, signature=known_schema.fuzz_signature(
                                                    item["test"], item["message"]))))
        self.assertEqual(self.classify(results, path)["status"], "known")

    def test_leg_narrowed_entry_applies_to_its_leg_only(self):
        _code, results = self.record(self.known([], "empty.json"))
        with open(os.path.join(self.dir, "summary.json"), encoding="utf-8") as f:
            sig = list(json.load(f)["signatures"])[0]
        other = self.known([{"signature": "^%s$" % re.escape(sig), "leg": "^macos-arm64$", "note": "n"}])
        self.assertEqual(self.classify(results, other)["status"], "new")
        status, s, _ = self.fuzz("diverge", other, ["--leg", self.LEG])
        self.assertEqual((status, s["stats"]["known"]), (1, 0))


if __name__ == "__main__":
    unittest.main()
