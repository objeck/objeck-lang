# Jagged Arrays — Design

Issue: [#1246](https://github.com/objeck/objeck-lang/issues/1246), split out of #1186.

## Why

Rectangular arrays exist (`Int[,]`), but rows of different lengths could not be declared,
built or passed. Code boxed each row (`IntArrayRef[]`, `Vector<IntArrayRef>`), which costs
an object per row and an unwrap on every access. Ragged data is everyday work in research
code: token sequences of different lengths, adjacency lists, triangles, batches.

```ruby
rows := Int[]->New[3];          # three rows, each Nil until set
rows[0] := [1, 2, 3];
rows[1] := Int->New[5];
rows[2] := [x, y];
total := rows[0][1] + rows[2][0];

function : Sum(rows : Int[][]) ~ Int { ... }
triangle := [[1], [1, 1], [1, 2, 1]];   # ragged, so Int[][]
```

## Decisions

### Syntax

| Form | Meaning |
|---|---|
| `T[][]` | a one-dimensional array whose elements are `T[]` |
| `T[][][]` | an array of `T[][]`, and so on |
| `T[]->New[n]` | allocate `n` rows, each `Nil` |
| `rows[i]` | a row, a `T[]` |
| `rows[i][j]` | an element |
| `[[1, 2, 3], [4]]` | a ragged literal is a `T[][]`. A rectangular one stays `T[,]`, as today. |
| `[a, b]` with `a`, `b : T[]` | a `T[][]` (array literals with expressions, #1245) |

Each level is one-dimensional in this phase. `Int[,][]` (rows that are themselves
rectangular) is not supported: no task in the batch needs it, and it would tie the syntax
to a choice about which bracket is outer.

### Representation

At run time a `T[][]` is an object array whose elements are `T[]` arrays. The VM, both JIT
backends and the collector already handle this:

- An object-array local or field is `OBJ_ARY_PARM`, so the collector marks each element.
- An element without a class mapping (an array) takes `CheckObject`'s array path. That path
  marks the array and, for an int or object array, scans its elements.

So deeper nesting is covered too, and **no VM change is needed**.

In the compiler, a jagged type is a one-dimensional array of a **row type**, a class name
that only the compiler uses: `Int[][]` is `$Int[]` with dimension 1, and `Int[][][]` is
`$Int[][]` with dimension 1. Everything that already works for object arrays then works
unchanged: allocation, `->Size()`, `each`, parameters, fields, return types, signature
encoding (`o.$Int[]*`) and memory types. The row type turns back into the real array type in
the few places an element is read or written:

- **Reading an element.** `rows[i]` has the row type. `RowElementType` turns `$Int[]` back
  into `Int[]`, and `$Int[][]` into `$Int[]` with dimension 1.
- **Writing an element.** `rows[i] := arr` accepts a value whose type is the row's element
  type.
- **Literals.** A ragged literal, or one whose elements are arrays, has the row type as its
  element type.

### Assignment through two subscripts

`rows[i][j] := v` is rewritten by the parser to `#ary := rows[i]; #ary[j] := v`. This is the
hidden-local form already used for an indexed call result as a target
(`ParseCallElementOperation`). The row is read before `v` is evaluated.

### Chained indexing

`rows[i][j]` is a variable with more than one index group. The groups are emitted
innermost first, then the variable. Each element load then takes the next group's index from
below the array it just loaded:

```
<j>  <i>  LOAD rows  LOAD_INT_ARY_ELM 1  LOAD_INT_ARY_ELM 1
```

A store is the same with the value first: `<v> <j> <i> LOAD rows LOAD_INT_ARY_ELM 1
STOR_INT_ARY_ELM 1`. This means **index expressions are evaluated innermost group first**
(`j` before `i`). That only shows when two index expressions have side effects, and it is
documented. Keeping written order would need a temporary per group.

## Out of scope

- Serializing jagged arrays (`ObjectSerializer`). This phase does not cover or test it.
- `Int[,][]` and other rectangular levels.
- Reflection's view of a jagged type: it reports the row type's name.

## Tests

`jagged_arrays.obs` covers declaration, allocation, ragged literals, chained reads and
writes, rows of objects (`String[][]`), parameters and return values, `each`/`->Size()`,
three levels, and a loop that allocates enough rows to collect (JIT, GC, three modes). It
also checks that rows survive collection under `--gc-threshold`. The `bad_jagged_*.obs`
files cover a wrong element type and a wrong number of index groups.
