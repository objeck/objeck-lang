"""Unit tests for tools/cicd/stress_probe.py (no Objeck binaries needed).

    python -m unittest tools/cicd/test_stress_probe.py
"""

import contextlib
import io
import os
import shutil
import sys
import tempfile
import unittest

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))

import stress_probe as sp  # noqa: E402


class BoundMath(unittest.TestCase):
    def test_rule_of_three(self):
        self.assertAlmostEqual(sp.rule_of_three(200), 0.015)
        self.assertAlmostEqual(sp.rule_of_three(100), 0.03)
        self.assertAlmostEqual(sp.rule_of_three(3000), 0.001)

    def test_small_and_empty(self):
        self.assertEqual(sp.rule_of_three(2), 1.0)   # capped at certainty
        self.assertEqual(sp.rule_of_three(3), 1.0)
        self.assertIsNone(sp.rule_of_three(0))

    def test_format_rate(self):
        self.assertEqual(sp.format_rate(0.015), "1.5%")
        self.assertEqual(sp.format_rate(0.00375), "0.38%")
        self.assertEqual(sp.format_rate(0.6), "60%")
        self.assertEqual(sp.format_rate(None), "n/a")


class Counting(unittest.TestCase):
    def _run(self, codes, jobs=1, planned=None, remaining=None):
        cell = sp.Cell("fx", "default", planned or len(codes))

        def run_fn(i):
            return codes[i - 1], 0.01, b"out%d" % i, b"err%d" % i, None

        return sp.run_cell(cell, run_fn, jobs, remaining)

    def test_counts_failures_and_timeouts(self):
        cell = self._run([0, 1, 0, None, 3, 0])
        self.assertEqual(cell.runs, 6)
        self.assertEqual(cell.failures, 3)
        self.assertEqual(cell.timeouts, 1)
        self.assertEqual(cell.exit_codes, {"exit code 1": 1, "timed out": 1, "exit code 3": 1})

    def test_all_pass(self):
        cell = self._run([0] * 50, jobs=4)
        self.assertEqual((cell.runs, cell.failures), (50, 0))
        self.assertAlmostEqual(cell.as_dict()["upper_bound"], 0.06)

    def test_parallel_counts_every_run_once(self):
        codes = [0 if i % 7 else 139 for i in range(1, 201)]
        cell = self._run(codes, jobs=8)
        self.assertEqual(cell.runs, 200)
        self.assertEqual(cell.failures, codes.count(139))
        self.assertIsNone(cell.as_dict()["upper_bound"])

    def test_budget_stops_early(self):
        left = [3]

        def remaining():
            left[0] -= 1
            return left[0]

        cell = self._run([0] * 10, remaining=remaining)
        self.assertEqual(cell.runs, 2)
        self.assertEqual(cell.planned, 10)


class FailureCapture(unittest.TestCase):
    def test_keeps_first_three(self):
        saved = []

        def save(cell, run, message, out, err):
            saved.append((run, message, err))
            return {"stderr_file": "f%d" % run}

        cell = sp.Cell("fx", "jit1", 10)
        for i, code in enumerate([1, 0, 2, None, 5, 6], start=1):
            cell.record(i, code, 0.1, b"o", b"e%d" % i, save)
        self.assertEqual(cell.failures, 5)
        self.assertEqual([c["run"] for c in cell.captured], [1, 3, 4])
        self.assertEqual(saved, [(1, "exit code 1", b"e1"), (3, "exit code 2", b"e3"),
                                 (4, "timed out", b"e4")])
        self.assertEqual(cell.captured[0]["stderr_file"], "f1")

    def test_clip(self):
        big = b"x" * (sp.OUTPUT_LIMIT + 10)
        text = sp.clip(big)
        self.assertTrue(text.startswith("[... 10 chars clipped ...]"))
        self.assertEqual(sp.clip(None), "")


class Summary(unittest.TestCase):
    def _cells(self):
        a = sp.Cell("minor_gc_stress", "default", 200)
        for i in range(200):
            a.record(i + 1, 0, 0.5, b"", b"", None)
        b = sp.Cell("minor_gc_stress", "verify256k", 200)
        for i in range(100):
            b.record(i + 1, 1 if i in (4, 9) else 0, 1.0, b"", b"", None)
        return [a, b]

    def test_table_rows_and_bounds(self):
        text = sp.build_summary({"leg": "linux-x64"}, [], self._cells(), [])
        self.assertIn("### Stress probe: linux-x64", text)
        self.assertIn("result: **FAIL** (2 failure(s) in 300 runs)", text)
        self.assertIn("| minor_gc_stress | default | 200 | 0 | 1.5% | 0.50 | 0.50 |  |", text)
        self.assertIn("| minor_gc_stress | verify256k | 100 | **2** | observed 2.0% |", text)
        self.assertIn("stopped at 100 of 200", text)
        self.assertIn("exit code 1 x2", text)
        self.assertIn("| default | 200 | 0 | 1.5% |", text)

    def test_pass_and_problems(self):
        cells = self._cells()[:1]
        self.assertIn("**PASS**", sp.build_summary({}, [], cells, []))
        text = sp.build_summary({}, [{"fixture": "x", "ok": False}], cells,
                                ["obr does not contain OBJECK_GC_VERIFY"])
        self.assertIn("**FAIL**", text)
        self.assertIn("- obr does not contain OBJECK_GC_VERIFY", text)
        self.assertIn("- `x`", text)


class Helpers(unittest.TestCase):
    def setUp(self):
        self.tmp = tempfile.mkdtemp()

    def tearDown(self):
        shutil.rmtree(self.tmp, ignore_errors=True)

    def test_mode_env_clears_inherited_knobs(self):
        base = {"PATH": "p", "OBJECK_NURSERY": "64k", "OBJECK_GC_VERIFY": "1"}
        self.assertEqual(sp.mode_env(base, "default"), {"PATH": "p"})
        self.assertEqual(sp.mode_env(base, "jit1")["OBJECK_JIT_THRESHOLD"], "1")
        v = sp.mode_env(base, "verify256k")
        self.assertEqual((v["OBJECK_NURSERY"], v["OBJECK_GC_VERIFY"]), ("256k", "1"))
        self.assertEqual(sp.MODES["jit1"][0], ["--jit=1"])

    def test_missing_knobs(self):
        obr = os.path.join(self.tmp, "obr")
        with open(obr, "wb") as f:
            f.write(b"\x00junk OBJECK_NURSERY\x00")
        self.assertEqual(sp.missing_knobs(obr, ["default", "jit1"]), [])
        self.assertEqual(sp.missing_knobs(obr, ["nursery256k"]), [])
        self.assertEqual(sp.missing_knobs(obr, ["verify256k"]), ["OBJECK_GC_VERIFY"])

    def test_extra_libs(self):
        src = os.path.join(self.tmp, "t.obs")
        with open(src, "w", encoding="utf-8-sig") as f:
            f.write("# EXTRA_LIBS: gen_collect\nclass T {}\n")
        self.assertEqual(sp.extra_libs(src), "gen_collect")
        self.assertEqual(sp.extra_libs(os.path.join(self.tmp, "none.obs")), "")

    def test_fixture_runs(self):
        self.assertEqual(sp.parse_fixture_runs(["a=10", "b=3"]), {"a": 10, "b": 3})
        with self.assertRaises(ValueError):
            sp.parse_fixture_runs(["a"])

    def test_every_fixture_has_a_source_and_count(self):
        root = os.path.abspath(os.path.join(os.path.dirname(__file__), "..", ".."))
        self.assertEqual(len(sp.FIXTURES), 10)
        for name in sp.FIXTURES:
            self.assertTrue(os.path.exists(os.path.join(root, "programs", "regression",
                                                        name + ".obs")), name)
            self.assertIn(name, sp.RUN_COUNTS)
            for mode in sp.MODE_ORDER:
                self.assertGreater(sp.planned_runs(name, mode), 0)

    def test_planned_runs_precedence(self):
        saved = dict(sp.RUN_COUNTS)
        try:
            sp.RUN_COUNTS.clear()
            sp.RUN_COUNTS.update({"a": 50, "b": {"verify256k": 20}})
            self.assertEqual(sp.planned_runs("a", "jit1"), 50)
            self.assertEqual(sp.planned_runs("b", "verify256k"), 20)
            self.assertEqual(sp.planned_runs("b", "default"), sp.DEFAULT_RUNS)
            self.assertEqual(sp.planned_runs("zz", "default"), sp.DEFAULT_RUNS)
            self.assertEqual(sp.planned_runs("b", "verify256k", 0, {"b": 7}), 7)
            self.assertEqual(sp.planned_runs("b", "verify256k", 5, {"b": 7}), 5)
        finally:
            sp.RUN_COUNTS.clear()
            sp.RUN_COUNTS.update(saved)

    def test_missing_tree_fails_cleanly(self):
        out = os.path.join(self.tmp, "out")
        with contextlib.redirect_stdout(io.StringIO()):
            code = sp.main(["run", "--bin", os.path.join(self.tmp, "nobin"), "--out", out,
                            "--runs", "1"])
        self.assertEqual(code, 1)
        self.assertTrue(os.path.exists(os.path.join(out, "results.json")))
        with open(os.path.join(out, "summary.md"), encoding="utf-8") as f:
            self.assertIn("missing tool", f.read())


if __name__ == "__main__":
    unittest.main()
