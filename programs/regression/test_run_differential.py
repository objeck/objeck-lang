#!/usr/bin/env python3
"""Unit tests for run_differential.py and tools/cicd/check_diff_markers.py.

No Objeck build is needed: obc and obr are replaced by a small Python script
behind a .cmd (Windows) or sh (POSIX) shim. The fake obc writes a JSON ".obe"
naming the test and -opt level; the fake obr reads it and prints whatever the
test's spec says for that configuration. Each case proves the runner catches
one kind of failure, or honors one marker, by making the fakes misbehave.

Run:  python3 programs/regression/test_run_differential.py
"""
import json
import os
import shutil
import subprocess
import sys
import tempfile
import textwrap
import unittest

HERE = os.path.dirname(os.path.abspath(__file__))
RUNNER = os.path.join(HERE, "run_differential.py")
LINT = os.path.join(HERE, "..", "..", "tools", "cicd", "check_diff_markers.py")

FAKE = r'''
import json, os, sys, time
tool = sys.argv[1]
args = sys.argv[2:]
spec = json.load(open(os.environ["FAKE_SPEC"]))
with open(spec["log"], "a") as log:
    log.write(json.dumps([tool] + args) + "\n")
if tool == "obc":
    src = args[args.index("-src") + 1]
    opt = args[args.index("-opt") + 1]
    dest = args[args.index("-dest") + 1]
    test = os.path.basename(src)[:-4]
    mode = spec.get("compile", {}).get(test + "/" + opt)
    if mode == "fail":
        print("error: something")
        sys.exit(1)
    if mode != "nodest":
        json.dump({"test": test, "opt": opt}, open(dest, "w"))
    sys.exit(0)
flags = [a for a in args if a.startswith("--")]
obe = json.load(open(args[-1]))
mode = {"--jit=off": "off", "--jit=1": "jit1"}.get(flags[0] if flags else "", "default")
config = obe["opt"] + "/" + mode
entry = spec.get("run", {}).get(obe["test"], {})
got = entry.get(config, entry.get("*", {}))
time.sleep(got.get("sleep", 0))
sys.stdout.write(got.get("out", "same output\n"))
sys.stderr.write("timing %s\n" % config)
sys.exit(got.get("rc", 0))
'''


class Harness(unittest.TestCase):
    def setUp(self):
        self.tmp = tempfile.mkdtemp(prefix="difftest_")
        self.bin = os.path.join(self.tmp, "bin")
        self.reg = os.path.join(self.tmp, "reg")
        os.makedirs(self.bin)
        os.makedirs(self.reg)
        fake = os.path.join(self.tmp, "fake.py")
        with open(fake, "w") as fh:
            fh.write(FAKE)
        for tool in ("obc", "obr"):
            if os.name == "nt":
                with open(os.path.join(self.bin, tool + ".cmd"), "w") as fh:
                    fh.write('@"%s" "%s" %s %%*\n' % (sys.executable, fake, tool))
            else:
                path = os.path.join(self.bin, tool)
                with open(path, "w") as fh:
                    fh.write('#!/bin/sh\nexec "%s" "%s" %s "$@"\n' % (sys.executable, fake, tool))
                os.chmod(path, 0o755)
        self.log = os.path.join(self.tmp, "calls.log")
        self.spec = {"log": self.log, "compile": {}, "run": {}}

    def tearDown(self):
        shutil.rmtree(self.tmp, ignore_errors=True)

    def add_test(self, name, header=""):
        body = textwrap.dedent(header).lstrip("\n")
        with open(os.path.join(self.reg, name + ".obs"), "w") as fh:
            fh.write(body + "class T { function : Main(args : String[]) ~ Nil { } }\n")

    def run_diff(self, *extra):
        spec_path = os.path.join(self.tmp, "spec.json")
        with open(spec_path, "w") as fh:
            json.dump(self.spec, fh)
        out_json = os.path.join(self.tmp, "result.json")
        env = dict(os.environ, FAKE_SPEC=spec_path)
        p = subprocess.run([sys.executable, RUNNER, self.bin, "--regression-dir", self.reg,
                            "--work", os.path.join(self.tmp, "work"), "--json", out_json,
                            "-j", "2", "--timeout", "3"] + list(extra),
                           env=env, stdout=subprocess.PIPE, stderr=subprocess.STDOUT)
        self.output = p.stdout.decode("utf-8", "replace")
        with open(out_json) as fh:
            results = {r["test"]: r for r in json.load(fh)["results"]}
        return p.returncode, results

    def calls(self, tool):
        if not os.path.exists(self.log):
            return []
        with open(self.log) as fh:
            return [json.loads(l)[1:] for l in fh if json.loads(l)[0] == tool]

    def run_configs(self):
        out = []
        for args in self.calls("obr"):
            opt = "s0" if args[-1].endswith("_s0.obe") else "s3"
            flag = [a for a in args if a.startswith("--")]
            out.append(opt + "/" + {"--jit=off": "off", "--jit=1": "jit1"}.get(flag[0] if flag else "", "default"))
        return sorted(out)


class RunnerTests(Harness):
    def test_agreeing_runs_pass(self):
        self.add_test("good")
        rc, res = self.run_diff()
        self.assertEqual(rc, 0, self.output)
        self.assertEqual(res["good"]["status"], "ok")
        self.assertEqual(self.run_configs(), ["s0/off", "s3/default", "s3/jit1", "s3/off"])

    def test_stdout_divergence_is_caught(self):
        self.add_test("miscompiled")
        self.spec["run"]["miscompiled"] = {"s3/jit1": {"out": "same output\nwrong 42\n"}}
        rc, res = self.run_diff()
        self.assertEqual(rc, 1)
        self.assertEqual(res["miscompiled"]["status"], "diff")
        self.assertIn("s3/jit1", " ".join(res["miscompiled"]["details"]))
        self.assertIn("DIFF miscompiled", self.output)

    def test_exit_status_divergence_is_caught(self):
        self.add_test("crashes")
        self.spec["run"]["crashes"] = {"s3/off": {"rc": 3}}
        rc, res = self.run_diff()
        self.assertEqual(rc, 1)
        self.assertEqual(res["crashes"]["status"], "diff")
        self.assertIn("exit status", res["crashes"]["details"][0])

    def test_only_zero_nonzero_is_compared(self):
        self.add_test("vmerr", "# EXPECT_RUNTIME_ERROR\n")
        self.spec["run"]["vmerr"] = {"*": {"rc": 1}, "s3/default": {"rc": 255}}
        rc, res = self.run_diff()
        self.assertEqual(rc, 0, self.output)
        self.assertEqual(res["vmerr"]["status"], "ok")

    def test_consistent_failure_is_not_a_pass(self):
        self.add_test("broken")
        self.spec["run"]["broken"] = {"*": {"rc": 1}}
        self.add_test("noerror", "# EXPECT_RUNTIME_ERROR\n")
        rc, res = self.run_diff()
        self.assertEqual(rc, 1)
        self.assertEqual(res["broken"]["status"], "fail")
        self.assertEqual(res["noerror"]["status"], "fail")

    def test_missing_obe_after_exit_zero_fails(self):
        self.add_test("nodest")
        self.spec["compile"]["nodest/s0"] = "nodest"
        rc, res = self.run_diff()
        self.assertEqual(rc, 1)
        self.assertEqual(res["nodest"]["status"], "fail")
        self.assertIn("wrote no .obe", res["nodest"]["details"][0])
        self.assertEqual(self.calls("obr"), [])

    def test_compile_failure_fails(self):
        self.add_test("nocompile")
        self.spec["compile"]["nocompile/s3"] = "fail"
        rc, res = self.run_diff()
        self.assertEqual(rc, 1)
        self.assertIn("exited 1", res["nocompile"]["details"][0])

    def test_timeout_fails(self):
        self.add_test("hangs")
        self.spec["run"]["hangs"] = {"s3/jit1": {"sleep": 6}}
        rc, res = self.run_diff()
        self.assertEqual(rc, 1)
        self.assertIn("timed out", res["hangs"]["details"][0])

    def test_diff_configs_limits_the_comparison(self):
        self.add_test("tco", "# DIFF_CONFIGS: s3\n# reason: needs TCO\n")
        self.spec["run"]["tco"] = {"s0/off": {"out": "stack overflow\n", "rc": 1}}
        rc, res = self.run_diff()
        self.assertEqual(rc, 0, self.output)
        self.assertEqual(self.run_configs(), ["s3/default", "s3/jit1", "s3/off"])
        self.assertFalse(any("s0" in c[c.index("-opt") + 1] for c in self.calls("obc")))

    def test_diff_configs_by_name(self):
        self.add_test("pair", "# DIFF_CONFIGS: s3/off, s3/jit1\n# reason: x\n")
        self.spec["run"]["pair"] = {"s3/default": {"out": "other\n"}}
        rc, _ = self.run_diff()
        self.assertEqual(rc, 0, self.output)
        self.assertEqual(self.run_configs(), ["s3/jit1", "s3/off"])

    def test_bad_diff_configs_fails(self):
        self.add_test("typo", "# DIFF_CONFIGS: s2\n# reason: x\n")
        rc, res = self.run_diff()
        self.assertEqual(rc, 1)
        self.assertIn("bad DIFF_CONFIGS", res["typo"]["details"][0])

    def test_requires_jit_skips_interpreter_configs(self):
        self.add_test("speed", "# DIFF_REQUIRES_JIT\n# reason: timing bound\n")
        self.spec["run"]["speed"] = {"s0/off": {"rc": 1}, "s3/off": {"rc": 1}}
        rc, _ = self.run_diff()
        self.assertEqual(rc, 0, self.output)
        self.assertEqual(self.run_configs(), ["s3/default", "s3/jit1"])

    def test_nondeterministic_output_compares_status_only(self):
        self.add_test("clock", "# NONDETERMINISTIC_OUTPUT\n# reason: prints the time\n")
        self.spec["run"]["clock"] = {"s3/jit1": {"out": "12:00\n"}}
        self.add_test("clock_crash", "# NONDETERMINISTIC_OUTPUT\n# reason: prints the time\n")
        self.spec["run"]["clock_crash"] = {"s3/jit1": {"out": "12:00\n", "rc": 1}}
        rc, res = self.run_diff()
        self.assertEqual(res["clock"]["status"], "ok")
        self.assertEqual(res["clock_crash"]["status"], "diff")
        self.assertEqual(rc, 1)

    def test_markers_are_line_anchored(self):
        # prose mentioning a marker must not act as one
        self.add_test("prose", "#~ never add # DIFF_REQUIRES_JIT here ~#\n")
        self.spec["run"]["prose"] = {"s0/off": {"rc": 1}}
        rc, res = self.run_diff()
        self.assertEqual(res["prose"]["status"], "diff")

    def test_serial_marker_and_detection(self):
        self.add_test("alone", "# DIFF_SERIAL\n")
        self.add_test("threads", "use System.Concurrency;\n")
        self.add_test("plain")
        rc, res = self.run_diff()
        self.assertEqual(rc, 0, self.output)
        self.assertTrue(res["alone"]["serial"])
        self.assertTrue(res["threads"]["serial"])
        self.assertFalse(res["plain"]["serial"])
        # serial tests run after the parallel pool
        order = [os.path.basename(c[c.index("-src") + 1]) for c in self.calls("obc")]
        self.assertEqual(order[-4:-2] + order[-2:], sorted(order[-4:], key=order.index))
        self.assertTrue(all(n != "plain.obs" for n in order[2:]))

    def test_compile_error_tests_are_skipped(self):
        self.add_test("bad", "# EXPECT_COMPILE_ERROR: undefined\n")
        rc, res = self.run_diff()
        self.assertEqual(rc, 0)
        self.assertEqual(res["bad"]["status"], "skip")
        self.assertEqual(self.calls("obc"), [])

    def test_jit_optout_runs_interpreter_only(self):
        self.add_test("optout", "# JIT_DISABLE\n# reason: arm64 bug\n")
        self.spec["run"]["optout"] = {"s3/jit1": {"rc": 1}}
        rc, _ = self.run_diff()
        self.assertEqual(rc, 0, self.output)
        self.assertEqual(self.run_configs(), ["s0/off", "s3/off"])

    def test_rotate_adds_s0_jit1(self):
        self.add_test("rot")
        self.spec["run"]["rot"] = {"s0/jit1": {"out": "nope\n"}}
        rc, _ = self.run_diff()
        self.assertEqual(rc, 0, self.output)
        rc, res = self.run_diff("--rotate")
        self.assertEqual(rc, 1)
        self.assertIn("s0/jit1", " ".join(res["rot"]["details"]))

    def test_extra_libs_reach_the_compiler(self):
        self.add_test("libs", "# EXTRA_LIBS: regex\n")
        self.run_diff()
        for args in self.calls("obc"):
            self.assertEqual(args[args.index("-lib") + 1], "cipher,collect,xml,json,regex")


class LintTests(unittest.TestCase):
    def setUp(self):
        self.dir = tempfile.mkdtemp(prefix="difflint_")
        for i in range(20):
            self.write("t%02d" % i, "")

    def tearDown(self):
        shutil.rmtree(self.dir, ignore_errors=True)

    def write(self, name, header):
        with open(os.path.join(self.dir, name + ".obs"), "w") as fh:
            fh.write(header + "class T {}\n")

    def lint(self):
        p = subprocess.run([sys.executable, LINT, "--dir", self.dir],
                           stdout=subprocess.PIPE, stderr=subprocess.STDOUT)
        return p.returncode, p.stdout.decode()

    def test_clean_tree_passes(self):
        self.write("t00", "# NONDETERMINISTIC_OUTPUT\n# reason: prints a pointer\n")
        self.write("t01", "# DIFF_CONFIGS: s3\n# reason: needs TCO\n# DIFF_SERIAL\n")
        rc, out = self.lint()
        self.assertEqual(rc, 0, out)

    def test_nondeterministic_without_reason_fails(self):
        self.write("t00", "# NONDETERMINISTIC_OUTPUT\nuse System;\n")
        rc, out = self.lint()
        self.assertEqual(rc, 1)
        self.assertIn("t00.obs:1", out)

    def test_empty_reason_fails(self):
        self.write("t00", "# DIFF_REQUIRES_JIT\n# reason:   \n")
        rc, out = self.lint()
        self.assertEqual(rc, 1)

    def test_cap_is_enforced(self):
        # 20 tests: 5% allows exactly one
        self.write("t00", "# NONDETERMINISTIC_OUTPUT\n# reason: a\n")
        self.assertEqual(self.lint()[0], 0)
        self.write("t01", "# NONDETERMINISTIC_OUTPUT\n# reason: b\n")
        rc, out = self.lint()
        self.assertEqual(rc, 1)
        self.assertIn("over the cap", out)

    def test_bad_config_stray_mention_and_whitespace_fail(self):
        for header, needle in (("# DIFF_CONFIGS: s9\n# reason: x\n", "no valid configuration"),
                               ("#~ do not add # DIFF_SERIAL ~#\n", "literal text"),
                               ("# DIFF_SERIAL \n", "trailing whitespace"),
                               ("# DIFF_REQUIRES_JIT\n# reason: x\n# JIT_DISABLE\n# reason: y\n",
                                "nothing to compare")):
            self.write("t05", header)
            rc, out = self.lint()
            self.assertEqual(rc, 1, header)
            self.assertIn(needle, out)

    def test_repository_tree_conforms(self):
        p = subprocess.run([sys.executable, LINT], stdout=subprocess.PIPE, stderr=subprocess.STDOUT)
        self.assertEqual(p.returncode, 0, p.stdout.decode())


if __name__ == "__main__":
    unittest.main()
