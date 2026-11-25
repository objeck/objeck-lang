#!/usr/bin/env python3
"""Tests for bench/scaling.py: the speedup/efficiency arithmetic, the multi-node
spec generator and report, and a whole `run` + `report` cycle against a fake obr.
No Objeck, no Slurm. Run:  python3 tools/hpc/alderaan/tests/test_scaling.py
"""
import contextlib
import csv
import io
import json
import os
import shutil
import stat
import sys
import tempfile
import unittest

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, os.path.join(os.path.dirname(HERE), "bench"))
sys.path.insert(0, os.path.dirname(HERE))
import scaling  # noqa: E402
import objeck_slurm as osl  # noqa: E402


def call(argv):
    out, err = io.StringIO(), io.StringIO()
    with contextlib.redirect_stdout(out), contextlib.redirect_stderr(err):
        rc = scaling.main(argv)
    return rc, out.getvalue(), err.getvalue()


def row(suite, workload, mode, cores, wall, rep=0, checksum="", throughput="", usable=None):
    return {"suite": suite, "workload": workload, "mode": mode, "cores": str(cores), "wall_s": str(wall),
            "rep": str(rep), "checksum": checksum, "throughput": str(throughput),
            "usable_cpus": str(cores if usable is None else usable)}


class ArithmeticTests(unittest.TestCase):
    def test_strong_scaling_speedup_and_efficiency(self):
        rows = [row("s", "w", "strong", k, t, rep=r) for k, t in ((1, 8.0), (2, 4.2), (4, 2.2)) for r in range(3)]
        t = scaling.summarize_runs(rows)[("s", "w", "strong")]
        self.assertAlmostEqual(t[1]["speedup"], 8.0 / 4.2)
        self.assertAlmostEqual(t[1]["efficiency"], 8.0 / 4.2 / 2)
        self.assertAlmostEqual(t[2]["speedup"], 8.0 / 2.2)
        self.assertAlmostEqual(t[2]["efficiency"], 8.0 / 2.2 / 4)

    def test_strong_scaling_baseline_need_not_be_one_core(self):
        rows = [row("s", "w", "strong", k, t) for k, t in ((2, 4.0), (4, 2.5), (8, 1.4))]
        t = scaling.summarize_runs(rows)[("s", "w", "strong")]
        self.assertAlmostEqual(t[1]["speedup"], 4.0 / 2.5)
        self.assertAlmostEqual(t[1]["efficiency"], (4.0 / 2.5) / 2)   # twice the base cores

    def test_weak_scaling_efficiency_is_t1_over_tk(self):
        rows = [row("s", "w", "weak", k, t) for k, t in ((1, 1.0), (2, 1.1), (4, 1.25))]
        t = scaling.summarize_runs(rows)[("s", "w", "weak")]
        self.assertAlmostEqual(t[1]["efficiency"], 1.0 / 1.1)
        self.assertAlmostEqual(t[2]["efficiency"], 0.8)
        self.assertAlmostEqual(t[2]["speedup"], 4 * 0.8)    # scaled speedup

    def test_median_not_mean(self):
        rows = [row("s", "w", "strong", 1, v, rep=i) for i, v in enumerate((2.0, 2.0, 40.0))]
        self.assertEqual(scaling.summarize_runs(rows)[("s", "w", "strong")][0]["median_s"], 2.0)

    def test_report_flags_noise_checksum_and_cpu_mismatch(self):
        rows = [row("strong-kernel", "k", "strong", 1, 1.0, checksum="A", rep=0),
                row("strong-kernel", "k", "strong", 1, 3.0, checksum="A", rep=1),
                row("strong-kernel", "k", "strong", 2, 0.5, checksum="B", usable=64)]
        md = scaling.render_markdown(scaling.summarize_runs(rows))
        self.assertIn("CV above 5%", md)
        self.assertIn("CHECKSUM DIFFERS ACROSS CORE COUNTS", md)
        self.assertIn("usable CPU count differs", md)


class PinningTests(unittest.TestCase):
    def test_parse_result(self):
        self.assertEqual(scaling.parse_result("x\nRESULT a=1 b=two\nRESULT a=3\n"), {"a": "3", "b": "two"})
        self.assertEqual(scaling.parse_result("no results here"), {})

    def test_pick_cores_stride_and_overflow(self):
        allowed = scaling.allowed_cpus()
        self.assertEqual(scaling.pick_cores(1), allowed[:1])
        with self.assertRaises(scaling.HarnessError):
            scaling.pick_cores(len(allowed) + 1)

    def test_cgroup_limit_does_not_raise(self):
        v = scaling.cgroup_cpu_limit()
        self.assertTrue(v is None or v > 0)


@unittest.skipUnless(os.name == "posix" and len(scaling.allowed_cpus()) >= 2, "needs POSIX and 2 CPUs")
class RunCycleTests(unittest.TestCase):
    def setUp(self):
        self.tmp = tempfile.mkdtemp(prefix="scaling_")
        self.addCleanup(shutil.rmtree, self.tmp, True)
        old = scaling.DEFAULT_BUILD
        scaling.DEFAULT_BUILD = self.tmp
        self.addCleanup(setattr, scaling, "DEFAULT_BUILD", old)
        for n in ("scale_kernel", "nbody", "cpu_probe"):
            open(os.path.join(self.tmp, n + ".obe"), "w").close()
        # a fake obr: prints what the real programs print, from its arguments
        self.obr = os.path.join(self.tmp, "obr")
        with open(self.obr, "w") as f:
            f.write("#!%s\n" % sys.executable + (
                "import os, sys\n"
                "a = sys.argv[1:]\n"
                "name = os.path.basename(a[0])\n"
                "cpus = os.environ.get('OBJECK_CPU_COUNT', '?')\n"
                "if name == 'scale_kernel.obe':\n"
                "    print('RESULT checksum=%d threads=%s units=%s cpus=%s kernel_ms=5' % (int(a[2]) * int(a[3]), a[1], a[2], cpus))\n"
                "elif name == 'cpu_probe.obe':\n"
                "    print('RESULT usable_cpus=%s' % cpus)\n"
                "else:\n"
                "    print('result')\n"))
        os.chmod(self.obr, os.stat(self.obr).st_mode | stat.S_IEXEC)
        self.csv = os.path.join(self.tmp, "r.csv")

    def test_strong_weak_and_clbg_then_report(self):
        rc, out, err = call(["run", "strong-kernel", "weak-kernel", "clbg-weak", "--cores", "1,2",
                             "--reps", "2", "--warmup", "0", "--obr", self.obr, "--out", self.csv,
                             "--fresh", "--units", "8", "--units-per-core", "4", "--steps", "10",
                             "--clbg", "nbody"])
        self.assertEqual(rc, 0, err)
        with open(self.csv, newline="") as f:
            rows = list(csv.DictReader(f))
        # 3 suites x 2 core counts x 2 reps
        self.assertEqual(len(rows), 12)
        strong = [r for r in rows if r["suite"] == "strong-kernel"]
        self.assertEqual(sorted(set(r["checksum"] for r in strong)), ["80"])     # same work at 1 and 2 cores
        self.assertEqual(sorted(set(r["usable_cpus"] for r in strong)), ["1", "2"])
        weak = [r for r in rows if r["suite"] == "weak-kernel"]
        self.assertEqual(sorted(set(r["units"] for r in weak)), ["4", "8"])     # work grows with the cores
        clbg = [r for r in rows if r["suite"] == "clbg-weak" and r["cores"] == "2"]
        self.assertEqual(clbg[0]["procs"], "2")
        rc, out, _ = call(["report", self.csv])
        self.assertEqual(rc, 0)
        self.assertIn("## strong-kernel / scale_kernel (strong)", out)
        self.assertIn("## weak-kernel / scale_kernel (weak)", out)
        self.assertNotIn("CHECKSUM DIFFERS", out)

    def test_checksum_mismatch_fails_the_run(self):
        with open(self.obr, "a") as f:
            pass
        bad = os.path.join(self.tmp, "obr_bad")
        with open(bad, "w") as f:
            f.write("#!" + sys.executable + "\nimport os, sys\n"
                    "print('RESULT checksum=%s kernel_ms=1' % os.environ.get('OBJECK_CPU_COUNT'))\n")
        os.chmod(bad, 0o755)
        rc, _, err = call(["run", "strong-kernel", "--cores", "1,2", "--reps", "1", "--warmup", "0",
                           "--obr", bad, "--out", self.csv, "--fresh"])
        self.assertEqual(rc, 1)
        self.assertIn("CHECKSUM MISMATCH", err)

    def test_missing_build_is_a_clear_error(self):
        os.remove(os.path.join(self.tmp, "scale_kernel.obe"))
        rc, _, err = call(["run", "strong-kernel", "--cores", "1", "--obr", self.obr, "--out", self.csv])
        self.assertEqual(rc, 2)
        self.assertIn("is not built", err)


class MultinodeTests(unittest.TestCase):
    def setUp(self):
        self.tmp = tempfile.mkdtemp(prefix="multinode_")
        self.addCleanup(shutil.rmtree, self.tmp, True)

    def test_specs_are_valid_sweeps_with_one_node_per_element(self):
        rc, out, _ = call(["multinode-specs", "--nodes", "1,2,4", "--cores-per-node", "32", "--tasks", "12",
                           "--tasks-per-node", "3", "--outdir", self.tmp, "--partition", "math-alderaan",
                           "--program", "/x/scale_kernel.obe", "--container", "/x/o.sif"])
        self.assertEqual(rc, 0)
        names = sorted(n for n in os.listdir(self.tmp) if n.endswith(".json"))
        self.assertEqual(names, ["ms_strong_k1.json", "ms_strong_k2.json", "ms_strong_k4.json",
                                 "ms_weak_k1.json", "ms_weak_k2.json", "ms_weak_k4.json"])
        m = osl.make_manifest(osl.load_spec(os.path.join(self.tmp, "ms_strong_k4.json")),
                              os.path.join(self.tmp, "o"))
        self.assertEqual(len(m["tasks"]), 12)                 # strong: fixed total
        self.assertEqual(m["resources"]["cpus_per_task"], 32)
        self.assertEqual(m["resources"]["throttle"], 4)
        self.assertIn("#SBATCH --exclusive", osl.sbatch_script(m))
        self.assertIn("--array=0-11%4", osl.sbatch_script(m))
        w = osl.make_manifest(osl.load_spec(os.path.join(self.tmp, "ms_weak_k4.json")),
                              os.path.join(self.tmp, "o"))
        self.assertEqual(len(w["tasks"]), 12)                 # weak: 3 per node x 4
        w2 = osl.make_manifest(osl.load_spec(os.path.join(self.tmp, "ms_weak_k2.json")),
                               os.path.join(self.tmp, "o"))
        self.assertEqual(len(w2["tasks"]), 6)
        self.assertTrue(os.path.exists(os.path.join(self.tmp, "submit_all.sh")))

    def fake_sweep(self, name, throttle, ntasks, makespan, hosts):
        d = os.path.join(self.tmp, name)
        os.makedirs(d)
        with open(os.path.join(d, "manifest.json"), "w") as f:
            json.dump({"name": name, "resources": {"throttle": throttle}}, f)
        with open(os.path.join(d, "results.csv"), "w", newline="") as f:
            w = csv.writer(f)
            w.writerow(["task_id", "status", "wall_s", "start_utc", "end_utc", "host"])
            for i in range(ntasks):
                # tasks start at t=0 and the last one ends at `makespan`
                end = makespan if i == ntasks - 1 else makespan / 2
                w.writerow([i, "ok", end, "2026-01-01T00:00:00.000000Z",
                            "2026-01-01T00:%02d:%02d.000000Z" % (int(end) // 60, int(end) % 60),
                            "node%d" % (i % hosts)])
        return d

    def test_multinode_report_speedup(self):
        dirs = [self.fake_sweep("ms_strong_k1", 1, 8, 80, 1), self.fake_sweep("ms_strong_k2", 2, 8, 44, 2),
                self.fake_sweep("ms_strong_k4", 4, 8, 25, 4)]
        out = io.StringIO()
        with contextlib.redirect_stdout(out):
            rc = scaling.main(["multinode-report"] + dirs)
        self.assertEqual(rc, 0)
        text = out.getvalue()
        self.assertIn("multi-node strong scaling", text)
        self.assertIn("| 4 | 8 | 4 | 25.0 |", text)
        self.assertIn("3.20", text)            # 80 / 25
        self.assertIn("80%", text)             # 3.2 / 4


if __name__ == "__main__":
    unittest.main()
