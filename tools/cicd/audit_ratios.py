#!/usr/bin/env python3
"""Inventory the divisions in Objeck sources and shortlist the unguarded ones.

Float division by zero RAISES in Objeck rather than yielding Inf or NaN. An
unguarded ratio is therefore not a wrong number -- it is an abort, inside a
method the caller never wrote, naming nothing about the input that caused it:

    >>> Divide by zero <<<
        method: pos=3, name='System.ML.LinearSolver->GetRSquared(a:Float[], b:Int)'

A sweep of System.ML on 2026-09-24 found 99 division sites, 92 already guarded
with clamps, floors and entry checks, and 7 that were not. Two were reachable
from public API with ordinary data: LinearSolver->Calculate on a target column
that happened to be constant, and Score on a zero-row matrix in four classes,
which guarded their arguments against Nil but not against a matrix that was
merely empty. Both took the VM down.

ADVISORY, NOT A GATE
--------------------
This reports CANDIDATES for a human to read, and is deliberately not wired into
CI. On the sweep above it shortlisted 37 sites of which 7 were real, because a
guard can clamp instead of compare, can floor a value several assignments
earlier, can be an entry check written against a different variable, or can sit
in a caller. A check with that false-positive rate trains everyone to ignore it,
which is worse than not having it.

The useful output is the inventory and the shortlist, not a verdict.

SELF-CHECK
----------
The first version of this analysis reported "0 unguarded" twice while measuring
nothing. Its doc-comment stripper tested `len(line) > 2` against a closing `~#`
that is exactly two characters, so the flag never cleared and every line after
the first doc comment in a file was blanked. The canary written to validate it
used `#` comments rather than `#~` blocks, so it exercised a different path and
passed. The bug surfaced only because a result contradicted code committed
hours earlier.

The second version reported 646 candidates the moment it was pointed past
System.ML, because a path inside a string literal -- "lib/sdl/fonts/lazy.ttf" --
reads as four divisions.

So this refuses to report anything until it has classified a set of embedded
fixtures correctly, and those fixtures carry both of the shapes above. A silent
zero is the failure mode that matters, and a zero that cannot be distinguished
from "found nothing" is worthless.
"""

import os
import re
import sys

# Identifiers that appear inside a denominator expression but are not the thing
# that can be zero: casts, constructors and math helpers.
NOT_DENOMINATORS = frozenset((
    'As', 'Float', 'Int', 'Byte', 'Char', 'Bool', 'Size', 'Pow', 'Exp', 'E',
    'Log', 'Sqrt', 'Abs', 'New', 'String',
))

STRING_LITERAL = re.compile(r'"(?:[^"\\]|\\.)*"')
CHAR_LITERAL = re.compile(r"'(?:[^'\\]|\\.)*'")
DENOMINATOR = re.compile(r'/\s*(\([^)]*\)|[A-Za-z_@][\w\.\[\]@]*(?:->\w+\(\))*)')
NUMERIC = re.compile(r'\d+\.?\d*\Z')
DECLARATION = re.compile(r'^\s*(method|function)\s*:')


def strip_literals(code):
    """Blank string and character literals.

    A path in a string is not arithmetic: "lib/sdl/fonts/lazy.ttf" otherwise
    reads as divisions by `sdl`, `fonts` and `lazy.ttf`.
    """
    return CHAR_LITERAL.sub("''", STRING_LITERAL.sub('""', code))


def code_lines(text):
    """Return [(lineno, code)] with doc comments, comments and literals blanked.

    Objeck doc comments open with `#~` and close with `~#`, usually alone on its
    own line -- a closing marker that is exactly two characters long, which an
    earlier version of this function failed to recognise. Everything inside one
    is prose and must not be scanned: `@param a the bias/intercept term` is not
    a division.
    """
    out = []
    in_doc = False
    for number, line in enumerate(text.splitlines(), 1):
        stripped = line.strip()
        if in_doc:
            out.append((number, ''))
            if '~#' in stripped:
                in_doc = False
            continue
        if stripped.startswith('#~'):
            # a one-line `#~ ... ~#` opens and closes on the same line
            if not (stripped.endswith('~#') and len(stripped) > 3):
                in_doc = True
            out.append((number, ''))
            continue
        out.append((number, strip_literals(line.split('#')[0])))
    return out


def enclosing_start(lines, index):
    """Index of the `method :` / `function :` line that opens this body."""
    for k in range(index - 1, max(0, index - 400), -1):
        if DECLARATION.search(lines[k][1]):
            return k
    return max(0, index - 150)


def is_guarded(window, denominator):
    """Whether the enclosing body visibly guards this denominator.

    Deliberately generous. A false 'guarded' costs a missed candidate; a false
    'unguarded' costs someone's afternoon reading code that was already correct.
    The shortlist is only useful if it is short enough to read in full.
    """
    names = [n for n in re.findall(r'@?[A-Za-z_]\w*', denominator)
             if n not in NOT_DENOMINATORS]

    # A denominator is often an alias of the value that was actually checked:
    # `n := rows->As(Float)` after `if(rows < 2) { return false; }`. Follow one
    # level of assignment so the guard on `rows` counts for `n` too.
    for name in list(names):
        for rhs in re.findall(re.escape(name) + r'\s*:=\s*([^;]+);', window):
            names.extend(m for m in re.findall(r'@?[A-Za-z_]\w*', rhs)
                         if m not in NOT_DENOMINATORS)

    for name in names:
        q = re.escape(name)
        # Compared against a small literal. Entry checks are written as any of
        # `rows < 1`, `rows = 0`, `rows < 2` (a covariance needs two rows), so
        # matching only 0 and 1 misses a whole family of real guards.
        if (re.search(q + r'\s*(\[[^\]]*\])?\s*(->\w+\(\))?\s*(<=|>=|<>|=|<|>)\s*[0-9]\b', window)
                or re.search(r'(<=|>=|<>|=|<|>)\s*' + q + r'\b', window)
                or re.search(q + r'\s*\+\s*\w+\s*(=|>|<)\s*0', window)
                or re.search(q + r'\s*->\s*IsEmpty\(\)', window)
                or 'floor' in window):
            return True
    return False


def scan_text(text):
    """Return (non_literal_sites, [(lineno, denominator, code)])."""
    lines = code_lines(text)
    total = 0
    flagged = []
    for index, (number, code) in enumerate(lines):
        if '/' not in code:
            continue
        for denominator in DENOMINATOR.findall(code):
            if NUMERIC.match(denominator):
                continue          # a literal denominator cannot be data-dependent
            total += 1
            window = ''.join(x[1] for x in lines[enclosing_start(lines, index):index])
            if not is_guarded(window, denominator):
                flagged.append((number, denominator, code.strip()))
    return total, flagged


# --- fixtures -------------------------------------------------------------
# Indented with spaces rather than tabs so nothing here depends on escaping.
# Each method carries a shape that has actually fooled this analysis.

FIXTURE = '''bundle Probe {
  class Canary {
    #~
    A doc comment mentioning a bias/intercept term and (TP + TN) / Total.
    Neither is a division in code, and the closing marker below is exactly two
    characters -- the stripper must still clear its flag, or every line after
    this point is blanked and the scan silently measures nothing.
    ~#
    method : Unguarded(values : Float[], total : Float) ~ Float {
      sum := 0.0;
      return sum / total;
    }

    method : UnguardedParen(rate : Float) ~ Float {
      return rate / (1.0 - rate);
    }

    method : Guarded(values : Float[], total : Float) ~ Float {
      if(total = 0.0) {
        return 0.0;
      };
      return values->Size()->As(Float) / total;
    }

    method : GuardedByEntryCheck(X : Float[,]) ~ Float {
      dims := X->Size();
      rows := dims[0];
      if(rows < 1) {
        return 0.0;
      };
      return 1.0 / rows->As(Float);
    }

    method : Halved(n : Float) ~ Float {
      return n / 2.0;
    }

    method : PathsAreNotDivisions() ~ String {
      sep := '/';
      return "lib/sdl/fonts/lazy.ttf";
    }
  }
}
'''

EXPECTED_FLAGGED = frozenset(('total', '(1.0 - rate)'))
EXPECTED_TOTAL = 4    # four non-literal denominators; `/ 2.0` and the path are not


def self_check():
    """Problems found classifying the fixtures; empty means safe to report."""
    total, flagged = scan_text(FIXTURE)
    names = frozenset(d for _, d, _ in flagged)
    problems = []
    if total != EXPECTED_TOTAL:
        problems.append(
            'counted %d non-literal denominators, expected %d -- the doc-comment '
            'stripper, the literal stripper or the scanner is wrong'
            % (total, EXPECTED_TOTAL))
    missed = EXPECTED_FLAGGED - names
    spurious = names - EXPECTED_FLAGGED
    if missed:
        problems.append('failed to flag known-unguarded: %s' % ', '.join(sorted(missed)))
    if spurious:
        problems.append('flagged known-guarded: %s' % ', '.join(sorted(spurious)))
    return problems


def sources(roots):
    paths = []
    for root in roots:
        if os.path.isfile(root):
            paths.append(root)
            continue
        for base, _, names in os.walk(root):
            for name in sorted(names):
                if name.endswith('.obs'):
                    paths.append(os.path.join(base, name))
    return sorted(paths)


def main(argv):
    roots = argv[1:] or ['core/compiler/lib_src']

    problems = self_check()
    if problems:
        print('SELF-CHECK FAILED -- not reporting results.')
        for problem in problems:
            print('  %s' % problem)
        print()
        print('This analysis has silently measured nothing before: a stripper bug')
        print('blanked every line after the first doc comment and it reported zero')
        print('unguarded divisions twice. A zero from a broken scanner cannot be')
        print('told apart from a clean tree, so it reports nothing at all.')
        return 2

    paths = sources(roots)
    if not paths:
        print('no .obs sources under: %s' % ', '.join(roots))
        return 2

    total_sites = 0
    total_flagged = 0
    for path in paths:
        with open(path, encoding='utf-8', errors='replace') as handle:
            sites, flagged = scan_text(handle.read())
        total_sites += sites
        if not flagged:
            continue
        total_flagged += len(flagged)
        print('%s' % path.replace('\\', '/'))
        for number, denominator, code in flagged:
            print('  %5d  / %-24s %s' % (number, denominator[:24], code[:60]))
        print()

    print('%d file(s), %d division site(s) with a non-literal denominator, '
          '%d without a visible guard.' % (len(paths), total_sites, total_flagged))
    print()
    print('These are CANDIDATES, not defects. Read each one: a guard can clamp')
    print('rather than compare, can floor a value several assignments earlier, or')
    print('can live in the only caller. On the System.ML sweep that produced this')
    print('script, 7 of 37 candidates were real.')
    return 0


if __name__ == '__main__':
    sys.exit(main(sys.argv))
