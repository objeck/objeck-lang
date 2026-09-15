"""Unit tests for partitions, signatures, the JIT report parser and known.json
suppression (no toolchain needed)."""

import json
import os
import sys
import tempfile
import unittest

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import fuzzlib  # noqa: E402
from fuzzlib import Result  # noqa: E402

ORDER = [c[0] for c in fuzzlib.CONFIGS]
AV = 3221225477  # 0xC0000005


def ok(out, code=0, err="", secs=0.1):
    return Result(code, out, err, secs)


def all_same(out="F0=1\nF1=2\n"):
    return {n: ok(out) for n in ORDER}


class PartitionTest(unittest.TestCase):
    def test_agreement_is_one_class_and_no_signature(self):
        r = all_same()
        self.assertEqual(fuzzlib.partition(r, ORDER), [ORDER])
        self.assertIsNone(fuzzlib.signature(r))

    def test_classes_follow_config_order(self):
        r = all_same()
        r["s3/jit1"] = ok("F0=1\nF1=3\n")
        r["s0/jit1"] = ok("F0=1\nF1=3\n")
        self.assertEqual(fuzzlib.partition_text(fuzzlib.partition(r, ORDER)),
                         "s0/off,s3/off,s3/default | s3/jit1,s0/jit1")

    def test_exit_status_splits_equal_output(self):
        r = all_same()
        r["s3/off"] = ok("F0=1\nF1=2\n", code=1)
        self.assertEqual(len(fuzzlib.partition(r, ORDER)), 2)

    def test_timeout_is_its_own_class(self):
        r = all_same()
        r["s3/default"] = Result(None, "", "", 30.0, timed_out=True)
        groups = fuzzlib.partition(r, ORDER)
        self.assertIn(["s3/default"], groups)


class SignatureTest(unittest.TestCase):
    def test_miscompile_signature_names_partition_and_first_line(self):
        r = all_same("F0=10\nF1=20\nF2=30\n")
        r["s3/default"] = ok("F0=10\nF1=21\nF2=30\n")
        r["s3/jit1"] = ok("F0=10\nF1=21\nF2=30\n")
        sig = fuzzlib.signature(r)
        self.assertEqual(sig, "diverge: s0/off,s3/off,s0/jit1 | s3/default,s3/jit1 first=F#=# vs F#=#")

    def test_signature_is_stable_across_values(self):
        a = all_same("F0=10\n")
        a["s3/off"] = ok("F0=11\n")
        b = all_same("F0=99999\n")
        b["s3/off"] = ok("F0=-5\n")
        self.assertEqual(fuzzlib.signature(a), fuzzlib.signature(b))

    def test_crash_signature_has_error_text_and_top_frame(self):
        r = all_same()
        err = ("[jit] Foo:Bar:i,: pinned 2 local(s) in loop [1,2], slot id(s) 3 4\n"
               ">>> Attempting to dereference a 'Nil' memory instance in JIT-to-JIT call: "
               "method='FuzzProgram:F3:i,', status=-1, caller='FuzzProgram:Main:o.System.String*,' <<<\n")
        r["s3/jit1"] = ok("F0=1\n", code=1, err=err)
        sig = fuzzlib.signature(r)
        self.assertIn("diverge: s0/off,s3/off,s3/default,s0/jit1 | s3/jit1", sig)
        r["s3/jit1"] = Result(AV, "", err, 0.5)
        sig = fuzzlib.signature(r)
        self.assertTrue(sig.startswith("crash: s3/jit1 access violation >>> Attempting to dereference"), sig)
        self.assertIn("@FuzzProgram:F3:i,;", sig)

    def test_runtime_error_adds_error_and_unwound_frame(self):
        r = all_same("F0=1\nF1=2\n")
        err = (">>> Attempting to dereference a 'Nil' memory element <<<\n"
               "Unwinding local stack (0000021692BC2700):\n"
               "  method: pos=2, name='FuzzProgram->F0(a:Int, b:Int)', file='prog.obs'\n"
               "  method: pos=1, name='FuzzProgram->Main(a:String[])'\n")
        for name in ("s3/off", "s3/default", "s3/jit1"):
            r[name] = ok("", code=1, err=err)
        self.assertEqual(fuzzlib.signature(r),
                         "diverge: s0/off,s0/jit1 | s3/off,s3/default,s3/jit1 first=F#=# vs  exit 1 "
                         ">>> Attempting to dereference a 'Nil' memory element <<< "
                         "@FuzzProgram->F0(a:Int, b:Int)")
        # a native crash prints nothing: no empty error or frame fields
        r["s3/off"] = Result(AV, "", "", 0.2)
        self.assertTrue(fuzzlib.signature(r).startswith("crash: s3/off access violation; diverge: "))

    def test_all_configs_failing_alike_is_still_a_finding(self):
        r = {n: ok("", code=1, err=">>> Index out of bounds: 9 <<<\n") for n in ORDER}
        self.assertEqual(fuzzlib.signature(r), "fail: all exit 1 >>> Index out of bounds: # <<<")

    def test_first_difference_handles_short_output(self):
        self.assertEqual(fuzzlib.first_difference("a\nb\n", "a\n"), (2, "b", ""))
        self.assertIsNone(fuzzlib.first_difference("a\n", "a\n"))

    def test_compile_signatures(self):
        crash = Result(AV, "", "", 1.0)
        good = Result(0, "Compiled 1 class.\n", "", 1.0)
        err = Result(2, "", "C:\\x\\prog.obs:(12,5): Undefined variable: 'v7'\n", 1.0)
        self.assertEqual(fuzzlib.compile_signature({"s0": (good, True), "s3": (crash, False)}),
                         "compile: s3 access violation")
        self.assertEqual(fuzzlib.compile_signature({"s0": (err, False), "s3": (good, True)}),
                         "compile: s0 error Undefined variable: 'v#'")
        self.assertEqual(fuzzlib.compile_signature({"s0": (good, True), "s3": (good, False)}),
                         "compile: s3 missing .obe")
        self.assertIsNone(fuzzlib.compile_signature({"s0": (good, True), "s3": (good, True)}))


class JitReportTest(unittest.TestCase):
    STDERR = "\n".join([
        "[jit] System.String:Append:c*,: pinned 2 local(s) in loop [57,86], slot id(s) 5 4",
        "[jit] FuzzProgram:F1:i,i,: not compiled -- unsupported opcode 77 at instruction 12",
        "[jit] FuzzProgram:Main:o.System.String*,: compiled on entry (has a loop)",
        "[jit] FzTree:Check:: compile failed at instruction 40",
        "[jit] FuzzProgram:#lambda_0:: not compiled -- a frame of 40000 bytes",
        "[jit] System.String:Get:i,: compile failed at instruction 3",
    ])

    def test_rejections(self):
        rej = fuzzlib.jit_rejections(self.STDERR)
        self.assertEqual(set(rej), {"FuzzProgram:F1:i,i,", "FzTree:Check:", "FuzzProgram:#lambda_0:",
                                    "System.String:Get:i,"})

    def test_coverage_needs_positive_evidence(self):
        generated = ["FuzzProgram:F0", "FuzzProgram:F1", "FzTree:Check", "FzTree:Build"]
        stderr = self.STDERR + "\n" + "\n".join([
            "[jit] FuzzProgram:F0:i,i,: compiled",
            "[jit] FuzzProgram:F1:i,i,: compiled",       # an overload; F1 was still rejected
            "[jit] FuzzProgram:F0:i,i,: inlined FzTree:Build:i,i,",
            "[jit] System.String:Size:: compiled",       # library, not ours
        ])
        compiled, total, uncovered = fuzzlib.jit_coverage(stderr, generated, ("FuzzProgram:", "Fz"))
        # 4 generated + the rejected lambda + Main (compiled on entry); F0 compiled,
        # Build inlined, Main compiled; F1, Check and the lambda rejected
        self.assertEqual((compiled, total), (3, 6))
        self.assertEqual(uncovered, ["FuzzProgram:#lambda_0:", "FuzzProgram:F1", "FzTree:Check"])

    def test_method_the_report_never_names_is_not_compiled(self):
        generated = ["FuzzProgram:F0", "FuzzProgram:F1"]
        compiled, total, uncovered = fuzzlib.jit_coverage("[jit] FuzzProgram:F0:: compiled\n",
                                                          generated, ("FuzzProgram:",))
        self.assertEqual((compiled, total), (1, 2))
        self.assertEqual(uncovered, ["FuzzProgram:F1" + fuzzlib.NEVER_COMPILED])

    def test_a_vm_that_ignores_the_jit_scores_zero(self):
        # The old metric subtracted rejections from the total, so an empty report
        # -- what an obr that ignores --jit=1 prints -- read as 100% compiled.
        generated = ["FuzzProgram:F0", "FuzzProgram:F1", "FzTree:Check"]
        compiled, total, uncovered = fuzzlib.jit_coverage("", generated, ("FuzzProgram:", "Fz"))
        self.assertEqual((compiled, total), (0, 3))
        self.assertEqual(len(uncovered), 3)


class KnownTest(unittest.TestCase):
    def write(self, data):
        fd, path = tempfile.mkstemp(suffix=".json")
        with os.fdopen(fd, "w") as f:
            json.dump(data, f)
        self.addCleanup(os.remove, path)
        return path

    def test_suppression_by_regex(self):
        path = self.write({"schema": 1, "known": [
            {"signature": r"^crash: s\d/(jit1|default) access violation", "note": "basic lambda, jit"},
            {"signature": r"\| s3/off first=", "note": "s3 interpreter"},
        ]})
        known = fuzzlib.load_known(path)
        self.assertEqual(fuzzlib.match_known("crash: s3/jit1 access violation  @", known)["note"],
                         "basic lambda, jit")
        self.assertEqual(fuzzlib.match_known("diverge: s0/off,s3/default | s3/off first=F#=# vs F#=#",
                                             known)["note"], "s3 interpreter")
        self.assertIsNone(fuzzlib.match_known("diverge: s0/off | s3/jit1 first=F#=# vs F#=#", known))
        self.assertIsNone(fuzzlib.match_known(None, known))

    def test_leg_narrowed_entry_needs_the_leg(self):
        path = self.write({"known": [{"signature": "^compile: s3", "leg": "arm64$", "note": "arm only"}]})
        known = fuzzlib.load_known(path)
        self.assertIsNone(fuzzlib.match_known("compile: s3 access violation", known))
        self.assertIsNone(fuzzlib.match_known("compile: s3 access violation", known, "linux-x64"))
        self.assertIsNotNone(fuzzlib.match_known("compile: s3 access violation", known, "linux-arm64"))

    def test_nightly_only_entries_never_suppress_a_fuzz_finding(self):
        path = self.write({"known": [{"test": "map_insert", "message": "Invalid object cast"},
                                     {"id": "0123456789"}]})
        self.assertIsNone(fuzzlib.match_known("crash: s3/jit1 access violation", fuzzlib.load_known(path)))

    def test_off_schema_and_missing_files_are_errors(self):
        import known_schema
        for bad in ([{"signature": "compile: s3", "note": "x"}],            # bare list
                    {"signatures": [{"signature": "x", "note": "x"}]},        # old triage key
                    {"known": [{"sig": "x"}]}):                               # misspelt matcher
            with self.assertRaises(known_schema.KnownError):
                fuzzlib.load_known(self.write(bad))
        with self.assertRaises(known_schema.KnownError):
            fuzzlib.load_known(os.path.join(tempfile.gettempdir(), "no_such_known.json"))

    def test_repository_known_json_parses(self):
        here = os.path.join(os.path.dirname(os.path.abspath(__file__)), "known.json")
        entries = fuzzlib.load_known(here)
        self.assertTrue(entries)
        for e in entries:
            self.assertIn("note", e["raw"])


if __name__ == "__main__":
    unittest.main()
