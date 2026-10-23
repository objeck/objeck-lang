# Query.Structured: a columnar, indexed in-memory query engine — Design

`core/compiler/lib_src/query.obs`, built into `core/lib/query.obl`. Pure Objeck; no native
module. This builds on the correctness fixes in #1291 (numeric aggregates, CSV typing, a stable
`order by`, parser precedence, deletes) and assumes it has merged.

## Why

`Query.Structured` exists to make structures that are not tables queryable as tables: a
filesystem through `FromFilesystem` and `Finder`, a CSV file through `FromCsv`, and in intent
other hierarchical or relational data (section 6). Research code needs the same thing: load a CSV
or two, or a tree of result files, then filter, join, group, aggregate and hand a matrix to
`System.ML`. Nothing in the standard library does that well. `Query.Structured` is the closest,
and on 2026-10-22 it was timed at 100,000 rows × 3 columns (Windows x64, `-opt s3`, after the
#1291 fixes), against the same data in plain arrays:

| Operation | Plain arrays | `Query.Structured` |
|---|---|---|
| Sum a column | 0.001 s | 0.051 s |
| `where value > 0.5` (≈50k rows) | 0.003 s | 0.558 s |
| `where grp = 'g7'` (2k rows) | 0.115 s | 0.114 s |
| `order by value` | — | 3.16 s |
| Insert 100k rows | — | 0.98 s |
| 10,000 `Get(pk)` | — | 0.032 s |

The costs are structural:

1. **Every query copies its result.** A `where` builds a new `Table`: a new primary key, a new map
   entry and a new `Compare[]` for every matching row, each cell set through a by-name lookup.
   That is about 10 µs a row and most of the 0.56 s.
2. **Every cell is an object.** Numbers are boxed `IntRef`/`FloatRef` in a per-row `Compare[]`;
   each row is an object on a doubly linked list and a node in a tree map. `Sum` chases pointers
   and type-tests 100,000 times instead of looping over a `Float[]`, and the collector traces
   several objects per row.
3. **Queries are interpreted every time.** The statement is re-parsed on each call, each row walks
   the expression tree through `Compare` dispatch, and with no indexes every equality filter
   scans the table.

It is also only a single table: no joins, no `GROUP BY`, no `LIMIT`, no descending order.

## Goals

- Within a small factor (≤ 5×) of hand-written array loops for scans, filters and aggregates.
- Joins, `GROUP BY`/`HAVING`, multi-key `ORDER BY` with `ASC`/`DESC`, `LIMIT`, and
  `INSERT`/`UPDATE`/`DELETE`.
- Indexes: hash for equality and joins, sorted for ranges and order; a B+ tree as a later upgrade.
- A method-chaining API as well as SQL strings, and prepared statements.
- Zero-copy bridges to `System.ML` (`ToMatrix`, `FromMatrix`) and to `Data.CSV`/JSON.
- Existing programs keep compiling and give the same answers.

## Not goals

- **Persistence beyond import/export.** CSV and JSON in and out, yes; a storage engine with pages,
  crash safety and concurrent writers, no. That is SQLite's job, and an `API.SQLite` binding
  (single public-domain C file, a native module like the others) is the better route to a real
  database. It is a separate design.
- **Full SQL.** No subqueries, no views, no cost-based planner, no transactions.
- **Concurrency.** A table is not thread-safe; callers lock (`critical`) if they share one. Read-only
  sharing of a table nobody mutates is safe.

## Design

### 1. Storage: one typed column per field

```
Table
  @columns : Column[]            # in declaration order
  @names   : Hash<String, IntRef> # name -> column index, resolved once per query, never per row
  @size    : Int                 # rows ever appended
  @deleted : Bitmap / Nil        # tombstones, Nil until the first delete
  @indexes : Index[] per column  # see section 5
```

`Column` is abstract, with one subclass per storage type:

| Column | Storage | Holds |
|---|---|---|
| `IntColumn` | `Int[]` + capacity | `Int`, `Bool`, `Char`, `Byte` |
| `FloatColumn` | `Float[]` | `Float` |
| `StringColumn` | `String[]` | `String` |
| `DateColumn` | `Int[]` of epoch seconds | `Date` |
| `ObjectColumn` | `Compare[]` | anything else that implements `Compare` |

Every column has a null bitmap (`Int[]`, 64 rows a word) allocated only when the first `Nil` is
stored. Arrays grow by doubling; `Compact()` trims them.

**Typing.** A column is declared (`Table->New("t", ["id" : ColumnType->INT, ...])`, or the
`AddColumn(name, type)` form) or inferred from its first non-Nil value. Storing an `Int` into a
`FloatColumn` widens it; storing a `Float` into an `IntColumn` promotes the column to
`FloatColumn` once (a single array conversion); anything else incompatible promotes to
`ObjectColumn`. Promotion is recorded so a test can assert on it. `FromCsv` types each column by
scanning it with the #1291 rules (a column of numbers is `IntColumn` or `FloatColumn`, otherwise
`StringColumn`), not cell by cell.

**Why this is fast in Objeck specifically.** A loop over `Float[]` is the code the JIT compiles
best, with no allocation in the loop. Large arrays are allocated in the old generation, so a
million-row column costs the collector one object, not a million. Boxed values appear only at the
boundary (`Row->Get` returning `Compare`) and never inside scans.

### 2. Results are views

A query returns a `View`: the source table, the projected columns, and an `Int[]` of row numbers
(or `Nil` for "all rows, in order"). Nothing is copied. Chaining (`Where` then `OrderBy` then
`Limit`) only ever replaces the row-number array.

```
View
  @table   : Table
  @columns : Int[]   # projected column indexes
  @rows    : Int[]   # selected row numbers, Nil = every live row
```

`View->ToTable()` materializes when the caller wants an independent copy; `Count()`, iteration,
aggregates, `ToMatrix`, `ToCsv` and `ToJson` read through the view directly. A view taken before a
mutation of its table is invalidated (a version counter on the table; the view refuses with an
error rather than returning stale or out-of-range rows).

### 3. Compiled predicates

A `where` clause is parsed once into a tree of typed nodes that are resolved against the table
before any row is read:

```
Predicate      Test(row : Int) ~ Bool
  FloatCompare   column array + operator + constant   ->  values[row] > 0.5
  IntCompare, StringCompare, DateCompare, Like, IsNull
  And, Or, Not
```

Column names, types and constants are resolved at compile time, so evaluating a row is an array
read and a comparison: no name lookup, no boxing, no `Compare` dispatch. A comparison between a
numeric column and a numeric constant of the other kind converts the constant once. Comparisons
involving `Nil` follow one simple rule, kept from #1291: every comparison with `Nil` is false, and
`IS NULL` / `IS NOT NULL` test for it (no three-valued logic).

Evaluation is column-at-a-time where possible: an `And` of simple comparisons filters the
row-number array one comparison at a time, starting with the most selective index-backed term
(section 5), so later terms see fewer rows.

**Prepared statements.** `Table->Prepare(sql)` returns a `Statement` with `?` placeholders;
`Bind(i, value)` then `Execute()` reuses the compiled tree. `Table->Query(sql)` is `Prepare` plus
`Execute` with a small per-table cache of recent statements, so a query in a loop is parsed once.

**Method-chaining API.** The same nodes are reachable without SQL, so the compiler checks column
expressions and the language server can complete them:

```
rs := table->Where(Col("value")->Gt(0.5)->And(Col("grp")->Eq("g7")))
           ->OrderBy("value", Order->DESC)
           ->Limit(100)
           ->Select(["id", "value"]);
```

### 4. The SQL subset

```
SELECT [DISTINCT] (* | expr [AS name], ...)
FROM table [[INNER | LEFT] JOIN table ON a.col = b.col ...]
[WHERE predicate]
[GROUP BY col, ... [HAVING predicate]]
[ORDER BY col [ASC | DESC], ...]
[LIMIT n [OFFSET m]]

INSERT INTO table [(cols)] VALUES (...), ...
UPDATE table SET col = value, ... [WHERE predicate]
DELETE FROM table [WHERE predicate]
CREATE TABLE name (col type, ...)
CREATE INDEX ON table (col) [USING HASH | SORTED | BTREE]
```

Aggregates: `COUNT(*)`, `COUNT(col)`, `SUM`, `AVG`, `MIN`, `MAX`, plus `MEDIAN` and `STDDEV`
because research code wants them; numeric aggregates skip non-numeric and `Nil` cells, as #1291
made them. Tables are named in a `Catalog` (`catalog->Add(table)`), which `Query` resolves `FROM`
and `JOIN` against; a single table's own `Query` keeps working without one.

**Joins** are hash joins on equality: build a hash of the smaller side's key column once, probe it
with the other, emit pairs of row numbers. A join result is a `JoinView` holding two row-number
arrays, not copied rows.

**Group by** hashes the group columns to group ids in one pass, then runs each aggregate over an
`Int[]` of group ids in a second pass: two linear scans, no per-group sub-tables.

**Order by** sorts the row-number array with a stable merge sort over typed comparators (one per
key column, resolved once), so it never compares boxed values. `LIMIT n` with `ORDER BY` uses a
bounded heap instead of a full sort.

### 5. Indexes

Indexes are optional, per column, and always correct: every mutation updates them, or marks them
stale to be rebuilt on the next query that uses them (the choice is per index kind, below).

| Kind | Structure | Serves | Maintenance |
|---|---|---|---|
| **Hash** | value -> row numbers | `=`, `IN`, joins, `DISTINCT`, `GROUP BY` | updated on every insert, update and delete |
| **Sorted** | `Int[]` of row numbers ordered by the column | `<`, `<=`, `>`, `>=`, `BETWEEN`, `ORDER BY`, `MIN`/`MAX` | rebuilt lazily after mutations (an O(n log n) sort) |
| **B+ tree** (phase 5) | in-memory B+ tree, leaves are `Int[]` blocks of row numbers in key order | the sorted index's queries, with O(log n) inserts | updated on every mutation |

The **sorted index** is the right default for research data, which is mostly loaded once and
queried many times: a binary search finds a range's start and end, and the range is already a
row-number array in order, so it feeds a view with no further sort.

The **B+ tree** is in scope as the sorted index's upgrade, behind the same `Index` interface,
for tables that take frequent inserts *mixed with* range queries, where re-sorting after every
batch stops being cheap. In memory its advantage over the existing balanced `Collection.Map` is
that leaves are contiguous `Int[]` blocks (fan-out around 64), so a range scan walks arrays
rather than one tree node per entry, and the collector sees one object per block. It is built
only when a benchmark shows a workload that needs it.

The planner is rule-based: for an `And` of terms, use the index of the most selective
index-backed term (equality on a hash index, then a range on a sorted or B+ tree index), then
filter the remaining terms over that candidate set. Without a usable index, scan.

### 6. Sources: making other structures queryable

This is the library's original purpose, and `Finder` and `FromFilesystem` are its first two
instances: take a structure that is not a table, such as a directory tree, a document or a data
file, and present it as tables the engine can query. Today they are two unrelated pieces.
`FromFilesystem` lists one directory, without recursing, into six columns. `Finder` walks a tree
matching a regular expression against full paths, but returns a `Vector<String>` instead of a
table, so its results cannot be queried. `use Data.XML` is imported with no code behind it. The
redesign makes sources one concept.

**The interface.** A `Source` declares its tables and columns, then fills them:

```
interface Source {
  method : virtual : public : GetTables() ~ TableSpec[];          # names, columns, types
  method : virtual : public : Load(catalog : Catalog, hint : ScanHint) ~ Bool;
}
```

`ScanHint` carries what the query already knows, so a source can push work down instead of
materializing everything. Examples: the columns it needs (a filesystem source skips the
owner lookup no query asked for), a filter it can apply cheaply (skip descending into
directories a `path LIKE 'src/%'` excludes, or test the file name before calling `stat`), and a
row limit. Pushdown is optional: a source that ignores the hint is still correct, because the
engine applies the full predicate afterwards. Programs implement `Source` for their own
structures; the library ships the common ones.

**Hierarchies become tables with parent keys.** A tree maps to rows with `id`, `parent_id`,
`depth` and `path` columns (an adjacency list plus a materialized path). Once phase 3 adds joins,
parent and child questions become self-joins, and subtree questions become `path LIKE 'a/b/%'`.
Nested repeated data goes into a child table keyed by its parent's `id`, so one document can
become several related tables that join back together.

| Source | Tables and columns |
|---|---|
| `FilesystemSource(root, recursive, max_depth)` | `files`: id, parent_id, depth, name, path, ext, is_dir, size, modified, created, owner, is_readonly, is_hidden |
| `CsvSource(path[, delimiter])` | one table, columns typed per section 1 |
| `JsonSource(text or path)` | an array of objects becomes one table, with nested objects flattened to dotted column names (`address.city`); nested arrays become child tables with `parent_id` |
| `XmlSource(text or path, row_element)` | each `row_element` becomes a row, its attributes and simple child elements become columns, and repeated child elements become child tables |
| `MatrixSource(m, names)` | one `FloatColumn` table (the `FromMatrix` bridge below) |

**Query a file by naming it.** CSV is the most common case, so a file path in `FROM` or
`JOIN` is a table. Nothing has to be loaded or registered first:

```
rs := Catalog->Run("select name, avg(score) from 'results/run_*.csv' group by name");
rs := Catalog->Run("select r.id, r.score, m.label from 'runs.csv' r join 'meta.csv' m on r.id = m.id");
```

`Catalog->Run(sql)` runs a statement against an empty catalog, so file tables are all it can
see. `catalog->Query(sql)` does the same over a catalog the program has filled, so in-memory
tables and files can be mixed in one statement.

- **The source follows the extension.** `.csv`, `.tsv` and `.txt` read as delimited text,
  `.jsonl` and `.ndjson` as JSON Lines, and `.json` as JSON. A trailing `.gz` is decompressed
  first, with the gzip support already in `lang`.
- **A glob matches several files with the same columns.** They are read as one table, with an
  added `_file` column naming each row's file. That is the shape of most experiment output:
  one CSV per run.
- **Options when the defaults are wrong:** `from csv('data.txt', delimiter => ';', header =>
  false)`. Delimiter, header and quoting go through `Data.CSV`, which follows RFC 4180 after
  #1289.
- **Loaded once per query, cached by path and modification time,** so repeating a query in a
  loop does not re-read an unchanged file. A changed file is re-read.
- **Columns are typed** per section 1, scanning each column rather than each cell.
- `Table->FromCsv(name, path)` keeps working and is the same source with a name attached.

Further sources fit the same interface without changing the engine, for example HTTP JSON APIs
through `Web.HTTP`, RSS through `Web.RSS`, or a program's own objects.

**Finder keeps its API and gains a table.** `Finder->New(regex)->FindFiles(path)` keeps its
signature and results, but becomes a query over `FilesystemSource`:
`select path from files where is_dir = false and path MATCHES regex`. `MATCHES` is a new SQL
operator backed by `Query.RegEx`, which gained character classes and capture groups in #1290.
`Finder->FindTable(path)` returns the result as a `View`, so a file search can be filtered,
sorted, grouped by extension or joined like any other table. `Table->FromFilesystem(name, path)`
keeps its six columns and one-directory behaviour, and becomes `FilesystemSource` with
`recursive` false.

### 7. Bridges

- `View->ToMatrix(cols) ~ Float[,]` and `Table->FromMatrix(m, names)`: copies between `FloatColumn`
  arrays and `System.ML`'s matrices, with `Nil` cells reported (an error, or a fill value the caller
  passes).
- `ToCsv`, `FromCsv` (through `Data.CSV`, so quoting is RFC 4180 after #1289), `ToJson`, `FromJson`
  (an array of objects).
- `Describe()`: per-column count, nulls, min, max, mean, std, distinct count; the first thing a
  researcher asks of a new dataset.

### 8. Compatibility

Almost nothing outside the library uses it today (`programs/tests/prgm193.obs` calls
`FromFilesystem` and `Query`; two programs use `Finder`), so the cost of keeping old programs
working is small, and they must keep working:

- `Table->New(name, String[])`, `Insert() ~ Row`, `Row->Get/Set` by name or index, `Delete(pk)`,
  `Get(pk)`, `Sum/Average/Min/Max/Count/Unique`, `Query(sql) ~ Table`, `FromCsv`, `FromFilesystem`
  and `GetError` keep their signatures and results. `Row` becomes a cursor (table, row number)
  instead of an object holding values, so `Insert()` no longer allocates a row's worth of cells.
- The hidden `pk` column stays: row numbers are primary keys, and `Get(pk)` becomes an array index
  (O(1)) instead of a tree lookup.
- `Query(sql)` keeps returning a `Table`: a `Table` that is backed by a view until it is mutated
  (copy on write), so the old API also stops copying.
- `Row->Next()/Prev()` iterate in row order, skipping deleted rows.
- `Query`, `Parser`, `Term` and `Token` are public classes today although they are implementation
  details. They stay for one release as deprecated shims over the new parser (their doc comments
  say so), then become private.
- `Finder` and `FromFilesystem` keep their signatures and results and are reimplemented over
  `FilesystemSource` (section 6). One latent oddity goes with the rewrite: `FromFilesystem`'s
  trailing-separator test reads `<>path->EndsWith('/')  <>path->EndsWith('\\')` with no operator
  between the two halves, and parses as a not-equal comparison that happens to give the right
  answer.

## Testing

- **A differential test is the core of it.** `programs/regression/query_differential.obs`
  generates random tables (mixed types, Nil cells, duplicate keys) and random queries from the
  grammar, and runs each through the engine and through a deliberately simple reference
  evaluator kept in the test (row-at-a-time, no indexes, no views). Results must match exactly,
  with and without indexes, for a fixed seed set that CI runs and a longer run for the nightly job.
- Unit tests per phase: column typing and promotion, null bitmaps, view invalidation, each
  predicate node, each join and aggregate, each index kind against a linear scan, prepared
  statements and the statement cache, the compatibility API (the #1291 tests must keep passing
  unchanged).
- **A performance gate.** `programs/regression/query_bench.obs` (the benchmark above, at 100k rows)
  with thresholds as multiples of the plain-array baseline measured in the same run, so it is
  machine-independent: scan and aggregate ≤ 5×, `where` ≤ 5× plus result size, `order by` ≤
  0.3 s. Whether it gates pull requests or runs only in the nightly job follows the repository's
  existing performance gate (`perf-results/PERF_GATE.md`); timing is noisy on shared runners, so
  the nightly job is the likelier home.
- Every phase passes at `-opt s3`, with `OBJECK_JIT_THRESHOLD=1`, and at `-opt s0` with `--jit=off`.

## Phases

Each phase lands as its own pull request, with its tests, a rebuilt `query.obl`, and the editor
index regenerated for new classes.

1. **Columnar storage.** `Column` types, null bitmaps, typing and promotion, `Row` as a cursor, O(1)
   `Get(pk)`, tombstone deletes with `Compact()`, the compatibility API on top, `Describe()`.
   The `Source` interface with `CsvSource` and `FilesystemSource`, and `FromCsv`,
   `FromFilesystem` and `Finder` reimplemented over them. Existing tests unchanged; benchmark:
   insert, scan and aggregates.
2. **Views and compiled predicates.** `View`, copy-on-write result tables, the typed predicate
   nodes, prepared statements and the statement cache, the method-chaining API. The differential
   test starts here.
3. **The relational layer.** `Catalog`, hash joins, `GROUP BY`/`HAVING`, multi-key `ORDER BY`
   `ASC`/`DESC`, `LIMIT`/`OFFSET`, `INSERT`/`UPDATE`/`DELETE`/`CREATE TABLE` in SQL, the bridges
   (`ToMatrix`, `FromMatrix`, CSV, JSON). The hierarchical sources, which need joins to be
   useful: `JsonSource` and `XmlSource` with child tables, the recursive `FilesystemSource` with
   parent keys, `MATCHES`, `Finder->FindTable`, and scan-hint pushdown. Files named in `FROM`
   and `JOIN` (CSV, TSV, JSON Lines, JSON, gzipped), globs over several files, and the per-path
   cache.
4. **Hash and sorted indexes** and the rule-based planner.
5. **B+ tree index**, only once a benchmark shows a mixed insert and range workload that the sorted
   index serves badly; behind the same interface, so nothing above it changes.

## Open questions

- Whether `Date` should be a first-class column type in phase 1 or start as an `ObjectColumn`; the
  current engine compares dates, so phase 1 must at least keep that working.
- Whether `ObjectColumn` should exist at all, or whether storing an incompatible value should be
  an error. It keeps today's "any `Compare`" behaviour, at the cost of a slow path.
- Whether the SQL `Catalog` should be global (one per program) or explicit. Explicit is assumed
  above; it is simpler to test and has no hidden state.
- Whether sources should load eagerly into columns (assumed above: simple, and fast once loaded)
  or also offer a streaming scan for structures too large to hold, such as a whole drive or a
  multi-gigabyte log. Eager loading with pushdown covers most research use; streaming would need
  a cursor-based plan and is left until a workload needs it.
- How a JSON or XML document with irregular structure (the same key holding an object in one
  record and a string in another) should map: promote the column to `ObjectColumn`, or split it.
