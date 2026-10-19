# Array Literals with Expressions — Design

Issue: [#1245](https://github.com/objeck/objeck-lang/issues/1245) (variadic parameters, and
array literals with variables), split out of #1186.

## The problem

Objeck's idiom for "any number of arguments" is an array parameter with an array literal at
the call site, `PrintEach(["a", "b", "c"])`. The issue found two limits:

1. **An array literal accepted only literal constants.** `[x, y]` did not compile, so a call
   over computed values had to build its array by hand.
2. `F([1, 2, 3], -1)` parsed the `-1` as subtraction from the literal. This was already
   fixed by #1184.

## Decision: fix the literal; no varargs declaration for now

With (1) fixed, `PrintEach([a, b + 1, Name()])` covers what a varargs declaration would. It
adds no new syntax, no overload-resolution rules against fixed-arity overloads, and no
spread operator. It also keeps the array visible at the call site, which is where a reader
expects it. A `values : Base...` declaration can come later if real code shows a need that
the literal leaves. The overload, spread and boxing questions the issue lists would be
decided then.

## Rules

- An element is a **literal** when a `,` or `]` follows it. Anything else is an expression:
  a variable, `2 * x`, `-y`, a call or a string concatenation. A literal that has an
  expression element is built when it is evaluated: the array is allocated and each element
  is evaluated **in the order written** and stored.
- An all-literal array is unchanged: it is still copied from the literal pool.
- **Element type:**

  | Elements | Array type |
  |---|---|
  | all the same primitive (`Int`, `Float`, `Char`, `Bool`, `Byte`) | that primitive's array |
  | all objects of one class | that class's array |
  | objects of several classes | `System.Base[]` |
  | objects and primitives | `System.Base[]`, the primitives boxed (`IntRef`, `FloatRef`, ...) |
  | two different primitives (`[1, 2.5]`) | error: there is no implicit numeric promotion |
  | a bare `Nil` | error: it has no type |
  | an array | error, for now; arrays of arrays are #1246 |

  The boxed case is what a format-style API wants: `["id", 7, 2.5]` is a `Base[]` holding a
  `String`, an `IntRef` and a `FloatRef`. Each prints through `ToString` (#1262).
- **One dimension** for expressions. A two-dimensional literal must still be all literals.
  Ragged rows are #1246, and a rectangular block of expressions is rare enough to build with
  `->New[r, c]`.
- `[x, y][1]` subscripts the built array, as it does a pooled literal.

## Implementation

- **Parser** (`ParseStaticArray`): one token of lookahead decides between a literal and
  `ParseExpression`. A literal is followed by `,` or `]`; a negative literal is `-` followed
  by a number, then `,` or `]`.
- **Tree:** `StaticArray::Validate` marks an array dynamic when an element is not a literal.
  A one-dimensional literal mixing literal types is routed the same way.
  `StaticArray::GetType()` then reports the element type the analyzer settled on.
- **Analyzer** (`AnalyzeDynamicArray`): analyzes each element, settles the element type by
  the table above, boxes primitives with the existing `BoxExpression`, and creates a hidden
  local for the array under construction.
- **Emitter** (`EmitStaticArray`) emits:

  ```
  LOAD_INT_LIT n
  NEW_<T>_ARY 1
  STOR_INT_VAR tmp
  (per element)  <element>  LOAD_INT_LIT i  LOAD_INT_VAR tmp  STOR_<T>_ARY_ELM 1
  LOAD_INT_VAR tmp
  ```

  The hidden local is a slot of its own. It is never the shared temp in slot 0 that a `?:`
  or an `&` uses, because an element can contain one (see #1256). It is typed as the array,
  so the collector sees the partly built array as a root while later elements allocate.

## Tests

- `array_literal_expressions.obs`: variables, expressions, negation, calls, strings,
  `Char`/`Bool`/`Float`, subscripting, mixed objects, boxing, evaluation order, and a
  200,000-iteration allocation loop (JIT, GC).
- One `bad_array_literal_*.obs` per error.
- `rosetta_variadic_function.obs`: header corrected.
