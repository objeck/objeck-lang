"""Unit tests for the choice-sequence reducer, with synthetic interestingness
predicates (no toolchain needed)."""

import os
import random
import sys
import unittest

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import gen     # noqa: E402
import reduce  # noqa: E402


class ShrinkTest(unittest.TestCase):
    def test_shrinks_to_the_needed_values(self):
        rng = random.Random(1)
        seq = [rng.randrange(100) for _ in range(300)]
        seq[123] = 77
        seq[250] = 42

        def test(c):
            return c if (77 in c and 42 in c and c.index(77) < c.index(42)) else None

        best, tests = reduce.shrink(seq, test)
        self.assertEqual(best, [77, 42])
        self.assertLess(tests, 2000)

    def test_lowers_values(self):
        best, _ = reduce.shrink([90, 90, 90], lambda c: c if sum(c) >= 10 else None)
        self.assertEqual(sum(best), 10)
        self.assertEqual(len(best), 1)

    def test_never_returns_uninteresting_and_respects_budget(self):
        seq = list(range(1, 200))
        best, tests = reduce.shrink(seq, lambda c: c if len(c) > 150 else None, max_tests=25)
        self.assertLessEqual(tests, 25)
        self.assertGreater(len(best), 150)

    def test_uses_normalized_choices_from_the_test(self):
        # the test reports the (shorter) sequence it consumed, which becomes the
        # result; values below the floor of 5 are never interesting
        seen = []

        def test(c):
            seen.append(list(c))
            return [max(c)] if c and max(c) >= 5 else None

        best, _ = reduce.shrink([5, 6, 7, 8], test)
        self.assertEqual(best, [5])
        self.assertIn([5, 6, 7, 8][:2], seen)  # truncation was tried first

    def test_program_level_predicate_keeps_programs_valid(self):
        """Reduce a real generated program to the smallest one that still has a
        select with a negative label -- a stand-in for 'still miscompiles'."""
        orig = None
        for seed in range(200):
            p = gen.generate(seed=seed, features=["F4"])
            if "label -" in p.text:
                orig = p
                break
        self.assertIsNotNone(orig)

        def test(c):
            p = gen.generate(choices=c, features=["F4"])
            return p.choices if "label -" in p.text else None

        best, tests = reduce.shrink(orig.choices, test)
        small = gen.generate(choices=best, features=["F4"])
        self.assertIn("label -", small.text)
        self.assertLess(len(best), len(orig.choices))
        self.assertLess(len(small.text), len(orig.text))
        self.assertIn("function : Main", small.text)


if __name__ == "__main__":
    unittest.main()
