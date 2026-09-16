#!/usr/bin/env python3
"""Unit tests for check_benchmark_inputs.py.

Run: python -m unittest tools.cicd.test_check_benchmark_inputs
  or: python tools/cicd/test_check_benchmark_inputs.py
"""

import io
import os
import sys
import tempfile
import unittest
from contextlib import redirect_stdout

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))

import check_benchmark_inputs as lint  # noqa: E402

REPO_ROOT = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))

SCRIPT = """#!/bin/bash
declare -A CLBG_BENCHMARKS
CLBG_BENCHMARKS[nbody]="50000000"
CLBG_BENCHMARKS[binarytrees]="17"
CLBG_BENCHMARKS[mandelbrot]="4000"

declare -A PERF_BENCHMARKS
PERF_BENCHMARKS[bench_matrix_multiply]="500"
PERF_BENCHMARKS[bench_cse]=""
"""

DOCS = """# Performance

| Benchmark | Input | Time (s) |
|-----------|-------|----------|
| **mandelbrot** | 4000 | 0.72 |
| **nbody** | 50M | 8.14 |
| **binarytrees** | 17 | 2.14 |

| Benchmark | Target | Now (s) |
|-----------|--------|---------|
| `bench_matrix_multiply` | Nested loop float computation (n=500) | 0.28 |
| `bench_cse` | Common subexpression elimination | 0.01 |
"""


# The benchmark sources the fixture script above claims to run.
CLBG_SOURCES = ['nbody', 'binarytrees', 'mandelbrot']
PERF_SOURCES = ['bench_matrix_multiply', 'bench_cse']


class Harness(unittest.TestCase):
    """Writes a script, a docs page and the two benchmark directories to disk
    and runs the lint over them."""

    def setUp(self):
        self.dir = tempfile.TemporaryDirectory()
        self.addCleanup(self.dir.cleanup)

    def write(self, script=SCRIPT, docs=DOCS, clbg=CLBG_SOURCES, perf=PERF_SOURCES):
        script_path = os.path.join(self.dir.name, 'run_benchmarks.sh')
        docs_path = os.path.join(self.dir.name, 'performance.md')
        with open(script_path, 'w', encoding='utf-8') as handle:
            handle.write(script)
        with open(docs_path, 'w', encoding='utf-8') as handle:
            handle.write(docs)
        for suite, names in (('clbg', clbg), ('perf', perf)):
            directory = os.path.join(self.dir.name, suite)
            os.makedirs(directory, exist_ok=True)
            for name in names:
                with open(os.path.join(directory, name + '.obs'), 'w', encoding='utf-8') as handle:
                    handle.write('class X { }\n')
        return script_path, docs_path

    def exclusions(self, entries):
        """Swap EXCLUSIONS for this test only."""
        original = lint.EXCLUSIONS
        lint.EXCLUSIONS = entries
        self.addCleanup(setattr, lint, 'EXCLUSIONS', original)

    def run_lint(self, script=SCRIPT, docs=DOCS, clbg=CLBG_SOURCES, perf=PERF_SOURCES):
        script_path, docs_path = self.write(script, docs, clbg, perf)
        buffer = io.StringIO()
        with redirect_stdout(buffer):
            code = lint.main(['check_benchmark_inputs.py', '--script', script_path,
                              '--docs', docs_path,
                              '--clbg-dir', os.path.join(self.dir.name, 'clbg'),
                              '--perf-dir', os.path.join(self.dir.name, 'perf')])
        return code, buffer.getvalue()


class AgreementTests(Harness):
    def test_matching_inputs_pass(self):
        code, out = self.run_lint()
        self.assertEqual(0, code, out)
        self.assertIn('harness and docs agree', out)

    def test_suffix_and_plain_count_are_the_same_input(self):
        """50M in the docs is the harness's 50000000, not a disagreement."""
        code, out = self.run_lint()
        self.assertEqual(0, code, out)
        self.assertNotIn('nbody', out)

    def test_an_n_note_is_read_when_the_table_has_no_input_column(self):
        docs = DOCS.replace('(n=500)', '(n=999)')
        code, out = self.run_lint(docs=docs)
        self.assertEqual(1, code, out)
        self.assertIn('bench_matrix_multiply', out)

    def test_every_compared_row_is_counted(self):
        code, out = self.run_lint()
        self.assertEqual(0, code, out)
        # 3 CLBG rows + bench_matrix_multiply's (n=500) note
        self.assertIn('4 benchmark input(s) in 4 benchmark(s)', out)


class DisagreementTests(Harness):
    def test_the_real_defect_fails(self):
        """The 2026-09-22 drift: harness at depth 21, page at 17."""
        script = SCRIPT.replace('CLBG_BENCHMARKS[binarytrees]="17"',
                                'CLBG_BENCHMARKS[binarytrees]="21"')
        code, out = self.run_lint(script=script)
        self.assertEqual(1, code, out)
        self.assertIn('binarytrees runs at 21', out)
        self.assertIn('documents 17', out)
        self.assertIn('1 benchmark input(s) disagree', out)

    def test_a_disagreement_in_a_second_table_is_reported_too(self):
        docs = DOCS + """
| Benchmark | Input | Objeck | Java |
|-----------|-------|--------|------|
| **binarytrees** | 21 | 2.14s | 0.26s |
"""
        code, out = self.run_lint(docs=docs)
        self.assertEqual(1, code, out)
        self.assertIn('1 benchmark input(s) disagree', out)
        self.assertIn('documents 21', out)

    def test_docs_side_drift_fails_the_same_way(self):
        docs = DOCS.replace('| **mandelbrot** | 4000 |', '| **mandelbrot** | 8000 |')
        code, out = self.run_lint(docs=docs)
        self.assertEqual(1, code, out)
        self.assertIn('mandelbrot runs at 4000', out)


class ScopeTests(Harness):
    def test_an_argument_free_benchmark_is_not_compared(self):
        """bench_cse takes no input; its prose row must not be read as one."""
        code, out = self.run_lint()
        self.assertEqual(0, code, out)
        self.assertNotIn('bench_cse', out)

    def test_a_benchmark_only_the_harness_names_is_a_note_not_a_failure(self):
        script = SCRIPT + 'CLBG_BENCHMARKS[fasta]="25000000"\n'
        code, out = self.run_lint(script=script, clbg=CLBG_SOURCES + ['fasta'])
        self.assertEqual(0, code, out)
        self.assertIn('no documented input for: fasta', out)

    def test_a_docs_row_naming_no_known_benchmark_is_ignored(self):
        docs = DOCS + """
| spectralnorm | Input | Time |
|-------------|-------|------|
| Auto-JIT, default threshold | 5500 | 2.72s |
"""
        code, out = self.run_lint(docs=docs)
        self.assertEqual(0, code, out)

    def test_an_uncomparable_input_is_a_note_not_a_failure(self):
        """1M x 200 is not a plain count; say so rather than guess."""
        script = SCRIPT + 'PERF_BENCHMARKS[bench_tco]="1M x 200"\n'
        docs = DOCS.replace('| `bench_cse` |',
                            '| `bench_tco` | Tail-recursive accumulator (n=1M x 200) | 0.11 |\n| `bench_cse` |')
        code, out = self.run_lint(script=script, docs=docs,
                                  perf=PERF_SOURCES + ['bench_tco'])
        self.assertEqual(0, code, out)
        self.assertIn('not a plain count, not compared', out)

    def test_a_missing_file_is_an_error(self):
        script_path, docs_path = self.write()
        os.remove(docs_path)
        code = lint.main(['check_benchmark_inputs.py', '--script', script_path,
                          '--docs', docs_path])
        self.assertEqual(1, code)

    def test_a_script_with_no_arrays_is_an_error(self):
        code, out = self.run_lint(script='#!/bin/bash\necho hi\n')
        self.assertEqual(1, code, out)


class BenchmarkSetTests(Harness):
    """The set half: what is on disk against what the harness runs."""

    def test_the_whole_set_being_run_passes(self):
        code, out = self.run_lint()
        self.assertEqual(0, code, out)
        self.assertIn('5 benchmark(s) on disk, all run by the harness', out)

    def test_the_real_defect_fails(self):
        """The 2026-09-15 drift: bench_tco and bench_spectralnorm_native were
        in programs/tests/perf/ and in neither array of the harness."""
        code, out = self.run_lint(perf=PERF_SOURCES + ['bench_tco',
                                                       'bench_spectralnorm_native'])
        self.assertEqual(1, code, out)
        self.assertIn('bench_tco is not run by the harness', out)
        self.assertIn('bench_spectralnorm_native is not run by the harness', out)
        self.assertIn('2 benchmark(s) on disk that the harness does not run', out)

    def test_an_unrun_clbg_benchmark_is_caught_too(self):
        code, out = self.run_lint(clbg=CLBG_SOURCES + ['fasta'])
        self.assertEqual(1, code, out)
        self.assertIn('fasta is not run by the harness', out)
        self.assertIn('add CLBG_BENCHMARKS[fasta]', out)

    def test_a_documented_exclusion_passes(self):
        self.exclusions({'perf/bench_cuda_matmul': 'needs a GPU no CI runner has'})
        code, out = self.run_lint(perf=PERF_SOURCES + ['bench_cuda_matmul'])
        self.assertEqual(0, code, out)
        self.assertIn('bench_cuda_matmul.obs is excluded from the harness: '
                      'needs a GPU no CI runner has', out)

    def test_an_exclusion_for_the_other_suite_does_not_cover_this_one(self):
        """Keys are <suite>/<name>; an excuse written for clbg is not one here."""
        self.exclusions({'clbg/bench_cuda_matmul': 'needs a GPU no CI runner has'})
        code, out = self.run_lint(perf=PERF_SOURCES + ['bench_cuda_matmul'])
        self.assertEqual(1, code, out)
        self.assertIn('bench_cuda_matmul is not run by the harness', out)

    def test_a_listed_benchmark_that_does_not_exist_is_caught(self):
        """A typo in the array costs a whole benchmark; the script only warns."""
        script = SCRIPT + 'PERF_BENCHMARKS[bench_matrix_multipl]="500"\n'
        code, out = self.run_lint(script=script)
        self.assertEqual(1, code, out)
        self.assertIn('PERF_BENCHMARKS[bench_matrix_multipl] has no benchmark behind it', out)
        self.assertIn('1 listed that do not exist', out)

    def test_a_stale_exclusion_is_a_note_not_a_failure(self):
        self.exclusions({'perf/bench_gone': 'deleted in 2026-09'})
        code, out = self.run_lint()
        self.assertEqual(0, code, out)
        self.assertIn("stale EXCLUSIONS entry 'perf/bench_gone'", out)

    def test_a_missing_directory_is_an_error(self):
        script_path, docs_path = self.write()
        code = lint.main(['check_benchmark_inputs.py', '--script', script_path,
                          '--docs', docs_path,
                          '--clbg-dir', os.path.join(self.dir.name, 'nope'),
                          '--perf-dir', os.path.join(self.dir.name, 'perf')])
        self.assertEqual(1, code)

    def test_set_and_input_drift_are_both_reported_in_one_run(self):
        script = SCRIPT.replace('CLBG_BENCHMARKS[binarytrees]="17"',
                                'CLBG_BENCHMARKS[binarytrees]="21"')
        code, out = self.run_lint(script=script, perf=PERF_SOURCES + ['bench_tco'])
        self.assertEqual(1, code, out)
        self.assertIn('bench_tco is not run by the harness', out)
        self.assertIn('binarytrees runs at 21', out)

    def test_an_argument_free_benchmark_still_counts_as_run(self):
        """bench_cse has an empty input, which the input check skips -- the set
        check must not read that as "not run"."""
        code, out = self.run_lint()
        self.assertEqual(0, code, out)
        self.assertNotIn('bench_cse is not run', out)


class ParsingTests(unittest.TestCase):
    def test_parse_input_expands_suffixes(self):
        self.assertEqual(50000000, lint.parse_input('50M'))
        self.assertEqual(25000000, lint.parse_input('25m'))
        self.assertEqual(4000, lint.parse_input('4,000'))
        self.assertEqual(17, lint.parse_input('**17**'))
        self.assertEqual(5500, lint.parse_input('`5500`'))

    def test_parse_input_refuses_what_it_cannot_compare(self):
        self.assertIsNone(lint.parse_input('1M x 200'))
        self.assertIsNone(lint.parse_input('depth 17-21'))
        self.assertIsNone(lint.parse_input(''))
        self.assertIsNone(lint.parse_input(None))


class RepositoryTests(unittest.TestCase):
    """The check must pass on the files actually committed."""

    def test_the_repository_is_clean(self):
        buffer = io.StringIO()
        with redirect_stdout(buffer):
            code = lint.main(['check_benchmark_inputs.py'])
        self.assertEqual(0, code, buffer.getvalue())

    def test_every_benchmark_on_disk_is_run(self):
        script_set = lint.parse_script_set(lint.DEFAULT_SCRIPT)
        for suite, directory in (('clbg', lint.DEFAULT_CLBG_DIR),
                                 ('perf', lint.DEFAULT_PERF_DIR)):
            for name in lint.list_sources(directory):
                self.assertTrue(name in script_set[suite]
                                or '%s/%s' % (suite, name) in lint.EXCLUSIONS,
                                '%s/%s.obs is in neither the harness nor EXCLUSIONS' % (suite, name))

    def test_the_two_benchmarks_the_set_check_was_added_for_are_run(self):
        perf = lint.parse_script_set(lint.DEFAULT_SCRIPT)['perf']
        self.assertIn('bench_tco', perf)
        self.assertIn('bench_spectralnorm_native', perf)

    def test_binarytrees_is_documented_and_run_at_the_same_depth(self):
        script = lint.parse_script(lint.DEFAULT_SCRIPT)
        docs = lint.parse_docs(lint.DEFAULT_DOCS, set(script))
        self.assertIn('binarytrees', script)
        self.assertIn('binarytrees', docs)
        expected = lint.parse_input(script['binarytrees'][0])
        for raw, line, _source in docs['binarytrees']:
            self.assertEqual(expected, lint.parse_input(raw),
                             'docs/performance.md:%d' % line)


if __name__ == '__main__':
    unittest.main()
