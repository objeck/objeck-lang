# Benchmark Task for Claude on AMD 7950X3D Box

## Context
Library-level optimizations were just pushed to master (commit `bcb5512f0`). Changes include:
- **gen_collect.obs**: Hash load-factor auto-resize (>75% triggers upsize), Vector in-place Remove (shift instead of new array), 50% growth factor, Map.Filter uses GetKeyValues
- **json.obs**: Fixed escape sequence scanning (`@input_index += 2`), fixed quoted keyword-as-key tokenization
- **onnx.obs**: Tokenizer Encode/Decode use `String.Append()` instead of `+=` (O(n) vs O(n²))
- **csv.obs**: Fixed Median value-as-index bug, fixed Average off-by-one
- **net_common.obs**: Fixed swapped `$`/`&` URL encoding, fixed Response.ToString nil check
- **regex.obs**: Cache ToCharArray across Find() loop
- **query.obs**: Reserved keyword map moved to class field (init once)
- **xml.obs**: Bulk String copy in ParseName, tag contents, CDATA
- **lang.obs**: Added `String->New(capacity : Int)` constructor (needs sys_obc rebuild)
- **New regression tests**: `core_collections_perf.obs` (7 tests), `core_json_escape.obs` (8 tests)

## Steps

### 1. Pull and rebuild
```bash
cd /path/to/objeck-lang
git pull
cd core/release
bash update_version.sh
```

### 2. Run regression tests
```bash
cd programs/regression
# Run full regression suite including new tests
# Expected: all tests pass (19/19 minimum, including 2 new tests)
```

### 3. Run performance benchmarks (3 runs each)
```bash
cd perf-results
bash run_benchmarks.sh ../../core/release/deploy-x64 ./v2026.3.0 3
```
This runs all CLBG + perf benchmarks and writes `v2026.3.0/results.csv`.

### 4. Create library-specific micro-benchmarks
The existing benchmarks (nbody, binarytrees, etc.) test JIT/compiler performance. They won't show the library changes. Create and run these targeted benchmarks:

#### a. Hash resize benchmark (`bench_hash_resize.obs`)
```
use Collection;
class HashResize {
    function : Main(args : String[]) ~ Nil {
        # Insert 100K entries, measure total time
        h := Hash->New()<String, IntRef>;
        each(i : 100000) {
            key := "key_{$i}";
            h->Insert(key, IntRef->New(i));
        };
        # Verify all present
        each(i : 100000) {
            key := "key_{$i}";
            val := h->Find(key);
            if(val = Nil | val->Get() <> i) {
                "FAIL"->PrintLine();
                return;
            };
        };
        sz := h->Size();
        "Hash: {$sz} entries OK"->PrintLine();
    }
}
```

#### b. JSON parse benchmark (`bench_json_parse.obs`)
```
use Data.JSON;
class JsonParseBench {
    function : Main(args : String[]) ~ Nil {
        # Build a large JSON string with escapes
        builder := "";
        each(i : 1000) {
            if(i > 0) { builder += ","; };
            builder += "\"key_{$i}\": \"value\\\\path\\\\{$i}\"";
        };
        json := "{" + builder + "}";

        # Parse it 100 times
        each(j : 100) {
            p := JsonParser->New(json);
            if(<>p->Parse()) {
                "FAIL"->PrintLine();
                return;
            };
        };
        "JSON: 100 parses of 1000-key object OK"->PrintLine();
    }
}
```

#### c. Vector remove benchmark (`bench_vector_remove.obs`)
```
use Collection;
class VectorRemoveBench {
    function : Main(args : String[]) ~ Nil {
        # Repeated remove-from-middle
        each(round : 100) {
            v := Vector->New()<IntRef>;
            each(i : 1000) {
                v->AddBack(IntRef->New(i));
            };
            # Remove from middle repeatedly
            while(v->Size() > 0) {
                mid := v->Size() / 2;
                v->Remove(mid);
            };
        };
        "Vector: 100 rounds of 1000 removes OK"->PrintLine();
    }
}
```

Compile and time each:
```bash
cd core/release/deploy-x64/bin
./obc -src bench_hash_resize.obs -dest bench_hash_resize.obe -lib gen_collect -opt s3
./obc -src bench_json_parse.obs -dest bench_json_parse.obe -lib json -opt s3
./obc -src bench_vector_remove.obs -dest bench_vector_remove.obe -lib gen_collect -opt s3

for bench in bench_hash_resize bench_json_parse bench_vector_remove; do
    echo "=== $bench ==="
    for run in 1 2 3; do
        /usr/bin/time -f "  Run $run: %e seconds" ./obr ${bench}.obe 2>&1
    done
done
```

### 5. Update `docs/performance.md`

Add a new section **after section 5** (before Benchmark Results):

**Section: v2026.3.0 — Standard Library Optimizations**

Include:
- Summary table of all changes with category (Bug Fix / Performance / Both)
- Benchmark results from step 3 (CLBG/perf — confirm no regressions)
- Micro-benchmark results from step 4 (library-specific improvements)
- Algorithmic complexity improvements table:

| Component | Operation | Before | After |
|-----------|-----------|--------|-------|
| Hash.Insert | Auto-resize | Fixed capacity (769 default) | Load-factor resize at 75% (769→12289→1.5M→...) |
| Vector.Remove | Element removal | O(n) new array + copy all | O(n) in-place shift (no allocation) |
| Vector.Expand | Growth | 25% increase | 50% increase (fewer resizes) |
| Map.Filter | Filtering | GetKeys + Find per key (2 lookups) | GetKeyValues (1 lookup per pair) |
| JsonParser.Scan | Escape handling | Stateful flag (buggy on \\) | Skip +2 (correct, branchless) |
| JsonParser.Scan | Keyword matching | Always matched true/false/null | Only matches unquoted identifiers |
| Tokenizer.Decode | String building | += concatenation O(n²) | String.Append O(n) |
| Tokenizer.Encode | String building | += concatenation O(n²) | String.Append O(n) |
| Regex.Find | Input conversion | ToCharArray every call | Cached (skip if same input) |
| Query.Scan | Reserved words | New Map per Scan() call | Class field (init once) |
| Xml.ParseName | Name extraction | Char-by-char append | Bulk String.New(buffer, start, len) |

Update the timeline table in section 3 to add:
```
| v2026.3.0 | Mar 2026 | Standard library: Hash auto-resize, Vector in-place Remove, JSON escape/keyword fixes, tokenizer O(n) string building |
```

Update `*Last updated:*` at the bottom to `March 2026 — v2026.3.0`.

### 6. Commit and push
```bash
git add -A
git commit -m "Update performance.md with v2026.3.0 library optimization benchmarks"
git push
```
