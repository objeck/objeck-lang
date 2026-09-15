"""Tests for emi.py, the EMI (equivalence modulo inputs) mutator.

The analysis and variant tests need no build. The toolchain tests use the
deploy tree test_toolchain.find_bin() picks (FUZZ_BIN first) and FAIL without
one, as test_toolchain.py does:

  * variants of real regression tests compile at s0 and s3 and agree;
  * a synthetic miscompile (faults/fault_obc_emi.py) is reported as a
    divergence and exits 1, both when only s3 takes the dead guard and when
    every opt level does (a uniform change is a finding, not "semantic");
    classify() spots both in synthetic runs;
  * the guard is not constant-folded: the s3 bytecode still holds every dead
    block's Trip mark, and the same s3 build takes the guard at run time when
    given more than 4096 arguments.
"""

import json
import os
import re
import sys
import tempfile
import unittest
from unittest import mock

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
import emi      # noqa: E402
import fuzzlib  # noqa: E402
from test_toolchain import find_bin  # noqa: E402

EXE = ".exe" if os.name == "nt" else ""

SAMPLE = """#~
# a comment with { braces } and 12345
~#
class Sample {
  @field : static : Int;

  function : Twice(x : Int) ~ Int {
    if(x > 100) {
      return x * 2;
    };
    return x + x;
  }

  function : Main(args : String[]) ~ Nil {
    a := 70;
    f := 2.5;
    s := "text {$a} ; 99 }";
    c := '}';
    total := 0;
    for(i := 0; i < 10; i += 1) {
      total += Twice(i) + a;
    };
    arr := [1, 2, 3];
    select(a) {
      label 70: {
        total += 1;
      }
      other: {
        total -= 1;
      }
    };
    if(total < 0) {
      "never"->PrintLine();
      Runtime->Exit(1);
    } else {
      total->PrintLine();
    };
    f->PrintLine();
  }
}
"""


class AnalysisTest(unittest.TestCase):
    def setUp(self):
        self.a = emi.Analysis(SAMPLE)

    def test_tokenizer_skips_comments_strings_and_chars(self):
        texts = [t.text for t in emi.tokenize(SAMPLE)]
        self.assertNotIn("12345", texts)
        self.assertIn('"text {$a} ; 99 }"', texts)
        self.assertIn("'}'", texts)

    def test_literal_sites_skip_array_literals_and_labels(self):
        lits = [SAMPLE[s:e] for s, e, _ in self.a.literals]
        for s, e, _ in self.a.literals:
            line = SAMPLE[SAMPLE.rfind("\n", 0, s) + 1:SAMPLE.find("\n", s)]
            self.assertNotIn("[1, 2, 3]", line)
            self.assertNotIn("label", line)
        self.assertIn("70", lits)
        self.assertIn("2.5", lits)

    def test_main_and_int_locals_found(self):
        self.assertEqual([args for _, args in self.a.mains], ["args"])
        reads = {SAMPLE[s:e] for s, e, _ in self.a.var_reads}
        self.assertIn("a", reads)
        self.assertIn("i", reads)
        self.assertNotIn("x", reads)  # a parameter: its type is not inferred

    def test_no_boundary_after_return(self):
        for b in self.a.boundaries:
            before = SAMPLE[:b.pos].rstrip()
            self.assertFalse(re.search(r"return [^;]*;$", before), before[-40:])

    def test_no_literal_sites_after_return_unary_or_array(self):
        text = SAMPLE.replace("return x + x;", "if(x < 0) { return -5; };\n    return x + x;")
        text = text.replace('"never"->PrintLine();', 'q := Pair(); "never"->PrintLine();')
        text = text.replace("  function : Main(", "  function : Pair() ~ Int[] {\n    return [8, 9];\n  }\n\n"
                                                  "  function : Main(")
        a = emi.Analysis(text)
        lits = [text[s:e] for s, e, _ in a.literals]
        self.assertNotIn("5", lits)
        self.assertNotIn("8", lits)
        self.assertNotIn("9", lits)

    def test_name_exclusions_are_whole_components(self):
        for name in ("runtime_feature_test", "runtime_gc_stats", "core_processor_ops"):
            self.assertIsNone(emi.NAME_EXCLUDE.search(name), name)
        for name in ("http_error_body", "https_persistence_test", "core_net_buffer", "socket_graceful_close_test",
                     "api_openai_test", "thread_accept_exit_test", "regex_bench", "gl_context_test"):
            self.assertIsNotNone(emi.NAME_EXCLUDE.search(name), name)

    def test_blocks_found(self):
        ctrls = sorted(b.ctrl for b in self.a.blocks)
        self.assertEqual(ctrls, ["else", "for", "if", "if", "label", "label"])


class VariantTest(unittest.TestCase):
    def setUp(self):
        self.a = emi.Analysis(SAMPLE)

    def test_variant_determinism(self):
        for index in range(20):
            v1 = emi.make_variant(self.a, 7, "sample", index, hits={0, 1, 2})
            v2 = emi.make_variant(emi.Analysis(SAMPLE), 7, "sample", index, hits={0, 1, 2})
            self.assertEqual(v1.text, v2.text)
        texts = {emi.make_variant(self.a, 7, "sample", i).text for i in range(20)}
        self.assertGreater(len(texts), 15)
        self.assertNotEqual(emi.make_variant(self.a, 7, "sample", 0).text,
                            emi.make_variant(self.a, 8, "sample", 0).text)

    def test_every_variant_carries_the_guard_and_mutations(self):
        for index in range(20):
            v = emi.make_variant(self.a, 1, "sample", index)
            self.assertIn("class EmiGuardZq", v.text)
            self.assertIn("EmiGuardZq->Init(args);", v.text)
            self.assertTrue(v.mutations)
            # mutations and the guard class are brace-balanced; count brace
            # tokens, since a cloned statement can copy braces inside a string
            braces = [t.text for t in emi.tokenize(v.text) if t.kind == "punct" and t.text in "{}"]
            self.assertEqual(braces.count("{"), braces.count("}"))

    def test_deletion_only_empties_unhit_blocks(self):
        # probe hit everything but the if(total < 0) block
        never = [b.id for b in self.a.blocks if b.ctrl == "if" and "never" in SAMPLE[b.open_end:b.close_start]]
        hits = {b.id for b in self.a.blocks} - set(never)
        self.assertEqual([b.id for b in emi.unhit_blocks(self.a, hits)], never)
        deleted = 0
        for index in range(30):
            v = emi.make_variant(self.a, 3, "sample", index, hits=hits)
            if any(m.kind == "delete" for m in v.mutations):
                deleted += 1
                self.assertNotIn('"never"->PrintLine()', v.text)
        self.assertGreater(deleted, 0)

    def test_probe_text_numbers_every_block(self):
        text = emi.probe_text(self.a)
        ids = sorted(int(k) for k in re.findall(r"EmiGuardZq->Probe\((\d+)\);", text))
        self.assertEqual(ids, list(range(len(self.a.blocks))))
        self.assertEqual(emi.parse_probe_hits("@@EMI-PROBE 3\nx\n@@EMI-PROBE 0\n"), {0, 3})

    def test_mutation_line_spans(self):
        v = emi.make_variant(self.a, 5, "sample", 1)
        text, spans = v.build()
        lines = text.split("\n")
        for m, (first, last) in zip(v.mutations, spans):
            if m.kind == "delete":
                continue
            chunk = "\n".join(lines[first - 1:last])
            self.assertIn("EmiGuardZq", chunk)


def _res(stdout, code=0, timed_out=False):
    return fuzzlib.Result(code, stdout, "", 0.1, timed_out=timed_out)


class ClassifyTest(unittest.TestCase):
    def test_synthetic_miscompile_is_a_divergence(self):
        ref = _res("PASS\n")
        results = {"s0/off": _res("PASS\n"), "s3/off": _res("EMI-DEAD 910001\n", 97),
                   "s0/jit1": _res("PASS\n"), "s3/jit1": _res("EMI-DEAD 910001\n", 97)}
        status, sig = emi.classify(ref, results)
        self.assertEqual(status, "diverge")
        self.assertIn("ref,s0/off,s0/jit1 | s3/off,s3/jit1", sig)

    def test_agreement_passes(self):
        ref = _res("PASS\n")
        same = {n: _res("PASS\n") for n in emi.CONFIG_NAMES}
        self.assertEqual(emi.classify(ref, same), ("pass", None))

    def test_uniform_change_is_a_divergence(self):
        # every configuration agrees, but not with the original: an emitter
        # miscompile looks exactly like this, so it must be a finding
        ref = _res("PASS\n")
        changed = {n: _res("other\n") for n in emi.CONFIG_NAMES}
        status, sig = emi.classify(ref, changed)
        self.assertEqual(status, "diverge")
        self.assertTrue(sig.startswith("uniform: output changed; diverge: ref | s0/off,s3/off,s0/jit1,s3/jit1"), sig)
        dead = {n: _res("PASS\nEMI-DEAD 910004\n", 97) for n in emi.CONFIG_NAMES}
        status, sig = emi.classify(ref, dead)
        self.assertEqual(status, "diverge")
        self.assertTrue(sig.startswith("uniform: dead guard taken; "), sig)
        # the non-crashing Nil-dereference shape: exit 1, same stdout everywhere
        nil = {n: _res("PASS\n", 1) for n in emi.CONFIG_NAMES}
        status, sig = emi.classify(ref, nil)
        self.assertEqual(status, "diverge")
        self.assertTrue(sig.startswith("uniform: exit changed; "), sig)

    def test_crash_everywhere_is_still_a_divergence(self):
        ref = _res("PASS\n")
        crashed = {n: _res("", 3221225477) for n in emi.CONFIG_NAMES}
        self.assertEqual(emi.classify(ref, crashed)[0], "diverge")


BIN = find_bin()
TOOL_TESTS = ["core_arithmetic", "opt_int_division", "core_classes"]
PROBE_MARK = 910999


class ToolchainEmiTest(unittest.TestCase):
    def setUp(self):
        self.assertIsNotNone(BIN, "no Objeck deploy tree found; set FUZZ_BIN to its bin directory")

    def run_emi(self, extra, tests, variants):
        out = tempfile.mkdtemp(prefix="emi_test_")
        summary = os.path.join(out, "summary.json")
        status = emi.main(["--bin", BIN, "--tests", ",".join(tests), "--variants", str(variants),
                           "--seed", "11", "-j", "3", "--out", out, "--json", summary] + extra)
        with open(summary) as f:
            return status, json.load(f)

    def test_variants_compile_and_agree(self):
        status, s = self.run_emi([], TOOL_TESTS, 6)
        self.assertEqual(s["ran"], len(TOOL_TESTS), [r["notes"] for r in s["tests"]])
        st = s["stats"]
        self.assertEqual((status, st["diverge"], st["flaky"]), (0, 0, 0), s["signatures"])
        # nearly every variant survives its s0 compile (after repair) and matches
        self.assertGreaterEqual(st["pass"], int(0.8 * st["variants"]), st)

    def run_fault(self, opts, variants=4):
        env = {"FUZZ_REAL_OBC": os.path.join(BIN, "obc" + EXE), "FUZZ_FAULT_OPTS": opts}
        fault = os.path.join(HERE, "faults", "fault_obc_emi.py")
        with mock.patch.dict(os.environ, env):
            return self.run_emi(["--obc", fault, "--no-delete"], ["core_arithmetic"], variants)

    def test_synthetic_miscompile_is_caught(self):
        status, s = self.run_fault("s3")
        self.assertEqual(status, 1)
        self.assertGreater(s["stats"]["diverge"], 0)
        self.assertEqual(s["stats"]["uniform"], 0)
        sigs = list(s["signatures"])
        self.assertTrue(all(sig.startswith("diverge: ref,s0/off,s0/jit1 | s3/off,s3/jit1 ") for sig in sigs), sigs)
        # the reducer keeps a single dead block, the mutation the fault needs
        finding = s["tests"][0]["findings"][0]
        self.assertEqual([m["kind"] for m in finding["mutations"]], ["dead"])
        self.assertTrue(os.path.exists(os.path.join(finding["path"], "reduced.obs")))

    def test_uniform_miscompile_is_caught(self):
        # every opt level takes the dead guard: all four configurations agree
        # with each other and not with the original
        status, s = self.run_fault("s0,s3")
        self.assertEqual(status, 1, s["stats"])
        self.assertGreater(s["stats"]["diverge"], 0)
        self.assertEqual(s["stats"]["uniform"], s["stats"]["diverge"])
        sigs = list(s["signatures"])
        # every finding is uniform; most take a dead block (EMI-DEAD, exit 97),
        # while a `(c) & Live()` condition made false can fail without one
        self.assertTrue(all(sig.startswith("uniform: ") and "ref | s0/off,s3/off,s0/jit1,s3/jit1" in sig
                            for sig in sigs), sigs)
        self.assertTrue(any(sig.startswith("uniform: dead guard taken; ") for sig in sigs), sigs)
        for finding in s["tests"][0]["findings"]:
            if finding["signature"].startswith("uniform: dead guard taken; "):
                self.assertEqual([m["kind"] for m in finding["mutations"]], ["dead"])

    def test_guard_is_not_constant_folded(self):
        tools = emi.Tools(BIN, timeout=60)
        with open(os.path.join(emi.REG_DIR, "core_arithmetic.obs"), encoding="utf-8") as f:
            text = f.read().lstrip("﻿").replace("\r\n", "\n")
        a = emi.Analysis(text)
        work = tempfile.mkdtemp(prefix="emi_fold_")
        checked = tripped = 0
        for index in range(6):
            v = emi.make_variant(a, 21, "core_arithmetic", index)
            marks = sorted({int(m) for m in re.findall(r"EmiGuardZq->Trip\((\d+)\)", v.text)})
            if not marks:
                continue
            # plus one dead block right after Init, on the path every run takes
            probe = "if(%s->Dead()) { %s->Trip(%d); };" % (emi.GUARD, emi.GUARD, PROBE_MARK)
            text = v.text.replace("%s->Init(args);" % emi.GUARD, "%s->Init(args); %s" % (emi.GUARD, probe), 1)
            self.assertIn(probe, text)
            marks.append(PROBE_MARK)
            src = os.path.join(work, "v%d.obs" % index)
            with open(src, "w", encoding="utf-8", newline="\n") as f:
                f.write(text)
            dest = os.path.join(work, "v%d.obe" % index)
            r, wrote = tools.compile(src, "s3", dest, "cipher,collect,xml,json", asm=True)
            if not wrote:
                continue
            with open(os.path.splitext(dest)[0] + ".obm", encoding="utf-8", errors="replace") as f:
                asm = f.read()
            main = asm[asm.find("name='CoreArithmeticTest:Main:"):]
            main = main[:main.find("\nMethod:", 1)] if "\nMethod:" in main else main
            for mark in marks:
                self.assertIn("LOAD_INT_LIT: value=%d" % mark, main, "dead block %d folded away" % mark)
            # the guard is live at run time: the same s3 build takes the dead
            # block after Init only when given more than 4096 arguments. A
            # guard the compiler had folded could not react to arguments.
            for flags in (["--jit=off"], ["--jit=1"]):
                quiet = tools.run(dest, flags, work, 60)
                self.assertEqual(quiet.code, 0, quiet.stdout)
                self.assertNotIn("EMI-DEAD", quiet.stdout)
                r = tools.run(dest, flags, work, 60, args=["x"] * 4097)
                self.assertEqual(r.code, emi.TRIP_EXIT, "v%d %s ignored its runtime guard: %s" %
                                 (index, flags, r.stdout[-200:]))
                self.assertIn("EMI-DEAD %d" % PROBE_MARK, r.stdout)
                tripped += 1
            checked += 1
        self.assertGreater(checked, 2)
        self.assertEqual(tripped, 2 * checked)


if __name__ == "__main__":
    unittest.main()
