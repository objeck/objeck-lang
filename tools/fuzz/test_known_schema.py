"""Unit tests for known_schema.py, the known.json schema shared by run_fuzz.py
and tools/cicd/nightly_triage.py (no toolchain needed)."""

import json
import os
import sys
import tempfile
import unittest

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
import known_schema as ks  # noqa: E402


class ParseTest(unittest.TestCase):
    def ok(self, data):
        return ks.parse(data)

    def bad(self, data, needle):
        with self.assertRaises(ks.KnownError) as cm:
            ks.parse(data)
        self.assertIn(needle, str(cm.exception))

    def test_canonical_form(self):
        entries = self.ok({"schema": 1, "known": [{"signature": "^crash", "note": "n"},
                                                  {"leg": "arm64$", "message": "cast", "issue": "#816"},
                                                  {"id": "abc1234567"}]})
        self.assertEqual(len(entries), 3)
        self.assertEqual(entries[0]["raw"]["note"], "n")

    def test_schema_is_optional_but_checked(self):
        self.ok({"known": []})
        self.bad({"schema": 2, "known": []}, "schema 2")

    def test_other_spellings_are_rejected(self):
        self.bad([], "top level must be an object")
        self.bad({"signatures": []}, "unknown top-level key(s) signatures")
        self.bad({"known": {}}, "must be a list")

    def test_entries_are_checked(self):
        self.bad({"known": ["x"]}, "is not an object")
        self.bad({"known": [{"note": "only a note"}]}, "has no matcher")
        self.bad({"known": [{"sig": "x", "note": "n"}]}, "unknown key(s) sig")
        self.bad({"known": [{"message": "("}]}, "bad message regex")
        self.bad({"known": [{"signature": "x"}]}, "needs a note")
        self.bad({"known": [{"signature": "x", "note": "n", "message": "m"}]},
                 "narrowed only by leg and step, not message")
        self.bad({"known": [{"test": 3}]}, "test must be a string")

    def test_load_reads_a_bom_and_reports_missing_files(self):
        d = tempfile.mkdtemp()
        p = os.path.join(d, "known.json")
        with open(p, "wb") as f:
            f.write(b'\xef\xbb\xbf{"known": []}')
        self.assertEqual(ks.load(p), [])
        with self.assertRaises(ks.KnownError):
            ks.load(os.path.join(d, "missing.json"))
        with open(p, "w") as f:
            f.write("{nope")
        with self.assertRaises(ks.KnownError):
            ks.load(p)

    def test_repository_file_follows_the_schema(self):
        self.assertTrue(ks.load(os.path.join(HERE, "known.json")))


class MatchTest(unittest.TestCase):
    def test_signature_entry_matches_only_fuzz_signatures(self):
        e = ks.parse({"known": [{"signature": r"\| s3/off first=", "note": "n"}]})
        self.assertIsNotNone(ks.match(e, {"signature": "diverge: s0/off | s3/off first=F#=#"}))
        self.assertIsNone(ks.match(e, {"signature": None, "message": "| s3/off first="}))
        self.assertIsNone(ks.match(e, {"message": "| s3/off first="}))

    def test_leg_and_step_narrow_a_signature(self):
        e = ks.parse({"known": [{"signature": "^crash", "leg": "arm64$", "step": "^fuzz$", "note": "n"}]})
        self.assertIsNone(ks.match(e, {"signature": "crash: x", "step": "fuzz"}))            # no leg
        self.assertIsNone(ks.match(e, {"signature": "crash: x", "leg": "linux-x64", "step": "fuzz"}))
        self.assertIsNotNone(ks.match(e, {"signature": "crash: x", "leg": "linux-arm64", "step": "fuzz"}))

    def test_context_entry_needs_every_field(self):
        e = ks.parse({"known": [{"leg": "arm64$", "config": "jit=1", "message": "Invalid object cast"}]})
        item = {"leg": "linux-arm64", "config": "jit=1", "message": "exit code 1",
                "detail": "Invalid object cast: '>>>'"}
        self.assertIsNotNone(ks.match(e, item))
        self.assertIsNone(ks.match(e, dict(item, leg="linux-x64")))

    def test_id_alone_or_with_context(self):
        e = ks.parse({"known": [{"id": "abc"}]})
        self.assertIsNotNone(ks.match(e, {"id": "abc"}))
        self.assertIsNone(ks.match(e, {"id": "abd", "message": "anything"}))
        # an issue-body snippet: id plus context; either the id or all the context matches
        e = ks.parse({"known": [{"id": "abc", "leg": "^x$", "message": "boom"}]})
        self.assertIsNotNone(ks.match(e, {"id": "zzz", "leg": "x", "message": "boom"}))
        self.assertIsNone(ks.match(e, {"id": "zzz", "leg": "y", "message": "boom"}))


class FuzzLineTest(unittest.TestCase):
    SIG = "diverge: s0/off,s3/off | s3/jit1 first=F#=# vs F#=# exit 1 >>> x (y) <<< @Main"

    def test_line_round_trips(self):
        line = ks.fuzz_failure_line(self.SIG, 20260915)
        self.assertTrue(line.startswith("FAIL fuzz: "))
        message = line[len("FAIL "):]
        self.assertEqual(ks.fuzz_signature("fuzz", message), self.SIG)

    def test_not_a_fuzz_failure(self):
        self.assertIsNone(ks.fuzz_signature("(step)", "fuzz: x (seed 1)"))
        self.assertIsNone(ks.fuzz_signature("fuzz", "exit code 1"))


if __name__ == "__main__":
    unittest.main()
