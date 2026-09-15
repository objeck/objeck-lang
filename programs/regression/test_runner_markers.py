#!/usr/bin/env python3
"""Tests for the regression runners' negative-test markers and OBJECK_VM_ARGS.

Each test class builds a throwaway `programs/regression` directory holding
copies of run_regression.cmd and run_regression.sh plus a few tiny .obs files,
links a real deploy tree in as `core/release/deploy-x64`, runs one runner over
it and checks the verdict it gives each file:

  * `# EXPECT_COMPILE_ERROR: <message>` passes only when <message> is in the
    compiler output, case-sensitively; the plain marker still passes but
    prints a WARN line;
  * a marker in the middle of a comment line is ignored (issue #729's shape);
  * OBJECK_VM_ARGS reaches obr, before the .obe, split on blanks.

Both runners are held to the same EXPECTED table, so they cannot drift apart.

Deploy tree: $OBJECK_TEST_DEPLOY, else core/release/deploy-x64, deploy-arm64
or deploy in this checkout. Runner scripts: $RUNNER_SCRIPT_DIR (to aim these
tests at another revision of the runners), else this directory. The bash
runner is exercised with bash on POSIX and Git Bash on Windows.

Run:  python programs/regression/test_runner_markers.py
"""
import os
import re
import shutil
import subprocess
import sys
import tempfile
import unittest

HERE = os.path.dirname(os.path.abspath(__file__))
REPO = os.path.dirname(os.path.dirname(HERE))
WINDOWS = os.name == "nt"
RUNNER_DIR = os.environ.get("RUNNER_SCRIPT_DIR") or HERE

sys.path.insert(0, HERE)
import gen_manifest  # noqa: E402

BAD_TYPE = """class {name} {{
  function : Main(args : String[]) ~ Nil {{
    x : NoSuchTypeAnywhere;
  }}
}}
"""

HELLO = """class {name} {{
  function : Main(args : String[]) ~ Nil {{
    "hello"->PrintLine();
  }}
}}
"""

BOUNDS = """class {name} {{
  function : Main(args : String[]) ~ Nil {{
    args[5]->PrintLine();
  }}
}}
"""

HANG = """class {name} {{
  function : Main(args : String[]) ~ Nil {{
    i := 0;
    while(true) {{ i += 1; }};
  }}
}}
"""

TIMEOUT_SECONDS = 8

NAMED = "# EXPECT_COMPILE_ERROR: Undefined class or enum: 'NoSuchTypeAnywhere'\n"

# name -> (source, expected status, expects a WARN line)
FIXTURES = {
    "ce_named": (NAMED + BAD_TYPE, "PASS", False),
    "ce_padded": ("# EXPECT_COMPILE_ERROR:    Undefined class or enum\n" + BAD_TYPE, "PASS", False),
    "ce_crlf": ((NAMED + BAD_TYPE).replace("\n", "\r\n"), "PASS", False),
    "ce_wrong": ("# EXPECT_COMPILE_ERROR: Array dimensions cannot exceed 8\n" + BAD_TYPE, "FAIL", False),
    "ce_case": ("# EXPECT_COMPILE_ERROR: undefined class or enum\n" + BAD_TYPE, "FAIL", False),
    "ce_plain": ("# EXPECT_COMPILE_ERROR\n" + BAD_TYPE, "PASS", True),
    "ce_compiles": (NAMED + HELLO, "FAIL", False),
    "mid_markers": ("#~\nNot a negative test: this comment only names # EXPECT_COMPILE_ERROR\n"
                    "and # EXPECT_RUNTIME_ERROR in the middle of a line.\n~#\n" + HELLO, "PASS", False),
    "rt_expected": ("# EXPECT_RUNTIME_ERROR\n" + BOUNDS, "PASS", False),
    "rt_mid_marker": ("# this comment mentions # EXPECT_RUNTIME_ERROR mid-line\n" + BOUNDS, "FAIL", False),
}

RESULTS_RE = re.compile(r"Results: (\d+) passed, (\d+) skipped, (\d+) failed")


def find_deploy():
    exe = "obc.exe" if WINDOWS else "obc"
    candidates = [os.environ.get("OBJECK_TEST_DEPLOY")]
    candidates += [os.path.join(REPO, "core", "release", n) for n in ("deploy-x64", "deploy-arm64", "deploy")]
    for candidate in candidates:
        if candidate and os.path.isfile(os.path.join(candidate, "bin", exe)):
            return os.path.abspath(candidate)
    return None


def find_bash():
    if not WINDOWS:
        return shutil.which("bash")
    for candidate in (os.environ.get("GIT_BASH"),
                      r"C:\Program Files\Git\bin\bash.exe",
                      r"C:\Program Files (x86)\Git\bin\bash.exe"):
        if candidate and os.path.isfile(candidate):
            return candidate
    found = shutil.which("bash")
    # System32\bash.exe is WSL, which cannot run the Windows deploy tree.
    if found and "system32" not in found.lower():
        return found
    return None


def make_tree(root, fixtures, deploy):
    """Lay out root/programs/regression + root/core/release/deploy-x64 (a link)."""
    reg = os.path.join(root, "programs", "regression")
    os.makedirs(reg)
    for script in ("run_regression.cmd", "run_regression.sh", "run_with_timeout.ps1"):
        if os.path.exists(os.path.join(RUNNER_DIR, script)):
            shutil.copyfile(os.path.join(RUNNER_DIR, script), os.path.join(reg, script))
    release = os.path.join(root, "core", "release")
    os.makedirs(release)
    link = os.path.join(release, "deploy-x64")
    if WINDOWS:
        subprocess.run(["cmd.exe", "/d", "/c", "mklink", "/J", link, deploy],
                       check=True, capture_output=True)
    else:
        os.symlink(deploy, link)
    for name, source in fixtures.items():
        with open(os.path.join(reg, name + ".obs"), "wb") as handle:
            handle.write(source.format(name=name.title().replace("_", "")).encode("utf-8"))
    return reg, link


def remove_tree(root, links):
    # Drop the links first so nothing under the real deploy tree is touched.
    for link in links:
        if os.path.lexists(link):
            if WINDOWS:
                os.rmdir(link)
            else:
                os.unlink(link)
    shutil.rmtree(root, ignore_errors=True)


def parse_verdicts(output):
    """Map each 'Running: <name>...' block to its status and WARN flag."""
    verdicts = {}
    current = None
    for line in output.splitlines():
        stripped = line.strip()
        match = re.match(r"Running: (.+?)\.\.\.$", stripped)
        if match:
            current = {"status": None, "warn": False, "text": []}
            verdicts[match.group(1)] = current
            continue
        if stripped.startswith("====="):
            current = None
            continue
        if current is None:
            continue
        current["text"].append(line)
        match = re.match(r"\[?(PASS|FAIL|SKIP|WARN)\]?(\s|$)", stripped)
        if match:
            if match.group(1) == "WARN":
                current["warn"] = True
            elif current["status"] is None:
                current["status"] = match.group(1)
    for verdict in verdicts.values():
        verdict["text"] = "\n".join(verdict["text"])
    return verdicts


def run_runner(shell, reg, vm_args=None, test_timeout=None):
    env = {k: v for k, v in os.environ.items()
           if k.upper() not in ("NODEFAULTCURRENTDIRECTORYINEXEPATH", "OBJECK_VM_ARGS",
                                "GITHUB_STEP_SUMMARY", "OBJECK_JIT_DISABLE", "TEST_TIMEOUT")}
    if vm_args is not None:
        env["OBJECK_VM_ARGS"] = vm_args
    if test_timeout is not None:
        env["TEST_TIMEOUT"] = str(test_timeout)
    if shell == "cmd":
        command = ["cmd.exe", "/d", "/c", os.path.join(reg, "run_regression.cmd"), "x64"]
    else:
        command = [find_bash(), "./run_regression.sh", "x64"]
    proc = subprocess.run(command, cwd=reg, env=env, capture_output=True, timeout=900)
    output = (proc.stdout + proc.stderr).decode("utf-8", errors="replace")
    return proc.returncode, output, parse_verdicts(output)


class RunnerMarkerTests(object):
    SHELL = None

    @classmethod
    def setUpClass(cls):
        deploy = find_deploy()
        if deploy is None:
            raise unittest.SkipTest("no deploy tree with obc; set OBJECK_TEST_DEPLOY")
        if cls.SHELL == "cmd" and not WINDOWS:
            raise unittest.SkipTest("run_regression.cmd needs Windows")
        if cls.SHELL == "bash" and find_bash() is None:
            raise unittest.SkipTest("no usable bash")

        cls.tmp = tempfile.mkdtemp(prefix="objeck_runner_%s_" % cls.SHELL)
        links = []
        cls.addClassCleanup(remove_tree, cls.tmp, links)

        sources = {name: spec[0] for name, spec in FIXTURES.items()}
        cls.reg, link = make_tree(os.path.join(cls.tmp, "markers"), sources, deploy)
        links.append(link)
        cls.code, cls.out, cls.verdicts = run_runner(cls.SHELL, cls.reg)

        vm_reg, link = make_tree(os.path.join(cls.tmp, "vmargs"), {"vm_hello": HELLO}, deploy)
        links.append(link)
        cls.vm_runs = {}
        for args in (None, "--gc-threshold=2x", "--jit=off --gc-threshold=2x", "--jit=off --gc-threshold=2m"):
            cls.vm_runs[args] = run_runner(cls.SHELL, vm_reg, args)

        # TEST_TIMEOUT: nightly-hardening.yml sets it for every leg; the Windows
        # runner used to ignore it, so one hang stalled the whole step
        to_reg, link = make_tree(os.path.join(cls.tmp, "timeout"),
                                 {"to_hang": HANG, "to_hello": HELLO, "to_bounds": BOUNDS}, deploy)
        links.append(link)
        cls.timeout_run = run_runner(cls.SHELL, to_reg, "--jit=off", test_timeout=TIMEOUT_SECONDS)

    def verdict(self, name, runs=None):
        verdicts = self.verdicts if runs is None else runs[2]
        output = self.out if runs is None else runs[1]
        self.assertIn(name, verdicts, "runner never reported %s:\n%s" % (name, output))
        return verdicts[name]

    def assertVerdict(self, name):
        _, status, warn = FIXTURES[name]
        verdict = self.verdict(name)
        self.assertEqual(verdict["status"], status, "%s:\n%s" % (name, verdict["text"]))
        self.assertEqual(verdict["warn"], warn, "%s WARN line:\n%s" % (name, verdict["text"]))
        return verdict

    # -- '# EXPECT_COMPILE_ERROR: <message>' ----------------------------------
    def test_named_message_in_output_passes(self):
        self.assertVerdict("ce_named")

    def test_named_message_blanks_are_trimmed(self):
        self.assertVerdict("ce_padded")

    def test_named_message_in_crlf_file_passes(self):
        self.assertVerdict("ce_crlf")

    def test_wrong_message_fails(self):
        verdict = self.assertVerdict("ce_wrong")
        self.assertIn("does not contain expected message: Array dimensions cannot exceed 8", verdict["text"])

    def test_message_match_is_case_sensitive(self):
        self.assertVerdict("ce_case")

    def test_plain_marker_passes_with_warning(self):
        self.assertVerdict("ce_plain")

    def test_named_marker_on_clean_compile_fails(self):
        verdict = self.assertVerdict("ce_compiles")
        self.assertIn("should have failed to compile", verdict["text"])

    # -- anchoring ------------------------------------------------------------
    def test_mid_line_markers_are_ignored(self):
        self.assertVerdict("mid_markers")
        self.assertVerdict("rt_mid_marker")

    def test_runtime_marker_at_line_start(self):
        self.assertVerdict("rt_expected")

    def test_summary_counts_and_exit_code(self):
        match = RESULTS_RE.search(self.out)
        self.assertIsNotNone(match, self.out)
        passes = sum(1 for spec in FIXTURES.values() if spec[1] == "PASS")
        fails = len(FIXTURES) - passes
        self.assertEqual((int(match.group(1)), int(match.group(3))), (passes, fails), self.out)
        self.assertEqual(self.code, 1)

    # -- OBJECK_VM_ARGS -------------------------------------------------------
    def test_no_vm_args(self):
        code, out, _ = self.vm_runs[None]
        self.assertEqual(self.verdict("vm_hello", self.vm_runs[None])["status"], "PASS", out)
        self.assertEqual(code, 0, out)
        self.assertNotIn("VM args:", out)

    def test_vm_args_reach_obr(self):
        runs = self.vm_runs["--gc-threshold=2x"]
        self.assertIn("VM args: --gc-threshold=2x", runs[1])
        self.assertEqual(self.verdict("vm_hello", runs)["status"], "FAIL", runs[1])
        self.assertIn("--gc-threshold: expected", runs[1])
        self.assertEqual(runs[0], 1, runs[1])

    def test_vm_args_are_split_on_blanks(self):
        # The second word must reach obr as its own argument: rejected when bad,
        # accepted when good (as one argument '--jit=off --gc-threshold=2m' obr
        # would reject the --jit value).
        bad = self.vm_runs["--jit=off --gc-threshold=2x"]
        self.assertEqual(self.verdict("vm_hello", bad)["status"], "FAIL", bad[1])
        good = self.vm_runs["--jit=off --gc-threshold=2m"]
        self.assertEqual(self.verdict("vm_hello", good)["status"], "PASS", good[1])
        self.assertEqual(good[0], 0, good[1])

    # -- TEST_TIMEOUT ---------------------------------------------------------
    def test_hanging_test_times_out(self):
        code, out, _ = self.timeout_run
        verdict = self.verdict("to_hang", self.timeout_run)
        self.assertEqual(verdict["status"], "FAIL", out)
        self.assertIn("timed out after %ds" % TIMEOUT_SECONDS, verdict["text"])
        self.assertEqual(code, 1, out)

    def test_timeout_keeps_output_exit_codes_and_vm_args(self):
        code, out, _ = self.timeout_run
        self.assertEqual(self.verdict("to_hello", self.timeout_run)["status"], "PASS", out)
        # a real non-zero exit still fails as a runtime error, not a timeout
        bounds = self.verdict("to_bounds", self.timeout_run)
        self.assertEqual(bounds["status"], "FAIL", out)
        self.assertNotIn("timed out", bounds["text"])
        self.assertIn("VM args: --jit=off", out)
        match = RESULTS_RE.search(out)
        self.assertIsNotNone(match, out)
        self.assertEqual((int(match.group(1)), int(match.group(3))), (1, 2), out)


class CmdRunnerTests(RunnerMarkerTests, unittest.TestCase):
    SHELL = "cmd"


class BashRunnerTests(RunnerMarkerTests, unittest.TestCase):
    SHELL = "bash"


class GenManifestMarkerTests(unittest.TestCase):
    def test_only_line_start_markers_make_a_test_negative(self):
        tmp = tempfile.mkdtemp(prefix="objeck_manifest_")
        self.addCleanup(shutil.rmtree, tmp, True)
        files = {
            "core_mid.obs": FIXTURES["mid_markers"][0],
            "core_named.obs": FIXTURES["ce_named"][0],
            "core_runtime.obs": FIXTURES["rt_expected"][0],
        }
        for name, source in files.items():
            with open(os.path.join(tmp, name), "w", encoding="utf-8") as handle:
                handle.write(source.format(name="T"))
        cwd = os.getcwd()
        os.chdir(tmp)
        try:
            rows = {name: category for name, category, _ in gen_manifest.build()}
        finally:
            os.chdir(cwd)
        self.assertEqual(rows["core_mid"], "Core Language")
        self.assertEqual(rows["core_named"], "Core Language (neg)")
        self.assertEqual(rows["core_runtime"], "Core Language (neg)")


if __name__ == "__main__":
    unittest.main(verbosity=2)
