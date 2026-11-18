"""run_nightly.py without clang: the report parser on canned sanitizer output, and the whole
driver against fake fuzz targets (POSIX only: they are small Python scripts).

  * a finding is named by what went wrong and the first project frame, so the same bug found
    under another seed or from another input is the same signature;
  * the FAIL lines parse with tools/cicd/nightly_triage.py: one failure per distinct finding,
    named 'loader-fuzz', carrying its seed and the saved input;
  * a clean run prints no FAIL line and exits 0; a missing target or failed build is a failure,
    never a skip.

    python3 -m unittest tools/fuzz/loader/test_run_nightly.py
"""
import io
import os
import stat
import sys
import tempfile
import unittest
from contextlib import redirect_stdout

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
sys.path.insert(0, os.path.join(os.path.dirname(os.path.dirname(HERE)), "cicd"))
import nightly_triage  # noqa: E402
import run_nightly     # noqa: E402

ASAN = """==1==ERROR: AddressSanitizer: heap-buffer-overflow on address 0x60300000f0f4 at pc 0x55 bp 0x7ff sp 0x7ff
READ of size 4 at 0x60300000f0f4 thread T0
    #0 0x5581 in __asan_memcpy (/work/build/loader_fuzz+0x1)
    #1 0x5582 in Loader::LoadMethods(StackClass*, int) /home/runner/work/objeck-lang/core/vm/loader.cpp:612:5
    #2 0x5583 in Loader::Load() /home/runner/work/objeck-lang/core/vm/loader.cpp:101:3
SUMMARY: AddressSanitizer: heap-buffer-overflow loader.cpp:612:5 in Loader::LoadMethods
"""
UBSAN = "core/vm/loader.cpp:410:22: runtime error: signed integer overflow: 2147483647 + 1 cannot be represented\n"
OOM = "==2==ERROR: libFuzzer: out-of-memory (malloc(17179869184))\n    #5 0x55 in Loader::LoadClasses() /w/core/vm/loader.cpp:90:7\n"
TIMEOUT = "==3==ERROR: libFuzzer: timeout after 10 seconds\n    #4 0x55 in Loader::Verify() /w/core/vm/loader.cpp:800:1\n"
LEAK = "==4==ERROR: LeakSanitizer: detected memory leaks\n    #1 0x55 in operator new[](unsigned long) (/w/loader_fuzz+0x2)\n    #2 0x56 in Loader::ReadStrings() /w/core/vm/loader.cpp:55:9\n"

FAKE_TARGET = """#!/usr/bin/env python3
import os, sys
ASAN = %r
args = sys.argv[1:]
if len(args) == 1 and os.path.isfile(args[0]):          # replay of a saved input
    print(ASAN if os.path.basename(args[0]).startswith("crash-") else "")
    sys.exit(77 if ASAN and os.path.basename(args[0]).startswith("crash-") else 0)
art = [a.split("=", 1)[1] for a in args if a.startswith("-artifact_prefix=")][0]
os.makedirs(art, exist_ok=True)
for name in os.environ.get("FAKE_ARTIFACTS", "").split(","):
    if name:
        open(os.path.join(art, name), "wb").write(b"x")
print("#42\\tDONE cov: 10 ft: 20 corp: 3/9b exec/s: 1000")
"""


def write_target(path, artifacts_text=ASAN):
    with open(path, "w") as f:
        f.write(FAKE_TARGET % artifacts_text)
    os.chmod(path, os.stat(path).st_mode | stat.S_IEXEC)


class Describe(unittest.TestCase):
    def test_asan_names_the_error_and_the_first_project_frame(self):
        self.assertEqual(run_nightly.describe(ASAN, "crash-abc"), ("heap-buffer-overflow", "Loader::LoadMethods"))

    def test_ubsan_names_the_file(self):
        self.assertEqual(run_nightly.describe(UBSAN, "crash-abc"), ("ubsan signed integer overflow", "loader.cpp"))

    def test_libfuzzer_oom_timeout_and_leak(self):
        self.assertEqual(run_nightly.describe(OOM, "oom-1"), ("out-of-memory", "Loader::LoadClasses"))
        self.assertEqual(run_nightly.describe(TIMEOUT, "timeout-1"), ("timeout", "Loader::Verify"))
        self.assertEqual(run_nightly.describe(LEAK, "leak-1"), ("memory-leak", "Loader::ReadStrings"))

    def test_unrecognised_output_falls_back_to_the_artifact_kind(self):
        self.assertEqual(run_nightly.describe("nothing useful", "crash-0123"), ("crash", "(no project frame)"))

    def test_no_address_or_line_number_reaches_the_name(self):
        kind, where = run_nightly.describe(ASAN, "crash-abc")
        self.assertNotRegex(kind + where, r"0x|\d")

    def test_progress_line_is_found_in_fork_and_single_process_logs(self):
        with tempfile.TemporaryDirectory() as d:
            log = os.path.join(d, "l.log")
            fork_line = "#10: cov: 5 ft: 6 corp: 7 exec/s: 9 oom/timeout/crash: 0/0/0 time: 1s job: 1"
            with open(log, "w") as f:
                f.write(fork_line + "\n" + "INFO: fuzzed for 1 seconds, wrapping up soon\n"
                        + "#99\tDONE   cov: 8 ft: 9 corp: 3/1b\n")
            self.assertTrue(run_nightly.last_stats(log).startswith("#99"))
            with open(log, "w") as f:
                f.write(fork_line + "\n")
            self.assertTrue(run_nightly.last_stats(log).startswith("#10:"))

    def test_seed_is_a_number_or_any_text(self):
        self.assertEqual(run_nightly.seed_number("20261117"), 20261117)
        self.assertEqual(run_nightly.seed_number("my seed"), run_nightly.seed_number("my seed"))
        self.assertNotEqual(run_nightly.seed_number("a"), run_nightly.seed_number("b"))

    def test_artifact_search_skips_slow_units(self):
        with tempfile.TemporaryDirectory() as d:
            for name in ("crash-a", "oom-b", "timeout-c", "leak-d", "slow-unit-e", "other"):
                open(os.path.join(d, name), "w").close()
            found = [os.path.basename(p) for p in run_nightly.find_artifacts(d)]
            self.assertEqual(found, ["crash-a", "oom-b", "timeout-c", "leak-d"])


@unittest.skipIf(os.name == "nt", "the fake fuzz targets are POSIX scripts")
class Driver(unittest.TestCase):
    def setUp(self):
        self.tmp = tempfile.TemporaryDirectory()
        self.work = os.path.join(self.tmp.name, "work")
        self.build = os.path.join(self.tmp.name, "build")
        os.makedirs(self.build)
        for sub in ("slices", "files"):
            os.makedirs(os.path.join(self.work, "seeds", sub))
        write_target(os.path.join(self.build, "loader_fuzz"))
        write_target(os.path.join(self.build, "loader_file_fuzz"))

    def tearDown(self):
        self.tmp.cleanup()

    def run_driver(self, artifacts, seed=5, extra=()):
        os.environ["FAKE_ARTIFACTS"] = artifacts
        out = io.StringIO()
        with redirect_stdout(out):
            code = run_nightly.main(["--bin", self.tmp.name, "--work", self.work, "--build-dir", self.build,
                                     "--minutes", "3", "--jobs", "2", "--seed", str(seed),
                                     "--out", os.path.join(self.tmp.name, "found")] + list(extra))
        return code, out.getvalue()

    def test_clean_run_exits_zero_and_prints_no_fail_line(self):
        code, text = self.run_driver("")
        self.assertEqual(code, 0)
        self.assertNotIn("FAIL", text)
        self.assertIn("no findings", text)

    def test_one_bug_found_by_several_inputs_is_one_finding(self):
        code, text = self.run_driver("crash-aaa,crash-bbb,crash-ccc")
        self.assertEqual(code, 1)
        self.assertEqual(text.count("FAIL loader-fuzz:"), 1)
        self.assertIn("FAIL loader-fuzz: heap-buffer-overflow Loader::LoadMethods", text)
        self.assertEqual(len(os.listdir(os.path.join(self.tmp.name, "found"))), 1)

    def test_findings_parse_with_the_nightly_triage(self):
        _, text = self.run_driver("crash-aaa", seed=20261117)
        failures = nightly_triage.parse_generic_log(text, "loader-fuzz", "20261117")
        self.assertEqual(len(failures), 1)
        f = failures[0]
        self.assertEqual(f["test"], "loader-fuzz")
        self.assertEqual(f["seed"], "20261117")
        self.assertTrue(f["reduced"].endswith("crash-aaa"))

    def test_the_signature_ignores_the_seed_and_the_input(self):
        _, first = self.run_driver("crash-aaa", seed=1)
        _, second = self.run_driver("crash-zzz", seed=2)
        sigs = []
        for text, seed in ((first, "1"), (second, "2")):
            f = nightly_triage.parse_generic_log(text, "loader-fuzz", seed)[0]
            sigs.append(nightly_triage.signature("linux-x64", "loader-fuzz", "loader-fuzz",
                                                 f["test"], f["message"]))
        self.assertEqual(sigs[0], sigs[1])

    def test_a_missing_target_fails_and_never_skips(self):
        os.remove(os.path.join(self.build, "loader_file_fuzz"))
        code, text = self.run_driver("")
        self.assertEqual(code, 1)
        self.assertIn("FAIL loader-fuzz: (tool) missing target", text)


if __name__ == "__main__":
    unittest.main()
