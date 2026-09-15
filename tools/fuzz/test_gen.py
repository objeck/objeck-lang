"""Unit tests for the program generator (no toolchain needed).

    python -m unittest discover -s tools/fuzz -p "test_*.py"
"""

import os
import random
import re
import sys
import unittest

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import gen  # noqa: E402


class ChoicesTest(unittest.TestCase):
    def test_seeded_draws_are_recorded_and_bounded(self):
        c = gen.Choices(seed=7)
        vals = [c.draw(n) for n in (1, 2, 10, 100, 3)]
        self.assertEqual(vals, c.record)
        for v, n in zip(vals, (1, 2, 10, 100, 3)):
            self.assertTrue(0 <= v < max(1, n))

    def test_replay_reduces_modulo_and_zero_fills(self):
        c = gen.Choices(replay=[13, 5])
        self.assertEqual(c.draw(10), 3)
        self.assertEqual(c.draw(10), 5)
        self.assertEqual(c.draw(10), 0)      # past the end: simplest option
        self.assertFalse(c.chance(99))       # 0 is "no"
        self.assertEqual(c.record, [3, 5, 0, 0])


class DeterminismTest(unittest.TestCase):
    def test_same_seed_same_program(self):
        for seed in (1, 42, 9001):
            a = gen.generate(seed=seed)
            b = gen.generate(seed=seed)
            self.assertEqual(a.text, b.text)
            self.assertEqual(a.choices, b.choices)
            self.assertEqual(a.methods, b.methods)

    def test_different_seeds_differ(self):
        texts = {gen.generate(seed=s).text for s in range(20)}
        self.assertGreater(len(texts), 15)

    def test_replay_of_recorded_choices_is_identical(self):
        for seed in range(10):
            a = gen.generate(seed=seed)
            b = gen.generate(choices=a.choices)
            # the header names the seed; everything after it must match
            strip = lambda t: t.split("~#", 1)[1]
            self.assertEqual(strip(a.text), strip(b.text))
            self.assertEqual(a.choices, b.choices)

    def test_draw_order_is_pinned(self):
        """Findings are stored as choice sequences, so a generator change that
        alters which draws a program makes silently turns every saved finding
        into a different program (it happened once: an unconditional draw added
        for the basic-lambda knob). Changing these hashes must be deliberate,
        and saved findings must be re-recorded when they change."""
        import hashlib
        golden = {
            1: ("68fb2b29b1370fb0f45730e69cb5a520793735e3", 2925),
            140: ("519737b86ea7261e1b230b2b8817a3d775e1c7da", 1671),
            231: ("e00bef58671a14206eef5404246d11d7db01ad8a", 266),
            7777: ("367e48e12bab255422d0d4831ffbd08da8fc4375", 1533),
        }
        for seed, (digest, ndraws) in golden.items():
            p = gen.generate(seed=seed)
            self.assertEqual((hashlib.sha1(p.text.encode()).hexdigest(), len(p.choices)), (digest, ndraws), seed)

    def test_knob_replay_uses_the_recorded_setting(self):
        # a program recorded with the basic-lambda knob replays exactly with it
        for seed in range(30):
            a = gen.generate(seed=seed, basic_lambdas=True)
            b = gen.generate(choices=a.choices, basic_lambdas=True)
            self.assertEqual(a.text.split("~#", 1)[1], b.text.split("~#", 1)[1])
        # and without F5 the knob changes nothing at all
        a = gen.generate(seed=140, features=["F2"], basic_lambdas=True)
        b = gen.generate(seed=140, features=["F2"], basic_lambdas=False)
        self.assertEqual(a.text, b.text)

    def test_forced_features_stay_aligned_on_replay(self):
        a = gen.generate(seed=5, features=["F2", "F6"])
        self.assertEqual(a.features, ["F1", "F2", "F6"])
        b = gen.generate(choices=a.choices, features=["F2", "F6"])
        self.assertEqual(a.text.split("~#", 1)[1], b.text.split("~#", 1)[1])


class ShapeTest(unittest.TestCase):
    """Structural promises the generated text keeps for every choice stream."""

    def programs(self):
        rng = random.Random(3)
        for seed in range(40):
            yield gen.generate(seed=seed)
        # arbitrary and truncated streams, as the reducer produces
        for _ in range(40):
            yield gen.generate(choices=[rng.randrange(1000) for _ in range(rng.randrange(0, 400))])
        yield gen.generate(choices=[])

    def test_every_stream_yields_a_complete_program(self):
        for p in self.programs():
            self.assertIn("class FuzzProgram {", p.text)
            self.assertIn("function : Main(args : String[]) ~ Nil {", p.text)
            self.assertEqual(p.text.count("{"), p.text.count("}"), p.text)
            nfuncs = len(re.findall(r"function : F\d+\(", p.text))
            self.assertGreaterEqual(nfuncs, 1)
            self.assertEqual(len(re.findall(r'"F\d+="->Print\(\)', p.text)), nfuncs)
            self.assertIn("r < %d" % gen.CALLS, p.text)
            self.assertGreaterEqual(gen.CALLS, 10)

    def test_no_nondeterministic_or_lambda_parameter_forms(self):
        for p in self.programs():
            for bad in ("Time", "Random", "args[", "\\(Int", "\\(x", "\\(a"):
                self.assertNotIn(bad, p.text)

    def test_local_slot_budget(self):
        for p in self.programs():
            for body in re.split(r"\n  function : ", p.text)[1:]:
                header, _, rest = body.partition("\n")
                names = set(re.findall(r"^\s*(\w+)\s*(?::=|: \w)", rest, re.M))
                names |= set(re.findall(r"(\w+) : (?:Int|FzNode|FzTree)(?=[,)])", header))
                self.assertLessEqual(len(names), 70, header)

    def test_shift_counts_and_divisors_are_safe(self):
        for p in self.programs():
            for count in re.findall(r"<< (\d+)\)", p.text):
                self.assertLessEqual(int(count), 20)
            for count in re.findall(r">> (\d+)\)", p.text):
                self.assertLessEqual(int(count), 63)
            self.assertNotRegex(p.text, r"[/%] 0\)")
            self.assertNotRegex(p.text, r"[/%] \(0 - 0\)")

    def test_features_gate_their_constructs(self):
        p = gen.generate(seed=11, features=[])
        self.assertEqual(p.features, ["F1"])
        for marker in ("Float", "select(", "FuncRef", "FzShape", "FzNode", '"ab"'):
            self.assertNotIn(marker, p.text.split("~#", 1)[1])

    def test_swarm_samples_varied_subsets(self):
        subsets = {tuple(gen.generate(seed=s).features) for s in range(60)}
        self.assertGreater(len(subsets), 12)

    def test_methods_list_names_generated_methods(self):
        p = gen.generate(seed=2, features=["F2", "F3", "F4", "F5", "F6"])
        self.assertTrue(p.methods)
        for m in p.methods:
            cls, name = m.split(":")
            self.assertRegex(p.text, r"class %s\b" % cls)
            self.assertRegex(p.text, r"(function|method) : (public : |virtual : public : )?%s\(" % name)


if __name__ == "__main__":
    unittest.main()
