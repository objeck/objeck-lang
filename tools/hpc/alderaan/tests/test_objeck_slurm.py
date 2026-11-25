#!/usr/bin/env python3
"""Tests for objeck_slurm.py. They need no Slurm, no Singularity and (except the
last class) no Objeck: sbatch and singularity are shims, and the "program" is
this Python interpreter. Run:  python3 tools/hpc/alderaan/tests/test_objeck_slurm.py
"""
import contextlib
import csv
import io
import json
import os
import shutil
import stat
import subprocess
import sys
import tempfile
import unittest

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, os.path.dirname(HERE))
import objeck_slurm as osl  # noqa: E402

POSIX = os.name == "posix"

TASK_PY = """\
import os, sys
n = int(sys.argv[1]); seed = int(sys.argv[2])
if os.environ.get("OBJECK_SWEEP_FAIL") == "1":
    sys.stderr.write("boom\\n"); sys.exit(3)
print("some log line")
print("RESULT acc=%d cpus=%s env_n=%s task=%s" % (n * seed, os.environ.get("OBJECK_CPU_COUNT"),
      os.environ.get("OBJECK_SWEEP_N"), os.environ.get("OBJECK_SWEEP_TASK_ID")))
"""


def run_main(argv):
    out, err = io.StringIO(), io.StringIO()
    with contextlib.redirect_stdout(out), contextlib.redirect_stderr(err):
        rc = osl.main(argv)
    return rc, out.getvalue(), err.getvalue()


def read_csv(path):
    with open(path, newline="") as f:
        return list(csv.DictReader(f))


class Base(unittest.TestCase):
    def setUp(self):
        self.tmp = tempfile.mkdtemp(prefix="objslurm_")
        self.addCleanup(shutil.rmtree, self.tmp, True)
        with open(os.path.join(self.tmp, "task.py"), "w") as f:
            f.write(TASK_PY)

    def spec(self, **over):
        s = {
            "name": "t",
            "program": "task.py",
            "runner": [sys.executable],
            "python": sys.executable,
            "args": ["{n}", "{seed}"],
            "grid": {"n": [10, 20], "seed": [1, 2, 3]},
            "resources": {"cpus_per_task": 2, "mem": "8G", "time": "00:30:00", "throttle": 3,
                          "partition": "math-alderaan"},
        }
        s.update(over)
        path = os.path.join(self.tmp, "sweep.json")
        with open(path, "w") as f:
            json.dump(s, f)
        return path

    def out(self):
        return os.path.join(self.tmp, "out")


class ExpandTests(Base):
    def test_grid_is_cartesian_in_order(self):
        tasks = osl.build_tasks(osl.load_spec(self.spec()))
        self.assertEqual(len(tasks), 6)
        self.assertEqual(tasks[0]["params"], {"n": 10, "seed": 1})
        self.assertEqual(tasks[5]["params"], {"n": 20, "seed": 3})
        self.assertEqual(tasks[4]["args"], ["20", "2"])

    def test_sets_csv_and_repeats(self):
        with open(os.path.join(self.tmp, "p.csv"), "w") as f:
            f.write("n,seed\n5,1\n6,2\n")
        spec = osl.load_spec(self.spec(csv="p.csv", grid=None, repeats=2))
        del spec["grid"]
        tasks = osl.build_tasks(spec)
        self.assertEqual([t["params"]["rep"] for t in tasks], [0, 1, 0, 1])
        self.assertEqual(tasks[2]["args"], ["6", "2"])

    def test_unknown_placeholder_and_ambiguous_source(self):
        with self.assertRaises(osl.SweepError):
            osl.build_tasks(osl.load_spec(self.spec(args=["{nope}"])))
        with self.assertRaises(osl.SweepError):
            osl.expand_params({"grid": {"a": [1]}, "sets": [{"a": 1}], "_dir": self.tmp})

    def test_bad_resources(self):
        for bad in ({"cpus_per_task": 0}, {"time": "soon"}, {"mem": "1G", "mem_per_cpu": "1G"},
                    {"throttle": 0}):
            with self.assertRaises(osl.SweepError):
                osl.make_manifest(osl.load_spec(self.spec(resources=bad)), self.out())


class ScriptTests(Base):
    def test_dry_run_prints_directives_and_writes_nothing(self):
        rc, out, _ = run_main(["plan", self.spec(), "--outdir", self.out(), "--dry-run"])
        self.assertEqual(rc, 0)
        for want in ("#SBATCH --cpus-per-task=2", "#SBATCH --mem=8G", "#SBATCH --time=00:30:00",
                     "#SBATCH --array=0-5%3", "#SBATCH --partition=math-alderaan",
                     "#SBATCH --output=%s/logs/slurm_%%A_%%a.out" % os.path.abspath(self.out()),
                     'export OBJECK_CPU_COUNT="${SLURM_CPUS_PER_TASK:-2}"', "run-task --manifest"):
            self.assertIn(want, out)
        self.assertFalse(os.path.exists(self.out()))

    def test_pack_reduces_array_size(self):
        spec = self.spec(resources={"cpus_per_task": 1, "pack": 4})
        m = osl.make_manifest(osl.load_spec(spec), self.out())
        self.assertEqual(osl.element_count(m), 2)           # 6 tasks / 4
        self.assertIn("--array=0-1\n", osl.sbatch_script(m) + "\n")
        self.assertEqual([t["id"] for t in osl.element_tasks(m, 1)], [4, 5])

    def test_container_wrapper_and_binds(self):
        spec = self.spec(container={"image": "/proj/objeck.sif", "binds": ["/data:/data"], "nv": True})
        m = osl.make_manifest(osl.load_spec(spec), "/scratch/x/out")
        cmd = osl.task_command(m, m["tasks"][0])
        self.assertEqual(cmd[:2], ["singularity", "exec"])
        self.assertIn("--nv", cmd)
        self.assertEqual(cmd[cmd.index("/proj/objeck.sif") + 1], sys.executable)
        self.assertIn("/data:/data", cmd)
        self.assertTrue(any(c.endswith(("scratch" + os.sep + "x" + os.sep + "out", "/scratch/x/out"))
                            for c in cmd))   # the outdir outside $HOME is bound

    def test_array_spec_for_resubmission(self):
        m = osl.make_manifest(osl.load_spec(self.spec()), self.out())
        self.assertEqual(osl.array_spec(m, [1, 4]), "1,4%3")


class LocalModeTests(Base):
    def test_local_sweep_collects_one_csv(self):
        rc, out, _ = run_main(["local", self.spec(), "--outdir", self.out(), "--workers", "3"])
        self.assertEqual(rc, 0, out)
        rows = read_csv(os.path.join(self.out(), "results.csv"))
        self.assertEqual(len(rows), 6)
        self.assertEqual([r["task_id"] for r in rows], [str(i) for i in range(6)])
        self.assertTrue(all(r["status"] == "ok" for r in rows))
        r = rows[3]   # n=20 seed=1
        self.assertEqual((r["n"], r["seed"], r["acc"]), ("20", "1", "20"))
        self.assertEqual(r["cpus"], "2")            # OBJECK_CPU_COUNT = cpus_per_task
        self.assertEqual(r["env_n"], "20")          # OBJECK_SWEEP_N exported
        self.assertEqual(r["task"], "3")            # OBJECK_SWEEP_TASK_ID exported
        self.assertEqual(r["slurm_job_id"], "local")
        self.assertEqual(r["cpus_per_task"], "2")

    def test_outer_cpu_count_is_not_inherited(self):
        os.environ["OBJECK_CPU_COUNT"] = "99"
        self.addCleanup(os.environ.pop, "OBJECK_CPU_COUNT", None)
        run_main(["local", self.spec(), "--outdir", self.out(), "--workers", "1"])
        self.assertEqual(read_csv(os.path.join(self.out(), "results.csv"))[0]["cpus"], "2")

    def test_failure_is_recorded_and_exit_code_nonzero(self):
        spec = self.spec(env={"OBJECK_SWEEP_FAIL": "1"})
        rc, out, _ = run_main(["local", spec, "--outdir", self.out(), "--workers", "2"])
        self.assertEqual(rc, 1)
        rows = read_csv(os.path.join(self.out(), "results.csv"))
        self.assertTrue(all(r["status"] == "failed" and r["exit_code"] == "3" for r in rows))
        rc, out, _ = run_main(["collect", self.out()])
        self.assertEqual(rc, 1)
        self.assertIn("failed task ids", out)

    def test_missing_program_is_a_failed_task_not_a_crash(self):
        spec = self.spec(runner=[os.path.join(self.tmp, "no_such_runner")])
        rc, _, _ = run_main(["local", spec, "--outdir", self.out(), "--workers", "1"])
        self.assertEqual(rc, 1)
        self.assertEqual(read_csv(os.path.join(self.out(), "results.csv"))[0]["exit_code"], "127")

    def test_task_timeout(self):
        with open(os.path.join(self.tmp, "slow.py"), "w") as f:
            f.write("import time; time.sleep(30)\n")
        spec = self.spec(program="slow.py", args=[], grid={"x": [1]}, task_timeout=1)
        rc, _, _ = run_main(["local", spec, "--outdir", self.out()])
        self.assertEqual(rc, 1)
        self.assertEqual(read_csv(os.path.join(self.out(), "results.csv"))[0]["status"], "timeout")

    def test_result_key_colliding_with_a_parameter_is_prefixed(self):
        with open(os.path.join(self.tmp, "c.py"), "w") as f:
            f.write("print('RESULT n=999 status=weird')\n")
        spec = self.spec(program="c.py", args=[], grid={"n": [1]})
        run_main(["local", spec, "--outdir", self.out()])
        row = read_csv(os.path.join(self.out(), "results.csv"))[0]
        self.assertEqual((row["n"], row["result_n"], row["result_status"], row["status"]),
                         ("1", "999", "weird", "ok"))


@unittest.skipUnless(POSIX, "needs a POSIX shell for the sbatch and singularity shims")
class SlurmShimTests(Base):
    def shim(self, name, body):
        d = os.path.join(self.tmp, "bin")
        os.makedirs(d, exist_ok=True)
        p = os.path.join(d, name)
        with open(p, "w") as f:
            f.write("#!%s\n%s" % (sys.executable, body))
        os.chmod(p, os.stat(p).st_mode | stat.S_IEXEC)
        return p

    def fake_sbatch(self):
        log = os.path.join(self.tmp, "sbatch.log")
        return self.shim("sbatch", (
            "import sys\n"
            "open(%r, 'a').write(' '.join(sys.argv[1:]) + '\\n')\n"
            "print('4242')\n") % log), log

    def test_submit_records_the_job_id(self):
        sbatch, log = self.fake_sbatch()
        rc, out, err = run_main(["submit", self.spec(), "--outdir", self.out(), "--sbatch", sbatch])
        self.assertEqual(rc, 0, err)
        self.assertIn("submitted job 4242", out)
        self.assertEqual(osl.load_manifest(self.out())["last_job_id"], "4242")
        with open(log) as f:
            called = f.read().split()
        self.assertEqual(called[0], "--parsable")
        self.assertTrue(called[1].endswith("job.sbatch"))

    def test_sbatch_failure_is_reported(self):
        bad = self.shim("sbatch_bad", "import sys\nsys.stderr.write('QOSMaxJobs\\n')\nsys.exit(1)\n")
        rc, _, err = run_main(["submit", self.spec(), "--outdir", self.out(), "--sbatch", bad])
        self.assertEqual(rc, 2)
        self.assertIn("QOSMaxJobs", err)

    def run_elements(self, script, count, cpus):
        for i in range(count):
            env = dict(os.environ, SLURM_ARRAY_TASK_ID=str(i), SLURM_CPUS_PER_TASK=str(cpus),
                       SLURM_JOB_ID=str(4242 + i), SLURM_ARRAY_JOB_ID="4242")
            env.pop("OBJECK_CPU_COUNT", None)
            subprocess.check_call(["bash", script], env=env, stdout=subprocess.DEVNULL)

    def test_generated_script_run_per_array_element_matches_local_mode(self):
        spec = self.spec()
        sbatch, _ = self.fake_sbatch()
        run_main(["submit", spec, "--outdir", self.out(), "--sbatch", sbatch])
        self.run_elements(os.path.join(self.out(), "job.sbatch"), 6, cpus=2)
        rc, _, _ = run_main(["collect", self.out()])
        self.assertEqual(rc, 0)
        slurm_rows = read_csv(os.path.join(self.out(), "results.csv"))

        local = os.path.join(self.tmp, "local")
        run_main(["local", spec, "--outdir", local])
        local_rows = read_csv(os.path.join(local, "results.csv"))

        for a, b in zip(slurm_rows, local_rows):
            for col in ("task_id", "n", "seed", "acc", "cpus", "env_n", "task", "status"):
                self.assertEqual(a[col], b[col], col)
        self.assertEqual(slurm_rows[4]["slurm_job_id"], "4246")
        self.assertEqual(slurm_rows[4]["slurm_array_job_id"], "4242")
        self.assertEqual(slurm_rows[4]["slurm_array_task_id"], "4")
        self.assertEqual(slurm_rows[4]["cpus_per_task"], "2")

    def test_slurm_cpus_per_task_wins_over_the_spec(self):
        sbatch, _ = self.fake_sbatch()
        spec = self.spec(grid={"n": [1], "seed": [1]})
        run_main(["submit", spec, "--outdir", self.out(), "--sbatch", sbatch])
        self.run_elements(os.path.join(self.out(), "job.sbatch"), 1, cpus=7)
        run_main(["collect", self.out()])
        self.assertEqual(read_csv(os.path.join(self.out(), "results.csv"))[0]["cpus"], "7")

    def test_packed_elements_run_their_slice(self):
        sbatch, _ = self.fake_sbatch()
        spec = self.spec(resources={"cpus_per_task": 1, "pack": 4})
        run_main(["submit", spec, "--outdir", self.out(), "--sbatch", sbatch])
        self.run_elements(os.path.join(self.out(), "job.sbatch"), 2, cpus=1)
        rc, out, _ = run_main(["collect", self.out()])
        self.assertEqual(rc, 0, out)

    def test_resubmit_only_failed_elements(self):
        sbatch, log = self.fake_sbatch()
        spec = self.spec()
        run_main(["submit", spec, "--outdir", self.out(), "--sbatch", sbatch])
        script = os.path.join(self.out(), "job.sbatch")
        self.run_elements(script, 6, cpus=2)
        os.remove(os.path.join(self.out(), "results", "task_00001.json"))   # a lost element
        os.remove(os.path.join(self.out(), "results", "task_00004.json"))
        rc, out, _ = run_main(["status", self.out()])
        self.assertIn("2 not finished", out)
        open(log, "w").close()
        rc, out, _ = run_main(["submit", spec, "--outdir", self.out(), "--sbatch", sbatch,
                               "--only-failed"])
        self.assertEqual(rc, 0, out)
        self.assertIn("--array=1,4%3", out)
        with open(script) as f:
            self.assertIn("#SBATCH --array=1,4%3", f.read())

    def test_container_wrapper_runs_through_a_singularity_shim(self):
        # a shim that, like `singularity exec [--bind X] IMAGE CMD...`, runs CMD
        sing = self.shim("singularity", (
            "import os, sys\n"
            "a = sys.argv[2:]\n"
            "while a[0].startswith('--'):\n"
            "    a = a[2:] if a[0] == '--bind' else a[1:]\n"
            "open(os.environ['SHIM_LOG'], 'a').write(a[0] + '\\n')\n"
            "os.execvp(a[1], a[1:])\n"))
        os.environ["SHIM_LOG"] = os.path.join(self.tmp, "sing.log")
        self.addCleanup(os.environ.pop, "SHIM_LOG", None)
        spec = self.spec(container={"image": "img.sif", "cmd": sing, "binds": ["/x:/x"]},
                         grid={"n": [3], "seed": [4]})
        run_main(["local", spec, "--outdir", self.out()])
        row = read_csv(os.path.join(self.out(), "results.csv"))[0]
        self.assertEqual((row["status"], row["acc"]), ("ok", "12"))
        with open(os.environ["SHIM_LOG"]) as f:
            self.assertEqual(f.read().strip(), "img.sif")


def find_objeck():
    """(obc, obr) from $OBJECK_BIN or a deploy tree of this checkout, else None."""
    cands = [os.environ.get("OBJECK_BIN")]
    root = os.path.abspath(os.path.join(HERE, "..", "..", "..", ".."))
    for d in ("deploy-x64", "deploy-arm64", "deploy"):
        cands.append(os.path.join(root, "core", "release", d, "bin"))
    for c in cands:
        if c:
            ext = ".exe" if os.name == "nt" else ""
            obc, obr = os.path.join(c, "obc" + ext), os.path.join(c, "obr" + ext)
            if os.path.exists(obc) and os.path.exists(obr):
                return obc, obr
    return None


@unittest.skipUnless(find_objeck(), "no obc/obr (set OBJECK_BIN to a deploy bin directory)")
class ObjeckSweepTests(Base):
    def test_sample_objeck_task_through_local_mode(self):
        obc, obr = find_objeck()
        src = os.path.join(HERE, "..", "bench", "workloads", "sweep_task.obs")
        exe = os.path.join(self.tmp, "sweep_task.obe")
        env = dict(os.environ)
        env.setdefault("OBJECK_LIB_PATH", os.path.join(os.path.dirname(obc), "..", "lib"))
        subprocess.check_call([obc, "-src", src, "-dest", exe], env=env, stdout=subprocess.DEVNULL)
        spec = self.spec(program=exe, runner=[obr], args=["{n}", "{seed}"],
                         grid={"n": [1000, 2000], "seed": [1, 7]})
        rc, out, _ = run_main(["local", spec, "--outdir", self.out(), "--workers", "2"])
        self.assertEqual(rc, 0, out)
        rows = read_csv(os.path.join(self.out(), "results.csv"))
        self.assertEqual(len(rows), 4)
        self.assertTrue(all(r["status"] == "ok" for r in rows), rows)
        self.assertEqual(rows[0]["env_n"], "1000")
        self.assertEqual(rows[0]["cpus"], "2")        # runtime.cpu.count honoured OBJECK_CPU_COUNT
        # determinism: the same parameters give the same checksum on every run
        rc, _, _ = run_main(["local", spec, "--outdir", self.out() + "2", "--workers", "1"])
        again = read_csv(os.path.join(self.out() + "2", "results.csv"))
        self.assertEqual([r["acc"] for r in rows], [r["acc"] for r in again])


if __name__ == "__main__":
    unittest.main()
