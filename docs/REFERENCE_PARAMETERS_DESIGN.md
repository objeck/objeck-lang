# Reference Parameters — Design

Issue: [#1243](https://github.com/objeck/objeck-lang/issues/1243), split out of #1186.

## Why

Every argument is passed by value, so no function can change a caller's local:

```ruby
a := 1; b := 2;
Swap(a, b);   # cannot exchange a and b
```

Code worked around this with holder objects (`IntRef`, a `Ref<T>` cell) or by returning a
record. Research code wants it for the small cases: return a second result
(`q := Divmod(a, b, ref r)`), update an accumulator in a helper, or swap two variables. A
holder in each of those is noise.

## Decisions

### Syntax: `ref` at the parameter and at the call

```ruby
function : Swap(ref a : Int, ref b : Int) ~ Nil {
  t := a; a := b; b := t;
}

Swap(ref x, ref y);
q := Divmod(17, 5, ref r);
```

- **The call site says `ref` too, as C# requires.** A reader sees which arguments a call
  may change without opening the callee.
- **`ref` is contextual.** It means "by reference" only where it stands before a name in a
  parameter list (`ref name :`) or before an argument (`ref x`). A variable or parameter may
  still be called `ref`.

### Semantics: copy in, copy out

A `ref` argument's value goes into the call, and the callee's final value comes back into the
caller's variable after the statement. That is how Swift's `inout` is defined, and the
observable behaviour matches true aliasing for every program that does not pass one variable
twice or read it again inside the same statement.

- **Why not a pointer into the caller's frame.** It would need the VM, both JIT backends and
  the collector to learn a new kind of value: JIT locals live in native stack slots, and a
  reference to an object-typed local would have to be a GC root. Copy-in/copy-out needs none
  of that.
- **How it works:**
  - **Callee.** A `ref a : T` parameter is a one-element `T[]`, a *cell*. Every use of `a` in
    the body, read or write, is element 0 of the cell (`ContextAnalyzer::AnalyzeVariable`).
    A lambda that captures `a` captures the cell, so it sees and makes the same changes.
  - **Caller.** `F(ref x)` becomes three statements:
    - before the call, `#ref_N := [x]`;
    - the call takes `#ref_N`;
    - after the statement, `x := #ref_N[0]`.

    This is done by the parser (`ParseReferenceArgument`, `TakeTrailingStatements`).
- **Cost:** one one-element array per `ref` argument per call, and no change at all on paths
  that do not use `ref`.

### What can be passed

- **A local variable, a parameter or a field** (`ref x`, `ref @count`), including a `ref`
  parameter passed on (`Inc(ref n)` inside a function whose `n` is `ref`). The copy back
  writes to the name, so only a name can be passed.
- **Not an array element or an expression** (`ref a[i]`, `ref F()`): a compile error.
  Writing back to `a[i]` would evaluate `i` twice.
- **A `ref` parameter takes a single value:** not an array and not a function type, and it
  cannot have a default.

### Where a call with `ref` arguments may stand

The copy back runs after the statement. A call with `ref` arguments must therefore be a
statement of its own or the right side of an assignment or declaration. Inside an `if` or
`while` condition, a `for` header or a `return`, the copy would come at the wrong time or
never, so those are compile errors.

### Overloads, libraries

- **Overloads.** A `ref a : Int` parameter's signature is `Int[]`. A method that also takes a
  plain `Int[]` there clashes ("already overloaded"), and a `ref` argument is a `T[]` to
  overload resolution.
- **Libraries.** This needs no format change: a library method's `ref` parameter is recorded
  as `T[]`, its body is compiled with the cell rewrite, and a caller in another binary
  passes `ref x` as a `T[]` cell like any other.
- **One thing is not checked.** Passing `ref x` to a parameter that is a plain `T[]` is
  accepted: the cell goes in and its element is copied back.
- **Types must match exactly.** `ref s` of a `String` variable passes a `String[]`, which a
  `ref b : Base` parameter (`Base[]`) does not take. An exact match is what by-reference
  languages require, since the callee may store a value the caller's type cannot hold.

## Found on the way

An array literal with a subscripted element, `[a[i], a[2]]`, typed the element by the array's
dimension, not the element's, and was rejected ("Invalid class type"). It is a single value
now (`AnalyzeDynamicArray`). This was a #1245 gap, and the `ref` cell of a `ref` parameter
passed on hit it.

## Tests

`ref_parameters.obs` covers:
- swap of `Int`, `Float`, `String`, `Bool` and an object;
- a field;
- `ref` passed on through two calls;
- a second result through `ref` in an assignment and a declaration;
- a lambda that changes a captured `ref` parameter;
- recursion;
- a loop for the JIT;
- the subscripted array literal.

All of it runs in three modes. The `bad_ref_*.obs` files cover:
- a default value;
- an array parameter;
- an expression argument;
- a call in a condition;
- a call in a return.
