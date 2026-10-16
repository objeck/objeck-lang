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
| Closures / functional | Currying, Partial function application, Function composition, Y combinator, Anonymous recursion | **Open, and the mechanism is proven.** A parameterised lambda both takes an argument and captures: `\(Int) ~ Int : (x) => x + n`. Two are independent, so currying and composition are correct and reentrant. Earlier text here called these blocked; that was wrong (see Findings) |
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
| [#1103](https://github.com/objeck/objeck-lang/issues/1103) | 35 words are reserved and none are documented; using one reports only `Expected identifier` without naming it. `other` -- the conventional parameter name for `Equals`/`Compare`/`Add` -- is `select`'s default label | open |

**#1100 and #1101 together block an entire cluster.** A lambda can close over
state but cannot take an argument; a first-class function reference takes
arguments but carries no environment. Nothing in the language today is both, so
currying, partial application, composition and the Y combinator have no correct
formulation. That is the most significant thing this effort has turned up, and
it is a language feature decision rather than a bug fix.

### Batch 3 (2026-10-15): 50 tasks, ~60 findings

Fifty tasks chosen by subsystem reach (#1118-#1167, tests in #1169), against
the 48 arithmetic tasks before them, which found nothing. The yield is the
argument for picking this way: the batch found nine compiler defects, all of
them crashes or wrong values in every execution mode, fixed in #1168:
- `+` and interpolation typed a call chain by its head
- `As(T)` and `TypeOf` broke call chains
- a primitive method on a call result got its arguments reversed
- closures overlapped a capture placed after a function value
- function-value results lost their casts

It also found the rest, filed by subsystem:

| # | Area |
|---|---|
| [#1170](https://github.com/objeck/objeck-lang/issues/1170) | x64 JIT branches on `nan = x` as if equal |
| [#1171](https://github.com/objeck/objeck-lang/issues/1171) | `?->` recovery corrupts the operand stack (interpreter with arguments; JIT two frames down) |
| [#1172](https://github.com/objeck/objeck-lang/issues/1172) | `??` treats a real 0 as Nil |
| [#1173](https://github.com/objeck/objeck-lang/issues/1173) | `Float->ToString` kills the VM at 1e57 and above |
| [#1174](https://github.com/objeck/objeck-lang/issues/1174) | serializer drops multi-dimensional array data |
| [#1175](https://github.com/objeck/objeck-lang/issues/1175) | reflection metadata wrong or missing |
| [#1176](https://github.com/objeck/objeck-lang/issues/1176) | scalar Byte/Char never narrow; `ToBinaryString(negative)` loops |
| [#1177](https://github.com/objeck/objeck-lang/issues/1177) | String `Split`/`TrimBack`/`Lines`/`Justify` edge cases |
| [#1178](https://github.com/objeck/objeck-lang/issues/1178) | `Float->Pi()` to 11 digits; `Abs(-0.0)` |
| [#1179](https://github.com/objeck/objeck-lang/issues/1179) | System.Time: DST-shifted GMT dates, weekday unset |
| [#1180](https://github.com/objeck/objeck-lang/issues/1180) | file IO: phantom `ReadLine`, `Flush` loses the handle |
| [#1181](https://github.com/objeck/objeck-lang/issues/1181) | the JIT allocates negative-size arrays |
| [#1182](https://github.com/objeck/objeck-lang/issues/1182) | generics: chained calls lose type arguments |
| [#1183](https://github.com/objeck/objeck-lang/issues/1183) | function types: array parameters, exact classes, unassigned value |
| [#1184](https://github.com/objeck/objeck-lang/issues/1184) | parser: `F(a)(b)` silently becomes two statements |
| [#1185](https://github.com/objeck/objeck-lang/issues/1185) | Windows: Char above U+FFFF truncated |
| [#1186](https://github.com/objeck/objeck-lang/issues/1186) | library gaps (Semaphore, set algebra, comparator sort, ...) |

Two process notes:
- **Pick from the page itself.** Picks come from the rendered *Tasks not
  implemented in Objeck* page. Its wikitext is only a template; a category
  difference gives a different, smaller list.
- **Run three modes.** Every task ran interpreted, with the default JIT, and
  with `OBJECK_JIT_THRESHOLD=1`. Three of the findings exist in only one of
  them.

### Completed

| Task | Notes |
|---|---|
| [Arithmetic/Rational](https://rosettacode.org/wiki/Arithmetic/Rational) | `Rational` with reduction, the six comparisons, flooring integer division and modulo, `Int` coercion, and detected overflow rather than silent wraparound. Verified: `6, 28, 496, 8128` in ~1s. Produced #1099 |
| [Arbitrary-precision integers (included)](<https://rosettacode.org/wiki/Arbitrary-precision_integers_(included)>) | 5^262144 computed by 18 squarings. Verified against the published answer: `183231` digits, first 20 `62060698786608744707`, last 20 `92256259918212890625`. **Median 1.21s** over three runs. Produced #1103 |

### Proven, not yet written up

Nothing outstanding: Currying, Partial function application, the Y combinator
and Anonymous recursion were written up in batch 3. Function composition is
blocked only by the closure defect fixed in #1168.

---

## Library work required

`math.obl` is **approved** (2026-10-14). These are permanent public API, so the
shape is recorded here rather than decided inside a task.

### `math.obl` -- the work, and every place it has to be registered

A new library is not one file. **Nine places**, seven of them guarded by CI. The
count was written as seven here and corrected after two separate guards caught
what that estimate missed -- which is the useful part of the story, because both
misses would have shipped a library that was half-registered and looked fine.

| Where | What |
|---|---|
| `core/compiler/lib_src/math.obs` | the source; bundle `System.Math`, which was free |
| `core/compiler/build_libs.sh` | one `$OBC` line, in dependency order. The single source of truth -- POSIX `update_version.sh` and the Windows CI runners both drive it |
| `core/lib/math.obl` | built **and committed**; `.obl` files are tracked and the deploy copies them rather than rebuilding |
| `.github/workflows/ci-build.yml` | the `obr code_doc.obe` list |
| `.github/workflows/release-build.yml` | the same list again |
| `core/release/code_doc64.in` | the same list again -- edit the `.in`, never the generated file |
| `core/release/code_doc64.cmd` | **and the generated file too.** Editing only the template leaves the committed output stale until the next Windows deploy regenerates it |
| `tools/lsp/server/doc_json/gen_json.{sh,cmd}` | the same list, twice more -- this is what the editor API index is built from |
| `tools/lsp/server/objk_apis.json` and `tools/lsp/clients/vscode/server/objk_apis.json` | **the regenerated index, both tracked copies.** `sh tools/lsp/server/doc_json/gen_json.sh` rewrites them |

Three guards, and each caught a real omission here rather than a hypothetical:

- `tools/cicd/check_doc_lists.py` -- the five `lib_src` lists. It reported all
  five while they were missing.
- `tools/cicd/check_version_templates.py` -- a generated file against its `.in`.
  This is the one that found `code_doc64.cmd`.
- `tools/cicd/check_api_index.py` -- the editor index's **class set**, not its
  bytes, so whitespace cannot redden CI but a missing class can. It reported
  "2 class(es) the libraries define but the index omits".

Note that `check_doc_lists.py` passing proves nothing about the last two: a
library can be in all five lists and still absent from the index, which is the
state the tree was in at #1050.

One API-hygiene point that is easy to miss and is not guarded at all: **Objeck
statics are public unless marked `private`**, so limb-level helpers are published
API by default. `code_doc` skips private members, so hiding an internal and
keeping it out of the documentation are the same edit.

Tests go in `programs/regression/` as **self-asserting** programs -- exit
non-zero on failure, per `tools/cicd/check_test_exit_codes.py`, which rejects a
test that prints `FAIL` and then returns normally -- and `TESTS.md` is
regenerated with `python gen_manifest.py`.

Assert **values**, not survival. A test that only checks a program did not crash
passes against one that computes a plausible wrong answer, and for anything
type- or overload-selected that is the likelier regression. #1107 is the case in
point: the bug was a wrong `String:Append` overload, and a crash-only test would
have gone green the moment the crash became a wrong number.

### Contents, in dependency order

**1. `BigInt` first** -- `Rational` depends on it, so it lands first or they
land together. Sign and magnitude, little-endian limbs. What a shipped type needs
beyond what the Rosetta task exercised: subtraction, comparison, division and
modulo (which `Rational`'s GCD reduction requires), sign handling in `ToString`,
and parsing from a `String`.

Verify it differentially against Python's integers over random inputs rather than
against hand-picked cases. A big-integer bug is a wrong digit in the middle of a
correct-looking number, and `Int` wraps in silence, so an overflowing column sum
produces a plausible answer rather than a failure. An oracle is the only honest
check.

**2. `Rational` on `BigInt`** -- already written against `Int` and verified
(`6, 28, 496, 8128`), with reduction, the six comparisons, flooring division and
modulo, and `Int` coercion. Backed by `BigInt` its overflow question disappears:
there is nothing to detect and nothing to document. Keep the detected-invalid
behaviour only if an `Int` fast path is wanted for speed, and measure before
adding one.

**3. `Complex`** -- `Float` real and imaginary parts, which is what
`Arithmetic/Complex` asks for. Independent of the other two.

### Why `BigInt` needs no native code -- the measurement

There is **no arbitrary-precision integer anywhere** in the 32 shipped
libraries, and `cipher.obl` exposes no multi-precision internals to build on.
That blocks exact rationals, large factorials, and a long tail of numeric tasks.

The open question was whether it would have to be native to be useful. **It does
not.** Roughly 130 lines of Objeck -- little-endian limbs in base 10^6,
schoolbook multiplication, repeated squaring -- compute 5^262144 correctly in a
**median 1.21 seconds**, for a 183,231-digit result. For comparison, the whole
point of the exercise: that is a pure-Objeck implementation on the JIT, doing
O(n^2) multiplication, landing within a small factor of runtimes whose big
integers are C.

So the proposal is concrete: `BigInt` belongs in the same `math.obl` as
`Rational` and `Complex`, written in Objeck, no native library. What the task
implementation does *not* cover, and a shipped type would need:

- division and modulo, and `ToString` for a sign
- subtraction and comparison (the task needs neither)
- Karatsuba above some limb threshold, if anyone multiplies numbers much larger
  than this; schoolbook is fine to about this size
- base conversion other than decimal

Base 10^6 was chosen so no intermediate can overflow: a limb is under 10^6, a
product of two under 10^12, and `Int` holds 9.2 * 10^18, so column sums stay
exact for operands up to 9.2 million limbs. That margin matters more than it
looks -- `Int` wraps in silence, so an overflowing column would have produced a
plausible wrong answer rather than a failure.

**This also removes the overflow question from `Rational`**: backed by `BigInt`
there is nothing to detect and nothing to document.

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
