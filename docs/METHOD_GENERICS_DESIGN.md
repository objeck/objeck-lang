# Method-Level Generics — Design

Issue: [#1242](https://github.com/objeck/objeck-lang/issues/1242), split out of #1186.

## Why

Type parameters existed only on classes. A generic helper needed a throwaway generic class,
instantiated at every call with explicit type arguments
(`Swapper->New()<String>->Swap(...)`). Research code is full of small generic helpers:
argmax over a collection, swap, pick-first, map over a vector.

```ruby
function : Max<T : Compare>(values : Vector<T>) ~ T { ... }
function : Swap<T>(a : T[], i : Int, j : Int) ~ Nil { ... }
function : First<T>(a : T[]) ~ T { return a[0]; }

best := Max(scores);        # T inferred: scores is a Vector<IntRef>, so best is an IntRef
name := First(names);       # a String
```

## Decisions

### Syntax

The type parameters follow the method name, exactly as on a class:
`Name<T, U : Bound>(...)`. Bounds, compound bounds and F-bounds parse as they do for
classes.

### Inference, not explicit type arguments

Type arguments are **inferred from the call's arguments**, which is what makes the feature
worth having. A parameter typed `T`, `T[]` or `C<..., T, ...>` (any generic class) binds `T`
to the argument's class.

- If a type parameter is bound twice, both bindings must be the same class.
- A type parameter that no argument determines is its bound.
- An explicit form such as `Max(v)<IntRef>` is left for later. It would mean deciding how it
  relates to the `<...>` suffix calls already use for class type arguments.

### Code generation: erasure

The same model as class generics: one body, and the type parameters are erased.

- **Inside the method**, `T` is its bound (the first one for a compound bound), or
  `System.Base` when it has none. So the body type-checks against exactly what the bound
  allows. The parser makes the substitution when it builds each type, and marks the type with
  the parameter it came from.
- **At a call**, the marked parameter and return types are re-instantiated with the inferred
  bindings:
  - the arguments are matched against `Vector<IntRef>`, not `Vector<Compare>`;
  - each binding is checked against its bound, so a non-`Compare` argument to
    `Max<T : Compare>` is a compile error;
  - the call's value has the return type with `T` substituted, so `best` above is an `IntRef`
    with no cast.
- **At run time** nothing changes: the objects are the concrete ones, and the signature is
  the erased one.

### Scope of phase 1

| Target | Phase 1 |
|---|---|
| functions and methods of the program's own classes | yes |
| type parameters on constructors | no (a class's own type parameters cover that) |
| primitive arguments (`Max(3, 5)`) | no: a type parameter takes an object, as class generics do (`IntRef`) |
| library methods (`Vector->Map<R>`) | **phase 2** (shipped): the `.obl` keeps the type-parameter marks |

## Phase 2: library methods (shipped)

A library's signatures carry the bound a type parameter was erased to (`Max<T : Compare>`
reads `Max(o.Collection.Vector|o.System.Compare)`), not the parameter, so a caller in another
binary could not infer anything: a library generic returned its bound.

- **The `.obl` keeps the marks.** After each method's parameter names, a library writes where
  its own type parameters stand, as `(path, name)` pairs. `p<i>` is a parameter and `r` the
  return, followed by `.g<i>` for a generic argument, `.f<i>` for a function type's parameter,
  and `.r` for a function type's return (`Type::GetMethodGenericMarks`). The linker applies
  them to the parsed types (`LibraryMethod::SetGenericMarks`). Paths rather than positions mean a
  mark can never land on the wrong part of a type. The library magic is `0xffb8`, and a
  `0xffb7` library is refused with a rebuild message.
- **Inference at a library call.** `InferLibraryMethodGenerics` mirrors the program path,
  and `UnifyMethodGeneric` now matches function types: `(A, ..) ~ R` against the argument's
  function type, parameters and return.
  - A typed lambda gives its declared type, and so does a function value.
  - An inferred lambda has no type until it is built. `DerivedLambdaFunction` builds it with
    the parameter's bound, then binds the parameter from what the lambda's body returns
    (`LambdaResultType`). Such a lambda must return an object: a primitive is not boxed into
    the bound, as it is not in an assignment to a `Base`.
- **Matching.** Resolving a class's own type arguments rebuilds a function type from its
  encoded name, which spells the bound. So the bindings are applied afterwards, walking the
  marked original alongside (`SubstituteAlong`), and the function type is re-encoded.
- **The call's value** has the return type with the bindings substituted, before the class's
  own type arguments are mapped. `words->Map(..)->Get(0)` therefore sees an `IntRef`.
- **APIs:** `Vector`, `List` and `CompareList` take `Map<R>((H) ~ R) ~ C<R>`; `CompareList`'s is
  `Map<R : Compare>`. A map that keeps the element type behaves as before. `Reduce` keeps
  its shape.

Found on the way: 34 doc examples in `gen_collect.obs` used `\(x : T) ~ R { return e; }`, which
does not parse. They are now `\(T) ~ R : (x) => e`, and every lambda example in the file
compiles.

Tests: `method_generics_library.obs` (`Map<R>` with typed, inferred and function-value
arguments, chained, in a loop), `bad_method_generic_library_bound.obs`, and
`check_lib_generic_methods.py`, which builds a library of generic functions and methods and
links a program against it in three modes.

## Implementation

1. **Parser:**
   - `ParseMethod` parses `<...>` after the name with the existing `ParseGenericClasses`.
   - While the method is parsed, `ParseType` maps each of those names to its bound type
     marked with the parameter name (`Type::SetMethodGeneric`). That covers the signature,
     locals, casts and generic arguments (`Vector<T>`).
2. **Analyzer** (`ResolveMethodCall`, program classes): for a candidate with marked types:
   - infer the bindings from the arguments (`InferMethodGenerics`);
   - substitute them into the parameter types the arguments are matched against;
   - check each binding against its bound.

   The bindings stay on the call so that the call's value takes the substituted return type.
3. **Emitter:** unchanged.

## Tests

`method_generics.obs` covers:
- `T` from a plain argument, an array and a generic class;
- a bound (`T : Compare`), and the inferred return type used without a cast;
- two type parameters;
- a generic method calling another;
- instance and static methods;
- a loop for the JIT, in three modes.

The `bad_method_generic_*.obs` files cover:
- a bound the argument does not meet;
- conflicting bindings;
- a primitive argument.
