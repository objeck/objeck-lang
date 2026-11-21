# Choosing a collection

Which of the `Collection` classes to reach for, what each operation costs, and what each
`Remove` hands back. Everything below was read from
[`core/compiler/lib_src/gen_collect.obs`](../core/compiler/lib_src/gen_collect.obs) (the
line numbers are where the class starts, as of 2026-11-18; search for `class Name` if they have
drifted); nothing here changes the API. Compile with
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
| Both ends O(1) plus index access (FIFO queue, sliding window) | `Deque` | ring buffer; `AddFront`/`RemoveFront` are O(1), unlike `Vector` |
| Count occurrences of keys (word counts, histograms) | `MultiSet` | `Hash` of counts; `Count`, `MostCommon(n)` |
| Group items into disjoint sets, connected components | `UnionFind` | int ids; near-constant `Union`/`Find` |
| Strings by prefix (autocomplete, dictionary) | `Trie` | `WithPrefix` returns sorted matches |
| Key to value, iterated in insertion order | `OrderedHash` | `Hash` + linked entries; O(1) expected |
| A one-to-one table you look up both ways (id and name) | `BiMap` | two `Hash` tables kept in step |
| A priority queue whose priorities change (Dijkstra, A*) | `PriorityMap` | indexed min-heap with decrease-key; O(log n) |
| Running sums that change (point update, prefix/range sum) | `FenwickTree` | O(log n) both ways |
| A cheap "definitely not seen" pre-check in front of something costly | `BloomFilter` | bit set; no false negatives, ~1% false positives at 10 bits/key |
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

## Nil and changing a collection while you walk it

- A `Nil` **key or value** is ignored by `Hash`, `HashSet`, `Set`, `Map`, `MultiHash`, `Heap` and
  `BiMap` inserts, and looking one up reports "not found" (`Nil`, `false` or `-1`). A `Vector`
  stores a `Nil` element, and sorting a `CompareVector` or `CompareList` that holds one is not
  defined.
- `Each`, `Filter` and the iterators read the collection as it is. Adding or removing entries
  from inside the callback, or while an iterator is live, is **not supported**: the result is
  unspecified (a skipped or repeated entry, or a stale count). Collect the changes and apply them
  afterwards, or iterate over `GetKeys()`/`ToArray()`, which are copies.
- `Hash` is open-addressed with the hash stored beside each key and grows at 70% load, so an
  insert that triggers growth is O(n) once and O(1) otherwise; `Reserve` (vectors) and
  `SetCapacity`/`Resize` (`Hash`) avoid that when the size is known.

## The classes

### `Vector<H>` (gen_collect.obs:5118)

A growable array. It doubles (at least four slots) when full, so `AddBack` is
amortised O(1).

| Operation | Cost |
|---|---|
| `Get(i)`, `Set(v, i)`, `First()`, `Last()`, `Size()` | O(1); `Get` returns `Nil` and `Set` returns `false` for a bad index |
| `AddBack(v)`, `RemoveBack()` | O(1) amortised |
| `Remove(i)`, `Insert(i, v)` | O(n): later elements are shifted down / up; `Insert` returns `Bool` (index 0..`Size()`) |
| `Slice(lo, hi)`, `AddAll(other)`, `Swap(a, b)` | O(hi-lo) / O(other) / O(1); `CompareVector` has the same three plus `Find`/`Has` |
| `FindFirst(f)`, `FindIndex(f)`, `Any`, `All`, `Count`, `Filter`, `Map`, `Reduce` | O(n) |
| `Sort(compare)` | O(n log n), stable (merge sort) |

There is no `AddFront` (`Insert(0, v)` works but is O(n)); use a `List` if you
need one. `Reserve(n)` avoids regrowth when the size is known.

### `List<H>` (gen_collect.obs:4355)

A doubly linked list with one built-in cursor (`Rewind`, `Forward`, `Next`, `Previous`, `More`,
`Get`, `Set`) and independent `ForwardIterator()`/`BackwardIterator()` objects.

| Operation | Cost |
|---|---|
| `AddBack`, `AddFront`, `RemoveBack`, `RemoveFront`, `Front`, `Back`, `Size` | O(1) |
| `Insert(v)` (after the cursor; returns `Bool`), `Remove()` (at the cursor) | O(1) |
| `Get()`, `Set(v)`, `Next()`, `Previous()` | O(1), at the cursor |
| anything by position or by value | not provided; walk it, O(n) |

### `Map<K, V>` (gen_collect.obs:6781)

An AA tree (`Skew`/`Split` rebalancing on a level per node), so it stays balanced.

| Operation | Cost |
|---|---|
| `Insert`, `Find`, `Has`, `Remove` | O(log n) |
| `GetKeys`, `GetValues`, `GetKeyValues`, `Each`, `ToString` | O(n), in ascending key order |

`Insert` of an existing key replaces its value. `Find` returns the value, or `Nil` when absent
(so it cannot tell a missing key from a stored `Nil`; use `Has`). A `Nil` key is ignored by
`Insert`. The name is the usual trap: `Map` is the *ordered tree*. It is not a hash map.

### `Hash<K, V>` (gen_collect.obs:2025)

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

### `Set<K>` (gen_collect.obs:7470)

A `Map<K, Base>` underneath, so it has the `Map` costs and ordering: `Add`, `Insert`, `Has` and
`Remove` are O(log n) and `GetKeys`/`ToVector`/`ToArray` return ascending order. `Add` returns
`true` when the value was new; `Insert` returns nothing. `Union`, `Intersect`, `Difference`,
`SymmetricDifference`, `IsSubset` and `IsSuperset` return new sets and leave both operands alone.

### `HashSet<H>` (gen_collect.obs:1430) and `SetHash<K>` (gen_collect.obs:7899)

`HashSet` wraps a `Hash<H, BoolRef>`, so `Add`, `Has` and `Remove` are O(1) expected and the
order is unspecified. `SetHash` wraps a `HashSet` and forwards to it, with the method names
`Set` uses (`Insert`, `Add`, `Has`, `Remove`, the set algebra). They behave the same; `HashSet`
also has `Insert` and `GetKeys` so code can move between the three. Pick one name and keep to it:
`HashSet` is the implementation, `SetHash` is the spelling that parallels `Set`. Adding a `Nil`
returns `false`.

### `MultiMap<K, V>` (gen_collect.obs:8298) and `MultiHash<K, V>` (gen_collect.obs:8644)

A `Map` (or `Hash`) from key to `Vector<V>`. `Insert(k, v)` appends to the key's vector,
creating it on the first value. Costs are those of the underlying map plus an O(1) append.

- `Find(k)` returns **the stored `Vector`**, not a copy, or `Nil` when the key has no values.
  Edits to it are edits to the multi-map.
- `Remove(k)` drops the key and all its values. There is no call that removes one value.
- `Size()` counts distinct keys; `TotalSize()` counts values and walks every key, O(keys).

### `Deque<H>`

A ring buffer. `AddFront`, `AddBack`, `RemoveFront`, `RemoveBack`, `Front`, `Back`, `Get(i)`, `Set(v, i)`
and `Size()` are all O(1) (the buffer doubles when full, so adds are amortised O(1)). Empty
reads return `Nil`. `ToArray()` copies front to back. Use it for a FIFO queue or a sliding
window that also needs index access; a plain `List` is as fast for pure FIFO use but has no
`Get(i)`, and `Vector->Remove(0)` is O(n).

### `MultiSet<K>`

A counter over a `Hash<K, IntRef>`. `Add(k)` and `AddMany(k, n)` return the new count, `Remove(k)`
takes one occurrence away (`false` if absent), `RemoveAll(k)` returns how many it dropped,
`Count(k)` is 0 for an absent key. `Size()` is the total with repeats, `Distinct()` the number of
keys, `GetKeys()` the keys (unordered) and `MostCommon(n)` the top `n` keys by count.
Costs are those of `Hash` (O(1) expected), `MostCommon` is O(k log k) in the distinct keys. It
is called `MultiSet` rather than `Counter` because `System.Concurrency.Counter` already exists in `lang`.

### `UnionFind`

Disjoint sets over the ids `0..n-1` (not generic: the elements are ints, map your own keys to ids
with a `Hash`). `Union` is by size and `Find` halves paths, so both are near-constant amortised.
`Union(a, b)` returns `true` only if it merged two sets; `Connected`, `SizeOf` and `Find` treat an
out-of-range id as absent (`false`, `0`, `-1`). `Count()` is the number of sets, `Size()` the
number of elements, and `Add()` appends a new singleton and returns its id. Typical use:
connected components, Kruskal's minimum spanning tree, grouping equivalent items.

### `Trie`

A prefix tree holding a set of strings. `Add(s)` and `Remove(s)` return `false` for a duplicate or
absent string; `Has(s)` tests a whole string and `HasPrefix(p)` any string starting with `p`.
`WithPrefix(p)` returns the matches as a sorted `Vector<String>` (an empty prefix lists
everything), which is the point of a trie: autocomplete without scanning the set. Each node keeps
its children in a `Map`, so a step costs O(log branching); `Remove` prunes nodes only that string
used. Characters are compared by their code, as in the string itself.

### `OrderedHash<K, V>`

A `Hash` plus a doubly linked list of entries, so `Insert`, `Find`, `Has` and `Remove` are O(1)
expected and `GetKeys()`, `GetValues()` and `Each` walk the entries in the order the keys first
arrived. Updating an existing key keeps its place; removing and re-inserting moves it to the
end. `FirstKey()`, `LastKey()` and `RemoveFirst()` expose the oldest and newest entries, which is
enough for a first-in first-out cache (for least-recently-used eviction use `Cache`). `Find` of an
absent key returns `Nil`. Costs one extra node per entry compared with a plain `Hash`.

### `BiMap<A, B>`

Two `Hash` tables kept in step, so `FindValue(key)` and `FindKey(value)` are both O(1) expected.
Both sides must be `Compare` and non-`Nil`. It is one-to-one: `Insert(k, v)` first drops any pair
that already uses `k` or `v`, so `Size()` never exceeds the number of distinct keys or of distinct
values. `RemoveKey` and `RemoveValue` take out the whole pair. Use it for id-to-name tables, enum
codecs and any "look it up either way" index.

### `PriorityMap<T>`

A min-priority queue where each key appears once and its `Float` priority can change. `Push(k, p)`
adds the key or, if it is present, updates it (decrease-key or increase-key); `Pop()` returns the
key with the smallest priority, `Remove(k)` takes a key out of the middle. Push, Pop, Remove and
updates are O(log n); `Has`, `Priority`, `Peek` are O(1) expected. This is what Dijkstra and A*
need and what `Heap` cannot do (`Heap` holds duplicates and has no update). An absent key's
`Priority` is `0.0`, so test with `Has` first.

### `FenwickTree`

A binary indexed tree over `Int` slots (0-based). `Add(i, delta)` is a point update, `Sum(i)` the
prefix sum up to and including `i`, `RangeSum(lo, hi)` an inclusive range, all O(log n) in n+1
`Int`s of memory. `Get(i)` and `Set(i, v)` are built from those. Use it for running totals that
change (rankings, histograms, "how many items below x") where re-summing a `Vector` would be O(n).
Out-of-range updates return `false`; out-of-range queries clamp or return 0.

### `BloomFilter<T>`

A probabilistic set on a `BitSet`. `Add(k)` sets `k` bits chosen from the key's `HashID`;
`MightContain(k)` is `false` only if the key was never added (no false negatives) and `true`
for added keys and for a small fraction of others. `New(expected, bits_per_key)` sizes it: 10 bits
per key is about 1% false positives, 15 about 0.1%. Keys cannot be removed (use `Empty()` to
start over). Use it as a cheap pre-check in front of something expensive: a disk or network
lookup, a large `Hash`, a de-duplication pass.

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

## How to check

The classes here are covered by `programs/regression/collect_*.obs`: `collect_vector_ops`,
`collect_map_ops`, `collect_hash_ops`, `collect_hash_open` (open addressing, growth past one
million entries, deletes in collision runs, keys of different classes with one `HashID`),
`collect_set_ops`, `collect_set_algebra`, `collect_pair_ops`, `collect_pair_compare`,
`collect_queue_ops`, `collect_stack_ops`, `collect_hash_heap_sort`, `collect_primitive` and
`collect_nested_ops`. Each prints failures and exits non-zero. `gen_collect` is linked by
default, so no `-lib` is needed:

```
cd programs/regression
obc -src collect_hash_open.obs -dest collect_hash_open.obe
obr collect_hash_open.obe
```

The costs in the tables above are read from the source, not measured by these tests.
