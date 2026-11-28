#!/usr/bin/env python3
"""Tests for the two loader-assurance gates in tools/fuzz/adhoc: obe_campaign.py (a crash passes
only by signature, from a checked-in file; the inputs are pinned; fewer mutants than requested
fails) and obe_corpus_gate.py (every source/configuration pair is accounted for; a fresh output
directory; no stale image). No build: the corpus gate runs against a fake obc and obr (POSIX only).

Run:  python3 tools/fuzz/test_obe_gates.py
"""
import json
import os
import shutil
import stat
import subprocess
import sys
import tempfile
import textwrap
import unittest

HERE = os.path.dirname(os.path.abspath(__file__))
ADHOC = os.path.join(HERE, "adhoc")
sys.path.insert(0, ADHOC)
import obe_campaign as camp  # noqa: E402

GATE = os.path.join(ADHOC, "obe_corpus_gate.py")


def rec(file, change, results, frames_by_mode=None):
    r = {"file": file + ".obe", "seed": "s", "change": change, "results": results}
    if frames_by_mode:
        r["frames_by_mode"] = frames_by_mode
        r["frames"] = next(iter(frames_by_mode.values()))
    return r


CRASH = ["crash", -11, ""]
RAN = ["ran", 0, ""]
INPUTS = {"seed": 2, "count": 100, "seeds": 3, "seeds_sha256": "abc", "modes": "default,off,jit1", "structured": 0.7}


def accepted(*entries, inputs=None):
    return {"format": 1, "inputs": inputs or INPUTS, "entries": [dict(e) for e in entries]}


def entry(sig, witnesses=None, classes=(3,), reason="a class of the assessment"):
    e = {"signature": sig, "classes": list(classes), "reason": reason}
    if witnesses is not None:
        e["witnesses"] = witnesses
    return e


class Signature(unittest.TestCase):
    def test_interpreter_frames_name_the_site(self):
        r = rec("a_2_1", "x", {"default": CRASH, "off": CRASH, "jit1": CRASH}, {"off": ["Runtime::LoadClsInstIntVar", "Execute"]})
        self.assertEqual(camp.crash_signature(r), "Runtime::LoadClsInstIntVar")

    def test_fault_in_generated_code(self):
        r = rec("a_2_1", "x", {"default": CRASH, "off": RAN, "jit1": CRASH}, {"default": ["??", "??"]})
        self.assertEqual(camp.crash_signature(r), "(jit code)")

    def test_no_frames_means_no_signature(self):
        r = rec("a_2_1", "x", {"default": CRASH, "off": CRASH, "jit1": CRASH})
        self.assertIsNone(camp.crash_signature(r))

    def test_silent_exit1(self):
        r = rec("a_2_1", "x", {"default": ["silent-exit1", 1, ""], "off": RAN, "jit1": RAN})
        self.assertEqual(camp.crash_signature(r), "silent-exit1")


class AcceptedFile(unittest.TestCase):
    def load(self, data):
        with tempfile.TemporaryDirectory() as tmp:
            p = os.path.join(tmp, "a.json")
            with open(p, "w") as f:
                json.dump(data, f)
            return camp.load_accepted(p)

    def test_complete_file_loads(self):
        self.assertEqual(len(self.load(accepted(entry("A")))["entries"]), 1)

    def test_entry_without_reason_is_refused(self):
        with self.assertRaisesRegex(ValueError, "no reason"):
            self.load(accepted(entry("A", reason="")))

    def test_entry_without_class_is_refused(self):
        with self.assertRaisesRegex(ValueError, "no classes"):
            self.load(accepted(entry("A", classes=())))

    def test_duplicate_signature_is_refused(self):
        with self.assertRaisesRegex(ValueError, "twice"):
            self.load(accepted(entry("A"), entry("A")))

    def test_the_checked_in_file_is_complete(self):
        if os.path.isfile(camp.DEFAULT_ACCEPTED):
            data = camp.load_accepted(camp.DEFAULT_ACCEPTED)
            self.assertTrue(data["entries"])
            for e in data["entries"]:
                self.assertTrue(all(1 <= c <= 8 for c in e["classes"]), e)


class Verdict(unittest.TestCase):
    def crash(self, name, site):
        return rec(name, "change " + name, {"default": CRASH, "off": CRASH, "jit1": CRASH}, {"off": [site]})

    def assess(self, crashes, acc, completed=100, inputs=None, gdb=True):
        return camp.assess(crashes, acc, inputs or INPUTS, 100, completed, gdb)

    def test_known_crash_passes(self):
        rc, lines, _ = self.assess([self.crash("m1", "A")], accepted(entry("A", ["m1"])))
        self.assertEqual(rc, 0, lines)

    def test_unlisted_signature_fails(self):
        rc, lines, rep = self.assess([self.crash("m1", "A"), self.crash("m2", "NEW")], accepted(entry("A", ["m1"])))
        self.assertEqual(rc, camp.RC_CRASH)
        self.assertTrue(any("UNACCEPTED crash signature NEW" in l for l in lines), lines)

    def test_a_new_crash_cannot_replace_a_known_one_at_the_same_site(self):
        # same count (1 crash), same signature, but not the mutant the file was measured with
        rc, lines, _ = self.assess([self.crash("other", "A")], accepted(entry("A", ["m1"])))
        self.assertEqual(rc, camp.RC_CRASH)
        self.assertTrue(any("not one of its witnesses" in l for l in lines), lines)

    def test_other_inputs_are_judged_by_signature_alone(self):
        other = dict(INPUTS, count=5000)
        rc, lines, _ = self.assess([self.crash("other", "A")], accepted(entry("A", ["m1"])), inputs=other)
        self.assertEqual(rc, 0, lines)
        self.assertTrue(any("signature only" in l for l in lines))

    def test_fewer_mutants_than_requested_fails(self):
        rc, lines, rep = self.assess([], accepted(entry("A")), completed=99)
        self.assertEqual(rc, camp.RC_INCOMPLETE)
        self.assertEqual((rep["attempted"], rep["completed"]), (100, 99))

    def test_any_crash_without_an_accepted_file_fails(self):
        rc, lines, _ = self.assess([self.crash("m1", "A")], None)
        self.assertEqual(rc, camp.RC_CRASH)

    def test_no_crash_and_no_file_passes(self):
        rc, lines, _ = self.assess([], None)
        self.assertEqual(rc, 0, lines)

    def test_crash_without_gdb_frames_fails(self):
        r = rec("m1", "x", {"default": CRASH, "off": CRASH, "jit1": CRASH})
        rc, lines, _ = self.assess([r], accepted(entry("A", ["m1"])), gdb=False)
        self.assertEqual(rc, camp.RC_CRASH)
        self.assertTrue(any("could not be established" in l for l in lines))

    def test_accepted_signature_not_seen_is_reported_not_failed(self):
        rc, lines, _ = self.assess([], accepted(entry("A", ["m1"])))
        self.assertEqual(rc, 0)
        self.assertTrue(any("not seen" in l for l in lines))


class SeedListHash(unittest.TestCase):
    class S:
        def __init__(self, name, raw):
            self.name, self.raw = name, raw

    def test_hash_follows_names_and_contents(self):
        a = camp.seed_list_hash([self.S("a", b"1"), self.S("b", b"2")])
        self.assertEqual(a, camp.seed_list_hash([self.S("b", b"2"), self.S("a", b"1")]))
        self.assertNotEqual(a, camp.seed_list_hash([self.S("a", b"1"), self.S("b", b"3")]))
        self.assertNotEqual(a, camp.seed_list_hash([self.S("a", b"1"), self.S("c", b"2")]))
        self.assertNotEqual(a, camp.seed_list_hash([self.S("a", b"1")]))


FAKE_OBC = r'''#!/usr/bin/env python3
import os, sys
a = sys.argv[1:]
src = a[a.index("-src") + 1]
dest = a[a.index("-dest") + 1]
text = open(src, encoding="utf-8", errors="replace").read()
if "FAILME" in text:
    print("%s:(3,5): Undefined class: 'Foo'" % src)
    sys.exit(1)
if "TIMEOUTME" in text:
    import time
    time.sleep(30)
with open(dest, "w") as f:
    f.write(text)
if os.environ.get("OBJECK_STACK_DEPTH_OUT"):
    with open(os.environ["OBJECK_STACK_DEPTH_OUT"], "w") as f:
        f.write("Main" + chr(9) + ("9" if "DEPTHDIFF" in text else "1") + chr(10))
if "SIDEEFFECT" in text:
    open(dest + ".extra.obe", "w").write("x")
'''
FAKE_OBR = r'''#!/usr/bin/env python3
import os, sys
text = open(sys.argv[-1], encoding="utf-8", errors="replace").read()
if os.environ.get("OBJECK_STACK_DEPTH_OUT"):
    with open(os.environ["OBJECK_STACK_DEPTH_OUT"], "w") as f:
        f.write("Main" + chr(9) + "1" + chr(10))
if os.environ.get("OBJECK_VERIFY_ONLY"):
    if "REFUSEME" in text:
        print(">>> Invalid bytecode: M, instruction 3: a number read through as a reference <<<")
        sys.exit(1)
    sys.exit(0)
if "Control" in text:
    print("ran")
'''


@unittest.skipIf(os.name == "nt", "the corpus gate runs on POSIX; the fakes are executable scripts")
class CorpusGate(unittest.TestCase):
    def setUp(self):
        self.tmp = tempfile.mkdtemp(prefix="corpusgate_")
        self.addCleanup(shutil.rmtree, self.tmp, True)
        self.deploy = os.path.join(self.tmp, "deploy")
        os.makedirs(os.path.join(self.deploy, "bin"))
        os.makedirs(os.path.join(self.deploy, "lib"))
        for name, body in (("obc", FAKE_OBC), ("obr", FAKE_OBR)):
            p = os.path.join(self.deploy, "bin", name)
            with open(p, "w") as f:
                f.write(body)
            os.chmod(p, os.stat(p).st_mode | stat.S_IXUSR)
        self.tree = os.path.join(self.tmp, "tree")
        os.makedirs(os.path.join(self.tree, "programs"))
        os.makedirs(os.path.join(self.tree, "tools", "lsp", "server"))
        os.makedirs(os.path.join(self.tree, "tools", "fuzz", "loader"))
        shutil.copy(os.path.join(HERE, "loader", "obe_image.py"), os.path.join(self.tree, "tools", "fuzz", "loader"))
        os.makedirs(os.path.join(self.tree, "core", "shared"))
        shutil.copy(os.path.join(HERE, "..", "..", "core", "shared", "instrs.h"), os.path.join(self.tree, "core", "shared"))
        with open(os.path.join(self.tree, "tools", "lsp", "server", "objk_apis.json"), "w") as f:
            json.dump({"class-bundle-names": {}}, f)
        self.out = os.path.join(self.tmp, "out")
        self.add_source("programs/ok.obs", "class A {}")

    def add_source(self, rel, text):
        p = os.path.join(self.tree, *rel.split("/"))
        os.makedirs(os.path.dirname(p), exist_ok=True)
        with open(p, "w") as f:
            f.write(text)

    def write_json(self, name, entries):
        p = os.path.join(self.tmp, name)
        with open(p, "w") as f:
            json.dump({"format": 1, "entries": entries}, f)
        return p

    def run_gate(self, *extra, expected=None, exclusions=None):
        args = [sys.executable, "-I", GATE, self.tree, self.deploy, self.out, "2",
                "--expected-refusals", expected or self.write_json("er.json", []),
                "--exclusions", exclusions or self.write_json("ex.json", [])] + list(extra)
        r = subprocess.run(args, capture_output=True, text=True, timeout=300)
        return r.returncode, r.stdout + r.stderr

    def test_clean_tree_passes_and_counts_every_pair(self):
        rc, out = self.run_gate()
        self.assertEqual(rc, 0, out)
        self.assertRegex(out, r"pairs attempted \d+ .*UNACCOUNTED 0")
        self.assertIn("verdict: PASS", out)
        ledger = json.load(open(os.path.join(self.out, "ledger.json")))
        self.assertEqual(ledger["attempted"], ledger["accounted"])
        self.assertEqual(ledger["counts"]["loaded"], ledger["attempted"])

    def test_a_depth_the_loader_counts_differently_fails(self):
        self.add_source("programs/deep.obs", "DEPTHDIFF")
        rc, out = self.run_gate()
        self.assertEqual(rc, 1, out)
        self.assertIn("DEPTH DIFFERS", out)
        self.assertIn("verdict: FAIL", out)

    def test_unannounced_compile_refusal_fails(self):
        self.add_source("programs/broken.obs", "FAILME")
        rc, out = self.run_gate()
        self.assertEqual(rc, 1, out)
        self.assertIn("UNACCOUNTED programs/broken.obs", out)
        self.assertIn("verdict: FAIL", out)

    def test_expected_refusal_is_listed_with_its_class_and_passes(self):
        self.add_source("programs/broken.obs", "FAILME")
        listed = self.write_json("er.json", [{"source": "programs/broken.obs", "refusal": "Undefined class: 'X'",
                                              "reason": "a fragment that names a class nothing defines"}])
        rc, out = self.run_gate(expected=listed)
        self.assertEqual(rc, 0, out)
        self.assertIn("expected refusal", out)

    def test_expected_refusal_with_another_class_still_fails(self):
        self.add_source("programs/broken.obs", "FAILME")
        listed = self.write_json("er.json", [{"source": "programs/broken.obs", "refusal": "Something else entirely",
                                              "reason": "wrong class"}])
        rc, out = self.run_gate(expected=listed)
        self.assertEqual(rc, 1, out)

    def test_named_exclusion_passes(self):
        self.add_source("programs/skipme.obs", "FAILME")
        ex = self.write_json("ex.json", [{"source": "programs/skipme.obs", "reason": "needs hardware"}])
        rc, out = self.run_gate(exclusions=ex)
        self.assertEqual(rc, 0, out)
        self.assertRegex(out, r"excluded [1-9]")

    def test_entry_without_a_reason_is_refused(self):
        bad = self.write_json("ex.json", [{"source": "programs/ok.obs", "reason": ""}])
        rc, out = self.run_gate(exclusions=bad)
        self.assertNotEqual(rc, 0)
        self.assertIn("no reason", out)

    def test_loader_refusal_fails(self):
        self.add_source("programs/refused.obs", "REFUSEME")
        rc, out = self.run_gate()
        self.assertEqual(rc, 1, out)
        self.assertIn("REFUSED", out)

    def test_exit_zero_without_an_image_is_unaccounted(self):
        # the fake sleeps 30 s; the gate's 300 s cap is not reached here, so use a source the compiler
        # exits 0 for without writing an image instead
        self.add_source("programs/noimage.obs", "NOIMAGE")
        with open(os.path.join(self.deploy, "bin", "obc"), "a") as f:
            f.write('\nif "NOIMAGE" in text:\n    os.remove(dest)\n')
        rc, out = self.run_gate()
        self.assertEqual(rc, 1, out)
        self.assertIn("exited 0 and wrote no image", out)

    def test_an_image_this_run_did_not_account_for_fails(self):
        self.add_source("programs/side.obs", "SIDEEFFECT")
        rc, out = self.run_gate()
        self.assertEqual(rc, 1, out)
        self.assertIn("STALE images", out)

    def test_old_images_in_the_output_directory_are_deleted_not_loaded(self):
        os.makedirs(os.path.join(self.out, "img"))
        with open(os.path.join(self.out, ".obe_corpus_gate"), "w") as f:
            f.write("marker")
        stale = os.path.join(self.out, "img", "00000__s9.obe")
        with open(stale, "w") as f:
            f.write("REFUSEME")  # would be refused if an earlier run's image were loaded
        rc, out = self.run_gate()
        self.assertEqual(rc, 0, out)
        self.assertFalse(os.path.exists(stale))

    def test_a_directory_that_is_not_the_gates_is_not_deleted(self):
        os.makedirs(self.out)
        keep = os.path.join(self.out, "precious.txt")
        with open(keep, "w") as f:
            f.write("x")
        rc, out = self.run_gate()
        self.assertNotEqual(rc, 0)
        self.assertIn("refusing to delete", out)
        self.assertTrue(os.path.exists(keep))

    def test_no_sources_is_not_a_pass(self):
        os.remove(os.path.join(self.tree, "programs", "ok.obs"))
        rc, out = self.run_gate()
        self.assertEqual(rc, 2, out)
        self.assertIn("nothing to gate", out)


if __name__ == "__main__":
    unittest.main()
