import os
import sys
import unittest

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import gen_roots  # noqa: E402
import run_roots  # noqa: E402


class GeneratorTests(unittest.TestCase):
    def test_same_seed_same_program(self):
        for seed in (1, 2, 3, 99):
            self.assertEqual(gen_roots.generate(seed)[0], gen_roots.generate(seed)[0])

    def test_seeds_differ(self):
        self.assertNotEqual(gen_roots.generate(1)[0], gen_roots.generate(2)[0])

    def test_addr_switch(self):
        on = gen_roots.generate(7, addr="on")
        off = gen_roots.generate(7, addr="off")
        self.assertNotIn("TestAddressOf", off[0])
        self.assertFalse(off[1]["address_int"])
        # auto: seeds divisible by 3 only
        self.assertFalse(gen_roots.generate(7)[1]["address_int"])
        self.assertIn("scenarios", on[1])

    def test_program_shape(self):
        text, meta = gen_roots.generate(5)
        self.assertIn("class RcMain", text)
        self.assertIn("function : Main(args : String[])", text)
        self.assertIn("total=", text)
        self.assertGreaterEqual(meta["scenarios"], 4)
        # braces balance, so the reducer's chunking and the compiler both see whole blocks
        self.assertEqual(text.count("{"), text.count("}"))

    def test_scale_shrinks(self):
        big = gen_roots.generate(11, scale=1.0)[0]
        small = gen_roots.generate(11, scale=0.3)[0]
        self.assertLess(len(small), len(big))


class RunnerTests(unittest.TestCase):
    def test_audit_counts(self):
        err = ("[gc-roots] collections=3 threads=1 mismatched_threads=0 frames: interpreted=1 compiled=0 maps: built=2 failed=0\n"
               "[gc-roots] disagreements: int_points_at_object=2 ref_points_at_nothing=0 unknown_points_at_object=0 "
               "jit_value_slot_at_object=1\n[gc-roots]   2x a note\n")
        res = run_roots.Result(0, "", err, 0.1)
        self.assertEqual(run_roots.audit_counts(err)["collections"], 3)
        self.assertEqual(run_roots.audit_problems(res, False), ["int_points_at_object=2", "jit_value_slot_at_object=1"])
        self.assertEqual(run_roots.audit_problems(res, True), [])
        self.assertTrue(run_roots.audit_problems(run_roots.Result(0, "", "", 0.1), False))

    def test_first_diff(self):
        self.assertEqual(run_roots.first_diff("a\nb\n", "a\nc\n"), "line 2: expected 'b', got 'c'")

    def test_chunks_are_whole_blocks(self):
        lines = ["class A {", "  f() {", "    x := 1;", "    if(x) {", "      y := 2;", "    };", "  }", "}"]
        spans = run_roots.chunks(lines)
        self.assertIn((0, 7), spans)
        self.assertIn((3, 5), spans)
        self.assertIn((2, 2), spans)


if __name__ == "__main__":
    unittest.main()
