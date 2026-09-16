#!/usr/bin/env python3
"""Fail when a benchmark's input in the harness disagrees with the docs.

`perf-results/run_benchmarks.sh` holds the input each benchmark is run with;
`docs/performance.md` publishes the input each reported number was measured at.
Nothing kept them in step, and they drifted: the harness ran `binarytrees` at
depth **21** while every table on the page said **17** (2026-09-22). Depth is
the exponent of a binary-tree workload, so that is not a 4/17 discrepancy but
roughly a 16x one -- enough that a harness run planned from the published
numbers came out with a multi-hour estimate for what the page reports as 2.14s,
and enough that any number produced by that run would have been silently
incomparable to the page it would have been written into.

That is the whole failure family this guards: the run still succeeds, the CSV
still fills in, and the only symptom is a number measured at an input nobody
reading it knows about.

What is compared: every benchmark named by both files. From the shell script,
the `CLBG_BENCHMARKS[name]="input"` and `PERF_BENCHMARKS[name]="input"` array
entries. From the Markdown, any table row whose first cell names one of those
benchmarks, taking the input from an `Input` column when the table has one and
otherwise from an `(n=...)` note in the row. Inputs are compared numerically
after `50M`/`25M`-style suffixes are expanded, so `50M` and `50000000` agree.
A benchmark only one file mentions is reported as a note, not a failure: the
docs cover runs and shapes the harness does not drive, and the harness holds
benchmarks the page has no row for.

Usage:
    check_benchmark_inputs.py [--script PATH] [--docs PATH]

Exit 0 when every benchmark named by both files carries the same input, 1
otherwise.
"""

import argparse
import os
import re
import sys

REPO_ROOT = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
DEFAULT_SCRIPT = os.path.join(REPO_ROOT, 'perf-results', 'run_benchmarks.sh')
DEFAULT_DOCS = os.path.join(REPO_ROOT, 'docs', 'performance.md')

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
    args = parser.parse_args(argv[1:])

    for path in (args.script, args.docs):
        if not os.path.isfile(path):
            print('%s: not found' % path, file=sys.stderr)
            return 1

    script = parse_script(args.script)
    if not script:
        print('%s: no CLBG_BENCHMARKS/PERF_BENCHMARKS entries found' % args.script, file=sys.stderr)
        return 1

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

    compared = sum(len(docs[name]) for name in set(script) & set(docs))
    print('%d benchmark input(s) in %d benchmark(s): harness and docs agree.'
          % (compared, len(set(script) & set(docs))))
    return 0


if __name__ == '__main__':
    sys.exit(main(sys.argv))
