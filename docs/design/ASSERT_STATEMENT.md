# The `assert` statement

Status: implemented on `feat/assert-statement` (draft PR). Syntax sugar only: the parser rewrites
the statement into an `if`, so the context analyzer, the emitter, the optimizer, the VM and the JIT
are unchanged.

## What it is for

Regression programs check a result like this:

```
if(x + 1 <> 5) {
  "  FAIL: x plus one is five"->PrintLine();
  Runtime->Exit(1);
};
```

Three lines per check, no file or line, and no values. The runner (`run_regression.cmd`,
`run_regression.sh`) decides a test from its exit code alone, so a check that forgets the
`Runtime->Exit(1)` silently passes (`tools/cicd/check_test_exit_codes.py` exists because 125 tests
did). `assert` is the same check in one statement:

```
assert x + 1 = 5 : "x plus one is five";
```

When it fails the program prints

```
FAIL: t.obs:4: assert x + 1 = 5 : x plus one is five
      left = 4, right = 5
```

and exits with code 1. A line that starts with `FAIL:` and exit code 1 are the convention the
runners and CI scripts already read, so nothing there changes.

## Syntax

```
assert <Bool expression> [ : <String expression> ] ;
```

* The label is optional and is any `String` expression (`"i is {$i}"` works). It is evaluated only
  when the check fails.
* `assert` is a *contextual* word, as `try` is (#1247). It starts an assertion only where a name
  could not continue the statement: when the next token begins an expression (a name, a literal,
  `true`, `false`, `Nil`, `not`), or is a `(` whose matching `)` is followed by a binary operator
  or the label's `:`. So `assert := 5;`, `assert : Bool;`, `assert->Foo();`, `assert[0] := 1;`,
  `assert(x);` (a call to a method named `assert`) and `Runtime->Assert(...)` compile as before.
  The one thing that changes meaning is an identifier `assert` followed by something that was a
  syntax error anyway.
* An assertion is a statement, not an expression. It cannot be the value of anything.

## Lowering

```
assert L op R : "label";          // op is = <> < > <= >=
```

becomes

```
#assert_0 := L;                   // only for an operand that is not read-only
if((#assert_0 op R) = false) {
  System.AssertCheck->Fail("file:line", "L op R", "label", "" + #assert_0, "" + R);
};
```

* An operand that is only read (a variable, a literal, arithmetic over them) is not held in a
  local: the failure branch re-reads it. Any other operand (a call, an indexed read, `++x`, a cast)
  is evaluated once into a hidden local, so a call is never made twice. A method has 768 bytes of
  locals (`LOCAL_SIZE`), so this keeps an assertion-heavy `Main` from running out of them.
* A `Nil` operand is not shown (it has no type to hold) and prints as `Nil`.
* An expression that is not a comparison (`assert ok;`, `assert a & b;`) shows no operand values.
* Operand values are converted by the compiler's ordinary `"" + value` rule, so numbers, `Bool`,
  `Char` and `String` print as themselves and an object prints through its `ToString()`. As
  everywhere else, that call on a `Nil` object is a Nil dereference; compare against `Nil`
  directly (`assert o <> Nil`) before asserting on an object's members.
* `=` keeps its existing meaning for every type (a `String` `=` is the comparison it always was).
* The source text comes from the scanner's line and column of the expression's first token and of
  the token that ends it (`Scanner::GetSourceLine`); an expression over several lines is joined
  with single spaces.

## The library side: `System.AssertCheck` (lang.obs)

| Member | Meaning |
|---|---|
| `AssertCheck->Fail(where, source, note, left, right)` | called by the lowered code: counts the failure, prints the `FAIL:` line and the operand line, then `Runtime->Exit(1)` unless `KeepGoing(true)` |
| `AssertCheck->KeepGoing(Bool)` | count failures instead of stopping at the first |
| `AssertCheck->Failures()` | the failure count |
| `AssertCheck->Finish()` | exit with code 1 (and one `FAIL: n assertion(s) failed` line) if any assertion failed |

Fail-fast is the default because a failed check usually invalidates what follows and because a test
that forgets `Finish()` must still exit non-zero.

`lang.obl` is the only library that changes. Programs link library classes on demand, so a program
that does not use `assert` links exactly the classes it linked before (checked: byte-identical
`.obe`).

## Why not a new tree node

An `Assert` node would need handling in the analyzer (Bool check, type of the operands), the
emitter and the formatter, and the operand-to-string conversion by type, which the analyzer already
does for `"" + value`. The rewrite reuses all of it and cannot change the code generated for any
program that does not contain the word.

## Limits

* The hidden locals count against the 768-byte local space (8 bytes each, one per non-read-only
  comparison operand).
* `assert` in the alternative (indentation-light) syntax mode takes the same form; the statement
  ends at its `;`.
* A label that is not a `String` is the usual type error, reported at the label.

## Tests

* `programs/regression/assert_statement.obs`: every shape that must hold, plus `assert` as a
  variable, a method name and `Runtime->Assert`.
* `programs/regression/check_assert_statement.py`: compiles and runs the fixtures in
  `programs/regression/assert_statement/` and checks the output and the exit code of each failing
  shape, and the compile errors (with line numbers) of the `bad_*` ones. Wired into `ci-build.yml`
  and `ci-windows.yml`.
