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
| library methods (`Vector->Map<R>`) | **phase 2**: the `.obl` must keep the type-parameter marks |

Phase 2 would also reshape the library APIs the issue names (`Vector->Map`, `Reduce`).

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
