# Choosing a collection

Which of the `Collection` classes to reach for, what each operation costs, and what each
`Remove` hands back. Everything below was read from
[`core/compiler/lib_src/gen_collect.obs`](../core/compiler/lib_src/gen_collect.obs) (the
line numbers are where the class starts); nothing here changes the API. Compile with
`-lib gen_collect` (every build gets it; see `core/compiler/build_libs.sh`).

## At a glance

| Need | Use | Why |
|---|---|---|
| A growable array, index access | `Vector` | O(1) `Get`/`Set`/`AddBack`; also the type every `GetKeys`, `GetValues` and `Find` on a multi-map returns |
| A queue you add to and drop from both ends, or splice at a cursor | `List` | O(1) at both ends and at the cursor; no index access |
| Key to value, keys in sorted order | `Map` | O(log n); iteration is in key order |
| Key to value, order does not matter | `Hash` | O(1) expected |
| Unique values, sorted | `Set` | `Map` underneath, O(log n) |
| Unique values, order does not matter | `HashSet` (or `SetHash`) | `Hash` underneath, O(1) expected |
| Several values per key, keys sorted | `MultiMap` | a `Map` of `Vector`s |
| Several values per key, keys unordered | `MultiHash` | a `Hash` of `Vector`s |
| Two related values with no class of their own | `Collection.Tuple.Pair` | immutable, any types |
| A pair that can sit in a sorted collection, or that you must change | `Collection.Pair` | first element must be `Compare` |

If you are unsure between a tree and a hash: take the `Hash` family unless you need the keys
in order (`Map`, `Set`, `MultiMap`). A key class must implement `Compare` for either, and
`HashID` should agree with `Compare` for the hash family (`String`, `IntRef` and the other
reference types in `lang` do).

`Stack`, `Queue` and `Heap` (a binary heap) are in the same file for the cases their names
describe; `IntVector`, `FloatVector`, `IntMap`, `IntSet` and `BitSet` hold unboxed numbers.

## What `Remove` returns

The three families disagree, and a caller that tests the result needs to know which it has.

| Class | Method | Returns | When nothing was removed |
|---|---|---|---|
| `Vector` | `Remove(i)` | the element removed | `Nil` (index out of range) |
| `Vector` | `RemoveBack()` | the element removed | `Nil` (empty) |
| `List` | `Remove()`, `RemoveFront()`, `RemoveBack()` | `Nil`, always | no signal; check `IsEmpty()` first |
| `Map`, `Hash` | `Remove(key)` | `Bool` | `false` (key not present, or `Nil`) |
| `Set`, `HashSet`, `SetHash` | `Remove(value)` | `Bool` | `false` |
| `MultiMap`, `MultiHash` | `Remove(key)` | `Bool`; removes **every** value of the key | `false` |

Two consequences. A `Vector` hands you back what it removed, so `Remove` on a `Vector<IntRef>`
cannot tell you "nothing there" from "a `Nil` was stored there"; use `Size()` or `Get` first when
`Nil` is a legal element. And a `List` removes at its *cursor*: `RemoveFront` and `RemoveBack`
move the cursor to that end first, so they disturb an iteration in progress that uses
`Next()`/`Get()` on the same list (use `ForwardIterator()` for a traversal that must survive).

## The classes

### `Vector<H>` (gen_collect.obs:5113)

A growable array. It grows by half again (at least four slots) when full, so `AddBack` is
amortised O(1).

| Operation | Cost |
|---|---|
| `Get(i)`, `Set(v, i)`, `First()`, `Last()`, `Size()` | O(1); `Get` returns `Nil` and `Set` returns `false` for a bad index |
| `AddBack(v)`, `RemoveBack()` | O(1) amortised |
| `Remove(i)` | O(n): later elements are shifted down |
| `FindFirst(f)`, `FindIndex(f)`, `Any`, `All`, `Count`, `Filter`, `Map`, `Reduce` | O(n) |
| `Sort(compare)` | O(n log n), stable (merge sort) |

There is no insert-at-index and no `AddFront`; use a `List`, or build a new `Vector`, if you
need either. `Reserve(n)` avoids regrowth when the size is known.

### `List<H>` (gen_collect.obs:4350)

A doubly linked list with one built-in cursor (`Rewind`, `Forward`, `Next`, `Previous`, `More`,
`Get`, `Set`) and independent `ForwardIterator()`/`BackwardIterator()` objects.

| Operation | Cost |
|---|---|
| `AddBack`, `AddFront`, `RemoveBack`, `RemoveFront`, `Front`, `Back`, `Size` | O(1) |
| `Insert(v)` (after the cursor; returns `Bool`), `Remove()` (at the cursor) | O(1) |
| `Get()`, `Set(v)`, `Next()`, `Previous()` | O(1), at the cursor |
| anything by position or by value | not provided; walk it, O(n) |

### `Map<K, V>` (gen_collect.obs:6776)

An AA tree (`Skew`/`Split` rebalancing on a level per node), so it stays balanced.

| Operation | Cost |
|---|---|
| `Insert`, `Find`, `Has`, `Remove` | O(log n) |
| `GetKeys`, `GetValues`, `GetKeyValues`, `Each`, `ToString` | O(n), in ascending key order |

`Insert` of an existing key replaces its value. `Find` returns the value, or `Nil` when absent
(so it cannot tell a missing key from a stored `Nil`; use `Has`). A `Nil` key is ignored by
`Insert`. The name is the usual trap: `Map` is the *ordered tree*. It is not a hash map.

### `Hash<K, V>` (gen_collect.obs:2020)

Open addressing with linear probing over parallel key, value and hash arrays. It doubles when it
is 70% full and has no upper limit; `Remove` uses backward-shift deletion, so deletions leave
no tombstones and do not slow later lookups.

| Operation | Cost |
|---|---|
| `Insert`, `InsertIfAbsent`, `Find`, `Has`, `Remove` | O(1) expected; an `Insert` that triggers the doubling is O(n) |
| `GetKeys`, `GetValues`, `GetKeyValues` and the other whole-table calls | O(capacity) |

Iteration order is unspecified, but `GetKeys`, `GetValues` and `GetKeyValues` agree with each
other. Keys of different classes are never equal. `Find` returns `Nil` for a missing key.
`InsertIfAbsent` returns `true` when it inserted. `SetCapacity`/`Resize` are hints for
pre-sizing; the table grows on its own. A lookup changes nothing in the table, so concurrent
readers are safe; a writer still needs a lock.

### `Set<K>` (gen_collect.obs:7465)

A `Map<K, Base>` underneath, so it has the `Map` costs and ordering: `Add`, `Insert`, `Has` and
`Remove` are O(log n) and `GetKeys`/`ToVector`/`ToArray` return ascending order. `Add` returns
`true` when the value was new; `Insert` returns nothing. `Union`, `Intersect`, `Difference`,
`SymmetricDifference`, `IsSubset` and `IsSuperset` return new sets and leave both operands alone.

### `HashSet<H>` (gen_collect.obs:1430) and `SetHash<K>` (gen_collect.obs:7894)

`HashSet` wraps a `Hash<H, BoolRef>`, so `Add`, `Has` and `Remove` are O(1) expected and the
order is unspecified. `SetHash` wraps a `HashSet` and forwards to it, with the method names
`Set` uses (`Insert`, `Add`, `Has`, `Remove`, the set algebra). They behave the same; `HashSet`
also has `Insert` and `GetKeys` so code can move between the three. Pick one name and keep to it:
`HashSet` is the implementation, `SetHash` is the spelling that parallels `Set`. Adding a `Nil`
returns `false`.

### `MultiMap<K, V>` (gen_collect.obs:8293) and `MultiHash<K, V>` (gen_collect.obs:8639)

A `Map` (or `Hash`) from key to `Vector<V>`. `Insert(k, v)` appends to the key's vector,
creating it on the first value. Costs are those of the underlying map plus an O(1) append.

- `Find(k)` returns **the stored `Vector`**, not a copy, or `Nil` when the key has no values.
  Edits to it are edits to the multi-map.
- `Remove(k)` drops the key and all its values. There is no call that removes one value.
- `Size()` counts distinct keys; `TotalSize()` counts values and walks every key, O(keys).

### The two `Pair` classes

They share a name in different bundles, which is easy to mix up.

| | `Collection.Tuple.Pair<A, B>` (gen_collect.obs:24) | `Collection.Pair<F : Compare, S>` (gen_collect.obs:1849) |
|---|---|---|
| Mutable | no (`GetFirst`, `GetSecond` only) | yes (`SetFirst`, `SetSecond`) |
| Element types | any | the first must implement `Compare` |
| Implements `Compare` / `Stringify` | neither (so it cannot be a `Map` or `Hash` key or a `Set` member) | both; `Compare` looks at the first element only |
| `ToString` | `[a,b]` | `{a,b}` |
| Where the library uses it | `Hash->Dict` and `Map->Dict` take a `Vector` of them; `Triplet`, `Quartet` and `Quintet` are the same family | what `GetKeyValues()` on `Map` and `Hash` returns |

`Collection.Pair->Compare(x)` compares its first element with `x`, so the documented call is
`a->Compare(b->GetFirst())`. Its `HashID()` is the first element's instance ID rather than a
hash of its value, so do not use a `Collection.Pair` as a `Hash` key and expect equal pairs to
collide. For a value type that crosses a library boundary, define a small class instead.

## Recipes

- **Counting words, order irrelevant:** `Hash<String, IntRef>`; `Find` then `Insert` the new count.
- **A report sorted by key:** `Map`, then `GetKeyValues()` (already ascending).
- **Unique ids you only test for membership:** `HashSet<IntRef>`.
- **Group rows by a column:** `MultiHash<String, Row>`, then `Find(key)` for the group.
- **A work queue:** `List` (`AddBack`, then `Front` and `RemoveFront`), or `Queue`.
- **Take the first n of a sorted result:** `Vector->Sort(...)`, then `Limit(n)`.
