"""Tests that drive a real Objeck toolchain.

They use the deploy tree named by FUZZ_BIN, else core/release/deploy-x64/bin
(or core/release/deploy/bin) of the checkout that holds this file. Without
one they FAIL rather than skip: a fuzzer test that silently skips proves
nothing.

  * every generated program compiles, for 50 seeds across feature subsets;
  * an injected compiler fault (faults/fault_obc.py: s3 gets a wrong
    constant) and an injected JIT fault (faults/fault_obr.py: --jit=1 prints
    a wrong digit) are each caught by run_fuzz.py with the expected
    partition, while the unmodified toolchain on the same seeds is clean.
"""

import concurrent.futures
import json
import os
import sys
import tempfile
import unittest

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
import fuzzlib   # noqa: E402
import gen       # noqa: E402
import run_fuzz  # noqa: E402

EXE = ".exe" if os.name == "nt" else ""


def find_bin():
    env = os.environ.get("FUZZ_BIN")
    if env:
        return env
    root = os.path.dirname(os.path.dirname(HERE))
    candidates = [os.path.join(root, "core", "release", d, "bin") for d in ("deploy-x64", "deploy")]
    # a worktree has no deploy tree of its own; fall back to the main checkout's
    main = os.path.join(root.split(os.sep + ".claude" + os.sep)[0], "core", "release", "deploy-x64", "bin")
    candidates.append(main)
    for c in candidates:
        if os.path.exists(os.path.join(c, "obc" + EXE)):
            return c
    return None


BIN = find_bin()

# feature subsets: each layer alone, pairs, everything, nothing extra
SUBSETS = [[], ["F2"], ["F3"], ["F4"], ["F5"], ["F6"], ["F2", "F4"], ["F3", "F6"], ["F3", "F5"],
           ["F2", "F3", "F4", "F5", "F6"]]


class ToolchainTest(unittest.TestCase):
    def setUp(self):
        self.assertIsNotNone(BIN, "no Objeck deploy tree found; set FUZZ_BIN to its bin directory")
        self.tc = fuzzlib.Toolchain(BIN, timeout=60)

    def test_generated_programs_compile(self):
        work = tempfile.mkdtemp(prefix="fuzz_compile_")
        jobs = [(seed, SUBSETS[seed % len(SUBSETS)]) for seed in range(50)]

        def one(job):
            seed, feats = job
            p = gen.generate(seed=seed, features=feats)
            src = os.path.join(work, "p%d.obs" % seed)
            with open(src, "w", encoding="utf-8", newline="\n") as f:
                f.write(p.text)
            dest = os.path.join(work, "p%d.obe" % seed)
            r = self.tc.compile(src, "s3", dest)
            return seed, feats, r, os.path.exists(dest)

        failures = []
        with concurrent.futures.ThreadPoolExecutor(4) as pool:
            for seed, feats, r, wrote in pool.map(one, jobs):
                if r.code != 0 or not wrote:
                    failures.append("seed %d %s: %s %s" % (seed, feats, fuzzlib.describe_exit(r),
                                                          (r.stdout + r.stderr)[-300:]))
        self.assertEqual(failures, [])

    def fuzz(self, extra, seeds, features):
        out = tempfile.mkdtemp(prefix="fuzz_run_")
        summary = os.path.join(out, "summary.json")
        known = os.path.join(out, "known.json")
        with open(known, "w") as f:
            json.dump({"known": []}, f)
        argv = ["--bin", BIN, "--count", str(len(seeds)), "--seed", str(seeds[0]), "-j", "4",
                "--features", ",".join(features), "--out", out, "--known", known, "--json", summary,
                "--min-jit", "0"] + extra
        status = run_fuzz.main(argv)
        with open(summary) as f:
            return status, json.load(f)

    # F1+F2 only: no closures or lambdas, the layer the real 9.4 findings live in,
    # so the unmodified toolchain is clean on these seeds and any finding is the fault
    SEEDS = list(range(500, 506))

    def test_unmodified_toolchain_is_clean_on_the_fault_seeds(self):
        status, s = self.fuzz([], self.SEEDS, ["F2"])
        self.assertEqual(s["stats"]["new"], 0, s["signatures"])
        self.assertEqual(status, 0)
        self.assertGreater(s["jit_fraction"], 0.9)

    def test_injected_compiler_fault_is_caught(self):
        os.environ["FUZZ_REAL_OBC"] = os.path.join(BIN, "obc" + EXE)
        status, s = self.fuzz(["--obc", os.path.join(HERE, "faults", "fault_obc.py")], self.SEEDS, ["F2"])
        self.assertEqual(status, 1)
        self.assertGreater(s["stats"]["new"], 0)
        self.assertIn("diverge: s0/off,s0/jit1 | s3/off,s3/default,s3/jit1 first=F#=# vs F#=#",
                      s["signatures"])

    def test_injected_jit_fault_is_caught(self):
        os.environ["FUZZ_REAL_OBR"] = os.path.join(BIN, "obr" + EXE)
        status, s = self.fuzz(["--obr", os.path.join(HERE, "faults", "fault_obr.py")], self.SEEDS, ["F2"])
        self.assertEqual(status, 1)
        self.assertEqual(s["stats"]["new"], len(self.SEEDS))
        self.assertEqual(list(s["signatures"]),
                         ["diverge: s0/off,s3/off,s3/default | s3/jit1,s0/jit1 first=F#=# vs F#=#"])

    def test_reducer_refuses_a_program_that_is_not_a_finding(self):
        import reduce
        d = tempfile.mkdtemp(prefix="fuzz_reduce_clean_")
        p = gen.generate(seed=500, features=["F2"])
        with open(os.path.join(d, "choices.json"), "w") as f:
            json.dump({"seed": 500, "forced_features": ["F2"], "choices": p.choices}, f)
        dest = os.path.join(d, "findings")
        self.assertEqual(reduce.main([d, "--bin", BIN, "--dest", dest, "--name", "clean"]), 1)
        self.assertFalse(os.path.exists(os.path.join(dest, "clean.obs")))

    def test_known_json_suppresses_a_caught_fault(self):
        os.environ["FUZZ_REAL_OBR"] = os.path.join(BIN, "obr" + EXE)
        out = tempfile.mkdtemp(prefix="fuzz_known_")
        known = os.path.join(out, "known.json")
        with open(known, "w") as f:
            json.dump({"known": [{"signature": r"\| s3/jit1,s0/jit1 first=", "note": "injected"}]}, f)
        summary = os.path.join(out, "summary.json")
        status = run_fuzz.main(["--bin", BIN, "--count", "3", "--seed", "500", "-j", "3", "--features", "F2",
                                "--out", out, "--known", known, "--json", summary, "--min-jit", "0",
                                "--obr", os.path.join(HERE, "faults", "fault_obr.py")])
        with open(summary) as f:
            s = json.load(f)
        self.assertEqual((status, s["stats"]["new"], s["stats"]["known"]), (0, 0, 3))


if __name__ == "__main__":
    unittest.main()
