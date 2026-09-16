#!/usr/bin/env python3
"""Fail when `perf-results/run_benchmarks.sh` drifts from what it should run.

The harness is not the source of truth for either half of what it does. The
benchmark *set* belongs to `programs/tests/clbg/` and `programs/tests/perf/`;
the *input* each published number was measured at belongs to
`docs/performance.md`. Treating the script as authoritative has produced the
same silent defect twice:

* **Inputs (2026-09-22).** The harness ran `binarytrees` at depth **21** while
  every table on the page said **17**. Depth is the exponent of a binary-tree
  workload, so that is not a 4/17 discrepancy but roughly a 16x one -- enough
  that a harness run planned from the published numbers came out with a
  multi-hour estimate for what the page reports as 2.14s, and enough that any
  number it produced would have been silently incomparable to the page it was
  going to be written into.
* **Set (2026-09-15).** `programs/tests/perf/` held `bench_tco.obs` and
  `bench_spectralnorm_native.obs`, and the harness listed neither, so a
  cross-build comparison covered 10 benchmarks while reporting on 12. The input
  check sailed past it: it only ever looked at benchmarks that were listed.

Both have the same shape -- the run succeeds, the CSV fills in, and the only
symptom is a result that means something other than what its reader assumes.

Two things are therefore checked.

**The set.** Every `.obs` under `programs/tests/clbg/` and
`programs/tests/perf/` is either run by the harness or named in `EXCLUSIONS`
below with a reason; and every benchmark the harness names exists on disk (the
script only warns and skips when it does not, which is how a typo costs a whole
benchmark without failing anything).

**The inputs.** Every benchmark named by both the script and the docs. From the
shell script, the `CLBG_BENCHMARKS[name]="input"` and
`PERF_BENCHMARKS[name]="input"` array entries. From the Markdown, any table row
whose first cell names one of those benchmarks, taking the input from an
`Input` column when the table has one and otherwise from an `(n=...)` note in
the row. Inputs are compared numerically after `50M`/`25M`-style suffixes are
expanded, so `50M` and `50000000` agree. A benchmark only one file mentions is
reported as a note, not a failure: the docs cover runs and shapes the harness
does not drive, and the harness holds benchmarks the page has no row for.

Usage:
    check_benchmark_inputs.py [--script PATH] [--docs PATH]
                              [--clbg-dir PATH] [--perf-dir PATH]

Exit 0 when the harness runs the whole benchmark set and every benchmark named
by both files carries the same input, 1 otherwise.
"""

import argparse
import os
import re
import sys

REPO_ROOT = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
DEFAULT_SCRIPT = os.path.join(REPO_ROOT, 'perf-results', 'run_benchmarks.sh')
DEFAULT_DOCS = os.path.join(REPO_ROOT, 'docs', 'performance.md')
DEFAULT_CLBG_DIR = os.path.join(REPO_ROOT, 'programs', 'tests', 'clbg')
DEFAULT_PERF_DIR = os.path.join(REPO_ROOT, 'programs', 'tests', 'perf')

# Which shell array holds which directory's benchmarks.
SUITES = {'CLBG': 'clbg', 'PERF': 'perf'}

# Benchmarks that exist on disk and are deliberately NOT run by the harness,
# keyed "<suite>/<name>". The value is the reason, and it has to be one a
# reader can check: an entry here is the only way a benchmark stays out of a
# cross-build comparison, so an undocumented one gives back exactly the hole
# this check exists to close. Example of the shape:
#
#     'perf/bench_cuda_matmul': 'needs a GPU no CI runner has',
#
# Empty today -- every .obs in both directories is run.
EXCLUSIONS = {}

# CLBG_BENCHMARKS[binarytrees]="21"   /   PERF_BENCHMARKS[bench_tco]=""
ARRAY_ENTRY_RE = re.compile(r'^\s*(CLBG|PERF)_BENCHMARKS\[([A-Za-z0-9_]+)\]\s*=\s*"([^"]*)"')

# An input written into a prose cell: "Nested loop float computation (n=500)".
N_NOTE_RE = re.compile(r'\(n\s*=\s*([^)]+)\)', re.IGNORECASE)

SUFFIXES = {'k': 10 ** 3, 'm': 10 ** 6, 'b': 10 ** 9, 'g': 10 ** 9}


def strip_markup(cell):
    """Drop the emphasis and code markers a table cell wraps its text in."""
    text = cell.strip()
    text = re.sub(r'`([^`]*)`', r'\1', text)
    text = re.sub(r'\*\*([^*]*)\*\*', r'\1', text)
    text = re.sub(r'\*([^*]*)\*', r'\1', text)
    return text.strip()


def parse_input(text):
    """Return an input as an int, or None when it is not a plain count.

    Accepts 4000, 4,000, 50M and 25m. Anything else (a range, a product such as
    1M x 200, a word) is not comparable and is reported as unparsed rather than
    guessed at.
    """
    if text is None:
        return None
    value = strip_markup(text).replace(',', '').replace('_', '')
    match = re.fullmatch(r'(\d+)\s*([KkMmBbGg]?)', value)
    if not match:
        return None
    number = int(match.group(1))
    suffix = match.group(2).lower()
    return number * SUFFIXES[suffix] if suffix else number


def read_text(path):
    with open(path, encoding='utf-8') as handle:
        return handle.read()


def parse_script(path):
    """Benchmarks and their inputs, as the harness runs them.

    Returns {name: (raw_input, line_number)}. Entries with an empty input take
    no argument and are not comparable, so they are left out.
    """
    found = {}
    for number, line in enumerate(read_text(path).splitlines(), 1):
        match = ARRAY_ENTRY_RE.match(line)
        if not match:
            continue
        name, raw = match.group(2), match.group(3).strip()
        if raw:
            found[name] = (raw, number)
    return found


def parse_script_set(path):
    """Every benchmark the harness runs, by suite.

    Returns {'clbg': {name: line_number}, 'perf': {...}}. Unlike parse_script
    this keeps entries whose input is empty: a benchmark that takes no argument
    is still a benchmark the harness runs.
    """
    found = {suite: {} for suite in SUITES.values()}
    for number, line in enumerate(read_text(path).splitlines(), 1):
        match = ARRAY_ENTRY_RE.match(line)
        if not match:
            continue
        found[SUITES[match.group(1)]][match.group(2)] = number
    return found


def list_sources(directory):
    """Benchmark names on disk: every `.obs` in the directory, extension off."""
    return sorted(name[:-len('.obs')] for name in os.listdir(directory)
                  if name.endswith('.obs'))


def compare_sets(script_set, sources):
    """Return (unrun, absent, excluded, stale) for the benchmark set.

    `sources` is {suite: [name]} read off disk. unrun: on disk, not run and not
    excluded -- the failure the two missing perf benchmarks were. absent: run
    by the harness with no `.obs` behind it, which the script itself only warns
    about. excluded and stale are notes: the documented opt-outs, and entries
    in EXCLUSIONS that no longer name a benchmark that is both present and
    unrun.
    """
    unrun = []
    absent = []
    excluded = []
    for suite in sorted(sources):
        run = script_set.get(suite, {})
        for name in sources[suite]:
            if name in run:
                continue
            reason = EXCLUSIONS.get('%s/%s' % (suite, name))
            if reason:
                excluded.append((suite, name, reason))
            else:
                unrun.append((suite, name))
        for name in sorted(run):
            if name not in sources[suite]:
                absent.append((suite, name, run[name]))

    live = set('%s/%s' % (suite, name) for suite, name, _reason in excluded)
    stale = [(key, EXCLUSIONS[key]) for key in sorted(EXCLUSIONS) if key not in live]
    return unrun, absent, excluded, stale


def split_row(line):
    """Cells of a Markdown table row, without the leading/trailing pipes."""
    stripped = line.strip()
    if not stripped.startswith('|'):
        return None
    cells = stripped.split('|')[1:]
    if cells and cells[-1].strip() == '':
        cells = cells[:-1]
    return [cell.strip() for cell in cells]


def is_separator(cells):
    return bool(cells) and all(re.fullmatch(r':?-{2,}:?', cell.replace(' ', '')) for cell in cells)


def parse_docs(path, names):
    """Inputs the docs publish for the given benchmark names.

    Returns {name: [(raw_input, line_number, source)]} -- a benchmark may appear
    in several tables, and every row is checked.
    """
    found = {}
    header = None
    input_column = None
    for number, line in enumerate(read_text(path).splitlines(), 1):
        cells = split_row(line)
        if cells is None:
            header = None
            input_column = None
            continue
        if is_separator(cells):
            continue
        if header is None:
            header = [strip_markup(cell).lower() for cell in cells]
            input_column = header.index('input') if 'input' in header else None
            continue

        name = strip_markup(cells[0])
        if name not in names:
            continue

        raw = None
        source = None
        if input_column is not None and input_column < len(cells):
            raw = cells[input_column]
            source = 'Input column'
        else:
            for cell in cells[1:]:
                note = N_NOTE_RE.search(cell)
                if note:
                    raw = note.group(1)
                    source = 'an (n=...) note'
                    break
        if raw is None:
            continue
        found.setdefault(name, []).append((strip_markup(raw), number, source))
    return found


def compare(script, docs):
    """Return (failures, unparsed) for every benchmark both files name."""
    failures = []
    unparsed = []
    for name in sorted(set(script) & set(docs)):
        script_raw, script_line = script[name]
        script_value = parse_input(script_raw)
        for docs_raw, docs_line, source in docs[name]:
            docs_value = parse_input(docs_raw)
            if script_value is None or docs_value is None:
                unparsed.append((name, script_raw, script_line, docs_raw, docs_line))
                continue
            if script_value != docs_value:
                failures.append((name, script_raw, script_value, script_line,
                                 docs_raw, docs_value, docs_line, source))
    return failures, unparsed


def main(argv):
    parser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    parser.add_argument('--script', default=DEFAULT_SCRIPT,
                        help='benchmark harness (default: perf-results/run_benchmarks.sh)')
    parser.add_argument('--docs', default=DEFAULT_DOCS,
                        help='published results (default: docs/performance.md)')
    parser.add_argument('--clbg-dir', default=DEFAULT_CLBG_DIR,
                        help='CLBG sources (default: programs/tests/clbg)')
    parser.add_argument('--perf-dir', default=DEFAULT_PERF_DIR,
                        help='micro-benchmark sources (default: programs/tests/perf)')
    args = parser.parse_args(argv[1:])

    for path in (args.script, args.docs):
        if not os.path.isfile(path):
            print('%s: not found' % path, file=sys.stderr)
            return 1
    for path in (args.clbg_dir, args.perf_dir):
        if not os.path.isdir(path):
            print('%s: not a directory' % path, file=sys.stderr)
            return 1

    script = parse_script(args.script)
    if not script:
        print('%s: no CLBG_BENCHMARKS/PERF_BENCHMARKS entries found' % args.script, file=sys.stderr)
        return 1

    directories = {'clbg': args.clbg_dir, 'perf': args.perf_dir}
    sources = dict((suite, list_sources(path)) for suite, path in directories.items())
    unrun, absent, excluded, stale = compare_sets(parse_script_set(args.script), sources)

    for suite, name in unrun:
        print('%s: %s is not run by the harness' % (args.script, name))
        print('    source: %s' % os.path.join(directories[suite], name + '.obs'))
        print('    add %s_BENCHMARKS[%s], or an EXCLUSIONS entry in this check saying why not'
              % (suite.upper(), name))
    for suite, name, line in absent:
        print('%s:%d: %s_BENCHMARKS[%s] has no benchmark behind it'
              % (args.script, line, suite.upper(), name))
        print('    expected: %s' % os.path.join(directories[suite], name + '.obs'))

    if unrun or absent:
        print()
        print('%d benchmark(s) on disk that the harness does not run, %d listed that do not exist.'
              % (len(unrun), len(absent)))
        print('The benchmark directories are the source of truth for what a comparison')
        print('covers, and the harness only warns and skips over what it cannot find, so')
        print('either gap costs whole benchmarks from a run that still reports success.')

    docs = parse_docs(args.docs, set(script))
    failures, unparsed = compare(script, docs)

    for name, script_raw, script_value, script_line, docs_raw, docs_value, docs_line, source in failures:
        print('%s: %s runs at %s (%d)' % (args.script, name, script_raw, script_value))
        print('    %s:%d: %s documents %s (%d), from %s'
              % (args.docs, docs_line, name, docs_raw, docs_value, source))
        print('    harness: %s:%d' % (args.script, script_line))

    if failures:
        print()
        print('%d benchmark input(s) disagree between the harness and the docs.' % len(failures))
        print('Every published number was measured at the input beside it, so a harness')
        print('that runs a different one produces figures that cannot be compared to the')
        print('page -- and, for an exponential workload such as binarytrees, a run whose')
        print('cost is nothing like the one the page implies. Change whichever is wrong,')
        print('and re-measure if it is the page.')
        return 1

    for name, script_raw, script_line, docs_raw, docs_line in unparsed:
        print('note: %s: input not a plain count, not compared (%s:%d = %r, %s:%d = %r)'
              % (name, args.script, script_line, script_raw, args.docs, docs_line, docs_raw))

    only_script = sorted(set(script) - set(docs))
    if only_script:
        print('note: no documented input for: %s' % ', '.join(only_script))

    for suite, name, reason in excluded:
        print('note: %s/%s.obs is excluded from the harness: %s' % (suite, name, reason))
    for key, reason in stale:
        print('note: stale EXCLUSIONS entry %r (%s): the benchmark is gone or is run now'
              % (key, reason))

    compared = sum(len(docs[name]) for name in set(script) & set(docs))
    print('%d benchmark input(s) in %d benchmark(s): harness and docs agree.'
          % (compared, len(set(script) & set(docs))))
    if unrun or absent:
        return 1
    print('%d benchmark(s) on disk, all run by the harness or documented as excluded.'
          % sum(len(names) for names in sources.values()))
    return 0


if __name__ == '__main__':
    sys.exit(main(sys.argv))
