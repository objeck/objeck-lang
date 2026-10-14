# Hardening the language with Rosetta Code tasks

Rosetta Code lists roughly 900 tasks with no Objeck entry
([Tasks not implemented in Objeck](https://rosettacode.org/wiki/Tasks_not_implemented_in_Objeck)).
Working through them is how this project has historically found library gaps and
language defects: each task is a small program written by someone who does not
already know which corners of the library work, which is exactly the property a
test suite written by the implementer cannot have.

**The entries are the by-product. The deliverable is fixed bugs and closed
gaps.** A task that compiles and runs first time taught us nothing; a task that
cannot be expressed at all is the most valuable result on the list.

This document is the plan and the working discipline. It is written so that
several sessions can work the list at once without colliding or duplicating.

---

## How to claim a task

**Claims live in GitHub, not in this file.** A table here would go stale between
one session's fetch and another's push, and that has already cost this project
real duplicated work -- four sessions once fixed the same issue on the same day.

Before starting a task:

```bash
git fetch origin                     # the list below may have moved
gh issue list --state all --search "rosetta: <Task Name>"
gh pr list --state all --search "<Task Name>"
```

If nothing comes back, **file the issue first** -- that is the claim, and it is
atomic in a way a document cannot be:

```
Title:  rosetta: <Task Name>
Body:   the task URL, what it should exercise, and why it was picked
```

Then close it with the PR. If a task turns out to be blocked, close the issue
explaining what blocked it and link the defect -- a blocked task is a finding,
and the next session needs to not re-discover it.

The `rosetta:` title prefix is the whole convention. Keep it exact.

---

## The working recipe

Tasks are written as standalone programs, compiled and run against a deployed
toolchain. Nothing goes in `programs/regression/` unless it is guarding a fix.

```bash
export OBJECK_LIB_PATH="<repo>/core/release/deploy-x64/lib"
BIN=<repo>/core/release/deploy-x64/bin

$BIN/obc.exe -src task.obs -dest task.obe        # add -lib a,b,c as needed
$BIN/obr.exe task.obe
```

Verify against the output the task page states, not against what looks
plausible. `Arithmetic/Rational` names its expected answer (`6, 28, 496, 8128`);
most tasks do.

### Discipline, learned by getting it wrong

- **Compile every probe on its own.** The compiler reports only the *first*
  error. A second "bug" in the same file is usually the first one's shadow --
  one was filed as real in this very effort before a one-line probe disproved
  it.
- **Build the probe table before filing.** A finding is a table of forms that
  work beside forms that do not (see #1099, #1101). Inference from a single
  failure is how you file a bug that does not exist.
- **Never pipe a compile through `head`/`tail` and read the exit code.** You get
  the pager's status, not the compiler's. Redirect to a file, test `$?`, then
  read the file.
- **A truncated `grep` is not an absence.** Two "this does not exist in the
  library" claims in this effort were `| head -10` cutting off the evidence.
  Count first, then look.

---

## What to pick, and why

Ordered by expected defect yield, not by ease. A task is worth doing if it
reaches a subsystem no other task on the shortlist reaches.

### Tier 1 -- language semantics

These exercise the compiler and VM rather than a library, and they are where the
findings have come from so far.

| Cluster | Tasks | Why |
|---|---|---|
| Closures / functional | Currying, Partial function application, Function composition, Y combinator, Anonymous recursion | **Blocked** -- see Findings. All four need a function that takes an argument *and* closes over state, which the language cannot express today |
| Exceptions | Exceptions, Exceptions/Catch an exception thrown in a nested call | Objeck has `Try()`/`Otherwise()` on chains and no `throw`. These tasks establish how far `Try()` reaches and document the gap |
| Numeric edges | Integer overflow, Two's complement, Zero to the zero power, Extreme floating point values, Infinity, Detect division by zero | `FEATURES.md` states the semantics precisely (64-bit two's complement, `+ - *` wrap and never trap, a zero divisor is the only integer trap). These verify the documentation is true |
| Arbitrary precision | Arbitrary-precision integers (included) | Needs the first and last 20 digits of 5^4^3^2, a 183,231-digit number. **Settles the BigInt question with a fact** |

### Tier 2 -- one task per subsystem

Chosen so a gap anywhere surfaces, rather than ten tasks hitting `String`.

| Subsystem | Tasks |
|---|---|
| Strings, RegEx | Align columns, Word wrap, Natural sorting, Tokenize a string with escaping |
| Collections | Cartesian product of two or more lists, Power set, Priority queue, Topological sort |
| `System.Time` (`misc.obl`) | Day of the week, Last Friday of each month, Convert seconds to compound duration |
| File IO | Hex dump, Read a specific line from a file, Remove lines from a file |
| `concurrent.obl` | Dining philosophers, Checkpoint synchronization, Atomic updates |

### Not on the list, deliberately

Stated so nobody counts them as remaining work:

- **Graphics, GUI, windowing** (Bitmap/\*, Window creation, Mandelbrot, the
  curve and fractal tasks). SDL- and display-dependent, flaky without a
  compositor, poor yield per hour.
- **OS-specific** (Write to Windows event log, Create a file on magnetic tape,
  Punched cards).
- **Anything needing credentials or a live third-party service** (Active
  Directory/\*, SOAP, Yahoo! search interface, MAC vendor lookup).
- **The "NKTg Law" / "NKT Law" entries.** Not tasks.

---

## Findings

| # | Finding | Status |
|---|---|---|
| [#1099](https://github.com/objeck/objeck-lang/issues/1099) | Unary minus rejects a parenthesised operand: `-(a + b)` does not parse, while `-x`, `-7`, `-F()` and `-@n` all do | open |
| [#1100](https://github.com/objeck/objeck-lang/issues/1100) | The function-composition idiom in `FEATURES.md` silently returns wrong results when a second composer is built -- captures live in static fields, so both references compute the last pair. `add_then_add(2)` returns 60 instead of 5, with no diagnostic | open |
| [#1101](https://github.com/objeck/objeck-lang/issues/1101) | `FuncRef<R>` has no parameter slots -- it is a thunk type -- but `FEATURES.md:209` documents lambdas as `\(params) => body`. Six spellings, six different errors | open |

**#1100 and #1101 together block an entire cluster.** A lambda can close over
state but cannot take an argument; a first-class function reference takes
arguments but carries no environment. Nothing in the language today is both, so
currying, partial application, composition and the Y combinator have no correct
formulation. That is the most significant thing this effort has turned up, and
it is a language feature decision rather than a bug fix.

### Completed

| Task | Notes |
|---|---|
| [Arithmetic/Rational](https://rosettacode.org/wiki/Arithmetic/Rational) | `Rational` with reduction, the six comparisons, flooring integer division and modulo, `Int` coercion, and detected overflow rather than silent wraparound. Verified: `6, 28, 496, 8128` in ~1s. Produced #1099 |

---

## Library proposals

These are permanent public API, so they are proposed here and decided before
they land -- not added as a side effect of a task.

### `Rational` -- written, undecided

Exists as a verified standalone implementation. Three things to settle:

1. **Overflow behaviour.** Numerator and denominator are 64-bit, so a chain of
   unrelated denominators exceeds the range. The implementation detects it and
   yields a value that reports itself invalid and propagates. Silent wraparound
   in a published numeric type is the alternative, and it is worse.
2. **Where it lives.** Not `lang.obl` -- a version bump must rebuild and commit
   that or windows-x64 CI fails, and every program pays for it. `misc.obl` is
   `System.Time` plus `System.Utility` and is not a numeric home. A new
   `math.obl` is the natural answer.
3. **Whether to wait for BigInt**, which would remove the overflow question
   entirely.

### `Complex` -- same question

`Arithmetic/Complex` asks for the identical shape. Decide it with `Rational`, so
the two land in one library with one convention rather than separately.

### `BigInt` -- the larger gap

There is **no arbitrary-precision integer anywhere** in the 32 shipped
libraries, and `cipher.obl` exposes no multi-precision internals to build on.
That blocks exact rationals, large factorials, and a long tail of numeric tasks.
It is also the one proposal that probably wants a native implementation for
speed, which makes it a project rather than a library file.

`Arbitrary-precision integers (included)` in Tier 1 is the cheapest way to turn
this from an opinion into a measurement: implement it, and the result either
works or names exactly what is missing.

---

## Things already verified, so nobody re-checks them

- No `Rational`, `Complex` or `BigInt` in any of the 32 shipped `.obl`
  libraries.
- Lambdas capture by copy, taken at creation. An object is copied as a
  reference, so mutation through it is shared. `self`, `@field` and bare
  instance-method calls are unavailable in a lambda body.
- `Int` is 64-bit two's complement; `+ - *` wrap and never trap; `/` truncates
  toward zero and `%` takes the dividend's sign; a zero divisor is the only
  integer trap. So a flooring division has to be written by hand -- truncation
  is not floor for negative values.
- Objeck has no operator overloading. Arithmetic types expose named methods.
