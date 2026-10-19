# Named Arguments — Design

Issue: [#1244](https://github.com/objeck/objeck-lang/issues/1244), split out of #1186 (Rosetta batch 3).

## Why

Trailing default values already work:

```ruby
function : Train(data : Matrix, epochs : Int := 10, rate : Float := 0.01, verbose : Bool := false) ~ Model
```

But a caller can only drop arguments from the end. To turn on `verbose` they must restate
`epochs` and `rate`, and a reader of `Train(data, 10, 0.01, true)` cannot tell which number
is which. Research code is full of calls like this: a model, an optimizer, a sampler, each
with a handful of knobs that have sensible defaults. Named arguments fix both problems:

```ruby
Train(data, verbose: true);
Train(data, rate: 0.1, epochs: 50);
```

## Decisions

### 1. Syntax: `name: value`

The same form as Swift, Kotlin and C#. Spacing is free (`name : value` also parses).

| Rejected | Why |
|---|---|
| `name = value` | `=` is equality in Objeck: `F(x = 1)` is already a valid Bool argument. |
| `name := value` | Reads as an assignment to a variable called `name` in the caller. |

An argument that starts with a plain identifier followed by `:` has no meaning today.
A ternary's `:` follows its `?` (`c ? a : b`), and a lambda's follows its `~ Type`. So the
form is unambiguous: the parser checks one token of lookahead at the start of each argument.
Instance variables (`@x:`) are not parameter names and are rejected.

### 2. Rules

- **Order.** Positional arguments come first and named ones follow. A positional argument
  after a named one is an error.
- **Named arguments in any order**, each parameter at most once. A parameter given both
  positionally and by name is an error.
- **Any parameter may be named**, including ones without a default (`Point->New(y: 2, x: 1)`).
  Every parameter without a default must be supplied, positionally or by name.
- **Unknown names** are an error that lists the method's parameter names.
- **Evaluation order is the order written at the call site.** Arguments are evaluated left
  to right as written and then bound to their parameters. Today's positional calls already
  evaluate left to right, and a reordering must not move a side effect.

### 3. Overloads

For each overload of the name, the call is mapped onto its parameters by name. Overloads the
call cannot be mapped onto (unknown name, missing required parameter) are dropped. The rest
go through ordinary resolution, with the reordered arguments. More than one survivor that
type-matches equally well is the existing ambiguity error.

Only *original* methods are mapped. The alternates generated for trailing defaults (below)
share the original's parameter names, and mapping them as well would give one call several
equivalent targets.

### 4. Skipping a parameter that has a default

Default values are implemented by **alternates**. `F(a, b := 1, c := 2)` also gets `F(a)` and
`F(a, b)`. Each is a thin forwarder that assigns the missing defaults in the callee's own
context and calls `F(a, b, c)` (`GenerateParameterMethods`, `IntermediateEmitter` for
`IsAlt()`). A named call whose supplied set is a prefix (`F(a, b: 5)`) is just that alternate.

A call that skips a middle parameter (`F(a, c: 5)`) needs a forwarder for the set `{a, c}`,
and a call that reorders (`F(c: 5, a: 1)`) needs its arguments put back in parameter order.
Generating a forwarder for every subset is exponential, and same-typed subsets would collide
as overloads. So:

- The parser records which method **names** are called with named arguments, and with which
  argument shape (positional count and named set).
- Before analysis, for each program method of such a name, the compiler generates a forwarder
  for each recorded shape that maps onto it. The forwarder **takes the arguments in the order
  the call writes them**. Its body assigns the defaults it does not receive in the callee's
  context, as an alternate's does, and calls the original with every parameter in its place.
  So the call site pushes its arguments as written, and the written evaluation order holds
  without temporaries.
- Every named call binds to a forwarder, even one that names its arguments in parameter
  order. The binding is the same for each overload, and the forwarder is one small frame
  that the s3 inliner can remove.
- Forwarders get a name no source can spell (`F$n<mask>`). They never collide with an
  overload, and a stack trace shows the original name.

A program with no named calls generates nothing, so every library and every existing program
compiles **byte-identically**.

Defaults are still evaluated in the callee, as today. They are never copied to the call site,
where names in a default expression would resolve against the caller.

### 5. What it applies to

| Target | Phase 1 | Notes |
|---|---|---|
| Functions and methods of classes in the program being compiled | yes | |
| Constructors (`New`) | yes | the most common place for options |
| Virtual and interface methods | **no**: later | they cannot have defaults; an interface cannot hold the concrete forwarder |
| Library methods (`String->Pad`, `Hash->New`, ...) | **no**: phase 2 | see below |
| Function values and lambdas | no | a function type has no parameter names |
| Primitive receivers (`v->Pow(...)`) | no | library functions; parameter 0 is the receiver |

A named argument to a target that does not support it is a compile error that says why,
not a silent positional call.

### 6. Phase 2: library methods

A release `.obl` keeps a method's parameter **types** but not their names. The names are
written only in a debug build (`Library::LoadMethods`, `is_debug`), and a caller cannot
generate a forwarder inside another binary. Phase 2 needs:

1. Parameter names in every `.obl`. This is a format change, so the linker version check
   must accept the old layout or the libraries must be rebuilt together, as they always are.
2. One **mask forwarder** per library method with two or more defaults. It takes every
   parameter plus an `Int` mask of the supplied ones, and assigns each missing default under
   `if((mask and bit) = 0)`. The caller passes a zero, `0.0` or `Nil` placeholder for each
   skipped parameter. That is one extra method per such method, not one per subset.

Once phase 2 lands, **standard-library parameter names become public API**: renaming one
breaks named callers. That is accepted, as in Kotlin, C# and Swift. Library reviews should
treat a parameter rename like a signature change.

## Phase 1 plan

1. **Parser:** `ParseExpressionList` (calling parameters only) accepts `IDENT ':' expression`
   and records the name next to the expression in the `ExpressionList`. It enforces positional
   before named. It records the call shape for forwarder generation.
2. **Context analysis:**
   - In `AnalyzeMethodCall` (program classes), `BindNamedArguments` maps a named call onto
     each original overload and binds it to the forwarder for its shape. Several overloads
     taking one call is an ambiguity error that asks for positional arguments.
   - Give the errors above for library targets, function values and bad names. A guard in
     `AnalyzeMethodCall` reports any named call no path consumed, so names are never dropped
     silently.
3. **Forwarder generation:** `GenerateNamedForwarders` is `GenerateParameterMethods`
   generalized from "every suffix" to "this order". Each forwarder gets its own copy of each
   default assignment's variable: `Declaration::Copy()` shares the original's, which the
   alternates can afford because their slots match the original's and a forwarder's do not.
   The emitter loads the original's parameters from the forwarder's own slots.
4. **Tests:** `named_arguments.obs` covers reordering, skipping, constructors, evaluation
   order, overloads and the JIT (three modes). A `bad_named_argument_*.obs` file covers each
   error. `rosetta_optional_parameters.obs` gets its header corrected and a named-argument
   form of the task.
5. **Docs:** a FEATURES.md section.

## Found on the way

- `reverse` is a reserved word, so `reverse : Bool` is not a parameter name (the error is
  "Expected identifier").
- A lambda as a default value (`f : (Int) ~ Int := \(Int) ~ Int : (x) => x`) does not parse
  inside a parameter list. Its parameter commas end the declaration.

## Not in scope

- Variadic parameters (#1245).
- Default values on non-trailing parameters. The existing rule ("only trailing parameters
  may have default values") stays: positional calls depend on it.
