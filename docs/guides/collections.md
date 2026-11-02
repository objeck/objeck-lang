# Collections

The `Collection` bundle (`gen_collect`, built into every compile, so no `-lib` flag) has
vectors, lists, maps, hashes, sets, multi-maps, queues, stacks, heaps, pairs and unboxed
`IntVector`, `FloatVector`, `IntMap`, `IntSet` and `BitSet`.

Which one to use, what each operation costs and what each `Remove` returns are in
**[`docs/COLLECTIONS_GUIDE.md`](../COLLECTIONS_GUIDE.md)**; read that to choose. This page is the
short tour in code: five collections doing the five jobs they are most often picked for.

```text
obc -src collections_tour.obs
obr collections_tour.obe
```

<!-- code: programs/examples/guides/collections_tour.obs -->
```ruby
use Collection;

class CollectionsTour {
  function : Main(args : String[]) ~ Nil {
    text := "the quick brown fox jumps over the lazy dog the end";
    words := text->Split(" ");

    # --- Hash: count words; Find gives Nil when the key is not there yet ---
    counts := Hash->New()<String, IntRef>;
    each(i : words) {
      word := words[i];
      seen := counts->Find(word);
      if(seen = Nil) {
        counts->Insert(word, IntRef->New(1));
      }
      else {
        seen->Set(seen->Get() + 1);
      };
    };
    the := counts->Find("the")->Get();
    "distinct words: {$(counts->Size())}, 'the' appears {$the} times"->PrintLine();

    # --- Map: iterates in key order, so the report comes out sorted ---
    ordered := Map->New()<String, IntRef>;
    pairs := counts->GetKeyValues();
    each(i : pairs) {
      ordered->Insert(pairs->Get(i)->GetFirst(), pairs->Get(i)->GetSecond());
    };
    entries := ordered->GetKeyValues();
    "in key order:"->Print();
    each(i : entries) {
      " {$(entries->Get(i)->GetFirst())}={$(entries->Get(i)->GetSecond()->Get())}"->Print();
    };
    ""->PrintLine();

    # --- Vector of pairs: sort by the first value ---
    # a CompareVector sorts; Collection.Pair compares by its first element,
    # so (count, word) pairs sort by count
    by_count := CompareVector->New()<Pair<IntRef, String>>;
    each(i : pairs) {
      by_count->AddBack(Pair->New(pairs->Get(i)->GetSecond(), pairs->Get(i)->GetFirst())<IntRef, String>);
    };
    by_count->Sort();
    top := by_count->Get(by_count->Size() - 1);
    "most common: '{$(top->GetSecond())}' x {$(top->GetFirst()->Get())}"->PrintLine();

    # --- Set: unique values and set algebra ---
    first := Set->New()<String>;
    each(i : 4) {
      first->Add(words[i]);          # the quick brown fox
    };
    second := Set->New()<String>;
    second->Add("the");
    second->Add("fox");
    second->Add("cat");
    shared := first->Intersect(second);
    either := first->Union(second);
    "in both sentences: {$(shared->Size())}, in either: {$(either->Size())}"->PrintLine();
    "shared:"->Print();
    keys := shared->GetKeys();
    each(i : keys) {
      " {$(keys->Get(i))}"->Print();
    };
    ""->PrintLine();

    # --- MultiHash: several values under one key ---
    by_length := MultiHash->New()<IntRef, String>;
    each(i : words) {
      by_length->Insert(IntRef->New(words[i]->Size()), words[i]);
    };
    fives := by_length->Find(IntRef->New(5));
    "five-letter words:"->Print();
    each(i : fives) {
      " {$(fives->Get(i))}"->Print();
    };
    ""->PrintLine();
  }
}
```

```text
distinct words: 9, 'the' appears 3 times
in key order: brown=1 dog=1 end=1 fox=1 jumps=1 lazy=1 over=1 quick=1 the=3
most common: 'the' x 3
in both sentences: 2, in either: 5
shared: fox the
five-letter words: quick brown jumps
```

Things to notice:

- `Map` is the **ordered** tree and `Hash` is the hash table. `Map` printed the words in key order;
  `Hash` makes no promise about order. The names are the usual trap.
- `Find` returns `Nil` for a key that is not there, which is how the counting loop tells a new
  word from a seen one.
- `CompareVector` is a `Vector` that can `Sort()`. `Collection.Pair` compares by its first value, so
  `(count, word)` pairs sort by count. In v2026.10.3 this works: before it, sorting a vector of
  pairs ended in `Invalid object cast: 'Collection.Pair' to 'System.IntRef'`, because
  `Pair->Compare` handed the other pair to the first value's `Compare`.
- `Set` operations (`Intersect`, `Union`, `Difference`) return new sets and leave both operands
  alone.
- `MultiHash` stores a `Vector` per key; `Find(key)` returns that vector, not a copy.

## Where next

- [`docs/COLLECTIONS_GUIDE.md`](../COLLECTIONS_GUIDE.md): the choosing table, costs and the
  `Remove` return values.
- The shipped [`collections_11.obs`](../../programs/deploy/collections_11.obs) covers `List`,
  `Stack`, `Queue` and the unboxed collections.
- [Query.Structured](query.md), when you want to filter, group and join rows with SQL.
