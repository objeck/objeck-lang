# Array Literals with Expressions — Design

Issue: [#1245](https://github.com/objeck/objeck-lang/issues/1245) (variadic parameters, and
array literals with variables), split out of #1186.

**Status:** implemented. Array literals with expressions shipped in PR #1264 (commit `3c6f0c573a`); two later fixes, `6fd7265dc3` (`Nil` elements, enum literals) and `620f328676` (string-literal pairing across elements), are folded into the rules below. Jagged `T[][]` rows (the "an array" row of the table) shipped in PR #1265, see [JAGGED_ARRAYS_DESIGN.md](JAGGED_ARRAYS_DESIGN.md). **Not implemented:** a `values : Base...` varargs declaration (the decision below).

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
  | arrays, each `T[]` of one type | an array of rows, `T[][]` (#1246); every row must be one-dimensional and of the same array type |
  | arrays mixed with single values | error: "An array literal cannot mix rows and single values" |
  | enum values of one enum | that enum's array. An enum value is an `Int` at run time, so it never boxes into `Base[]`: enums mixed with other classes or primitives are an error |
  | objects (or rows) and `Nil` | `Nil` is an empty slot; the type comes from the other elements |
  | `Nil` among primitives or enums, or `Nil` alone | error ("cannot hold Nil", "cannot be Nil alone; give the array a type with ->New[..]") |

  The boxed case is what a format-style API wants: `["id", 7, 2.5]` is a `Base[]` holding a
  `String`, an `IntRef` and a `FloatRef`. Each prints through `ToString` (#1262).
- **One dimension** for expressions. A two-dimensional literal must still be all literals;
  the analyzer reports "A multi-dimensional array literal must hold only literals; build it
  with ->New[..]". A rectangular block of expressions is rare enough to build with
  `->New[r, c]`; ragged rows of arrays are the jagged form above.
- **Only inside a method or function.** The hidden local needs a symbol table, so an
  expression literal elsewhere is an error ("must be inside a method or function").
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

  `<T>` is `INT`, `FLOAT` or `CHAR`; `Bool` and `Byte` use `NEW_BYTE_ARY`; an object, string,
  function or row element uses `NEW_OBJ_ARY` and stores with `STOR_INT_ARY_ELM`.

  The hidden local is a slot of its own. It is never the shared temp in slot 0 that a `?:`
  or an `&` uses, because an element can contain one (see #1256). It is typed as the array,
  so the collector sees the partly built array as a root while later elements allocate.

## Limitations

- Elements are evaluated in written order, but there is no implicit numeric promotion, so
  `[1, 2.5]` is an error rather than a `Float[]`.

## How to check

Compile and run `programs/regression/array_literal_expressions.obs`; each `bad_array_literal_*.obs`
must fail to compile with the message in the table. From a build tree:
`obc -src programs/regression/array_literal_expressions.obs -dest t.obe && obr t.obe`.
`obc ... -asm` writes the emitted sequence to `t.obm`.

## Tests

- `array_literal_expressions.obs`: variables, expressions, negation, calls, strings,
  `Char`/`Bool`/`Float`, subscripting, mixed objects, boxing, evaluation order, and a
  200,000-iteration allocation loop (JIT, GC).
- One `bad_array_literal_*.obs` per error (`2d_expressions`, `enum_mix`, `mixed_primitives`, `nil_element`); `core_array_literal_nil_element.obs` and `core_array_literal_mixed_strings.obs` cover the accepted `Nil` and string cases, and `gc_minor_str_array_literal.obs` the collector.
- `rosetta_variadic_function.obs`: header corrected.
