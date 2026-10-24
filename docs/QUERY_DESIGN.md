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
- **Speed first:** dictionary-encoded strings, row groups with min/max statistics so whole groups
  are skipped, and batch evaluation (section 8).
- **Persistence that is fast and safe for one process:** a native columnar file format that saves
  and loads at near disk speed, reads only the columns and row groups a query needs, appends
  without rewriting, survives a crash mid-save, and keeps a change log between saves
  (section 9).
- **Disk and network alike:** any file source reads from an `https://` URL as well as a path;
  tables and query results can be served over HTTP and queried from another process
  (section 10).
- Existing programs keep compiling and give the same answers.

## Not goals

- **A multi-user database.** Several processes writing one table file at once, transactions
  spanning tables, and a SQL server with users and permissions are SQLite's or a real database
  server's job. An `API.SQLite` binding (single public-domain C file, a native module like the
  others) is the route there, as a separate design. One writing process with any number of
  readers is in scope (section 9).
- **Full SQL.** No subqueries, no views, no cost-based planner, no transactions.
- **Concurrency.** A table is not thread-safe; callers lock (`critical`) if they share one. Read-only
  sharing of a table nobody mutates is safe.

## Decision: a pure-Objeck engine, not one backed by SQLite

Running SQLite underneath the `Query.Structured` front end was considered and rejected
(2026-10-23). It would have brought joins, grouping, B-tree indexes, transactions, persistence and
full SQL at once, and removed most of the phases below. It was not chosen, for four reasons:

- **The use case is in-memory analytics that feeds `System.ML`.** That is scans, filters and
  aggregates over loaded data. Typed column arrays do this faster than SQLite's row storage, and
  `ToMatrix` over a `FloatColumn` is nearly free, whereas every SQLite value crosses the native
  boundary twice: once loading the data in, once reading the result back.
- **No native dependency.** The engine runs wherever Objeck runs, with nothing to compile or
  ship for five platforms, and it is Objeck code the language server, debugger and JIT all see.
- **Sources stay natural.** SQLite's virtual-table interface, its fit for "sources", works by
  calling back from C into the program, which the native interface cannot do, so every source
  would have had to load into a SQLite table first.
- **The cost of building an engine is contained.** The differential test against a simple
  reference evaluator, the per-source tests and the performance gate catch the classic
  query-engine bugs. The SQL subset, and the absence of persistence, transactions and
  concurrency, keep the surface small.

Choosing a pure-Objeck engine does not mean giving up persistence. Section 9 adds a native file
format with fast save and load, partial reads, appends, crash-safe saves and a change log, which
covers a research program's own data. SQLite remains the right tool where several processes write
the same data, or where transactions must span tables, as a separate `API.SQLite` binding (see
"Not goals"). It can later be attached as a source like any other.

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
| **B+ tree** (phase 8) | in-memory B+ tree, leaves are `Int[]` blocks of row numbers in key order | the sorted index's queries, with O(log n) inserts | updated on every mutation |

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
`depth` and `path` columns (an adjacency list plus a materialized path). Once phase 4 adds joins,
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

**More sources.** Beyond the four above, the sources below are chosen for research use, ranked by
value against the cost of building them on libraries the language already has.

*Tier 1: in `query.obl`, built on libraries it already links or that are in `lang`.*

| Source | What becomes a table | Built on |
|---|---|---|
| `TextSource(path, pattern[, record_start])` | Each line, or each multi-line record beginning at `record_start`, matched by a regular expression; named capture groups become columns, and lines that do not match are counted and optionally kept in a `_unmatched` table. Training logs, server logs, instrument output. | `Query.RegEx` capture groups (#1290) |
| `JsonLinesSource(path)` | One JSON object per line, mapped like `JsonSource`, with a record that fails to parse reported by line number. ML datasets, evaluation outputs, OpenAI batch files. It reads line by line, so it is also the first source that could stream. | `Data.JSON` |
| Compression | `.gz`, wrapping any file source above, transparently. | `lang`'s gzip and zlib |
| `HttpJsonSource(url, records_path[, paging])` | The array at `records_path` in a JSON response; paging follows a next-page link or an offset parameter, up to a row limit. Public datasets and REST services. | `Web.HTTP`, `Data.JSON` |
| `ObjectSource(objects, columns)` | A `Vector<T>` of the program's own objects: one row per object, one column per named field, read through `System.Introspection`. Where reflection cannot reach a value, a column can be given as a function from the object to a value instead. | `System.Introspection` |

*Tier 2: separate add-on libraries, because they need native modules the core should not load.
A program that only queries CSV files must not need ODBC, ONNX Runtime or OpenCV installed, so
each of these links `query` plus the library it adapts.*

| Library | Source | What becomes a table |
|---|---|---|
| `query_ml.obl` | `MlResultSource` | `System.ML` results: cross-validation folds and per-fold metrics, threshold sweeps, and predictions with their scores and true labels. Comparing runs becomes a `GROUP BY` and `ORDER BY`. |
| `query_odbc.obl` | `OdbcSource(connection, sql)` | The result of a query against a real database. With a scan hint, simple `where` terms are pushed into the SQL sent, so the database filters before anything is copied. |
| `query_vision.obl` | `VisionSource(folder, detector)` | Detections across a folder of images, one row each: image, label, score and box, from any `API.Onnx` detector. For example, the images where a person was found with a score above 0.9, grouped by camera. |
| `query.obl` (no native code) | `ArchiveSource(path)` | Zip and tar contents as a tree, like `FilesystemSource`, and any member readable as a source, such as a CSV inside a zip. Tar and stored zip members need only `lang`; deflated zip members use `Byte->UncompressDeflate` (raw deflate, RFC 1951) from `lang`. |
| `query.obl` | `XlsxSource(path[, sheet])` | Each worksheet as a table, with typed cells and the first row as the header. An `.xlsx` file is a zip of XML parts, so this builds on `ArchiveSource` and `XmlSource`. |

Further sources fit the same interface without changing the engine, for example RSS through
`Web.RSS`.

**Source tests.** Every source has its own regression test,
`programs/regression/query_source_<name>.obs`, and every test builds its input files at run time
in a temporary directory and deletes them afterwards. No dataset is committed, so there are no
fixture files to drift or to fall foul of ignore rules, and every edge case is visible in the test
itself. Each source's test covers:

- **Schema and typing:** the declared tables and columns, and column types as section 1 infers
  them, including a column that is numeric except for one cell.
- **Round trip:** a random in-memory table, written out in the source's format and read back,
  equals the original, cell for cell, including `Nil` cells and Unicode text. This extends the
  differential test to sources, for every writable format (CSV, TSV, JSON, JSON Lines, XML).
- **Malformed input:** each kind of bad record is rejected with an error naming the file and
  line, or row, and is never silently skipped. Examples: a CSV row with too many fields, a JSON
  Lines record that does not parse, an XML file that is not well formed.
- **Edge cases:** an empty file, a header-only file, CRLF line endings, a missing final newline,
  a byte-order mark, and very long lines.
- **Pushdown equivalence:** the same query with and without the scan hint returns the same rows.
  Pushdown may only ever change speed.
- **Compression and globs:** a gzipped file returns exactly what the uncompressed one does, and a
  glob over N files returns their rows with `_file` set.
- **Caching:** an unchanged file is not re-read on a second query, and a file changed between
  queries is re-read.

Sources that reach outside the process are tested without the outside world:

- `HttpJsonSource` runs against an in-process `Web.HTTP.Server` serving canned pages, including
  pagination, an error status and a truncated body. Windows loopback has dropped data when the
  sender tears down first, so the server must keep the connection until the client finishes.
- `OdbcSource` runs against SQLite through its ODBC driver, which the CI images already install.
  It prints `SKIP:` where no driver is present.
- `VisionSource` takes the detector as an interface, so its test supplies a fake detector with
  scripted boxes. A second, optional test runs a real model and skips when none is present, as
  the regression tree carries no models.
- `MlResultSource` runs a small, seeded cross-validation from `System.ML` and checks that the rows
  match the metrics read directly from the result objects.

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

> **As built (phase 4c):** `ToCsv([delimiter])`, `WriteCsv(stream[, delimiter])`, `ToJson()`,
> `WriteJson(stream)` and `ToMatrix(names[, fill])` on both `View` and `Table`;
> `Table->FromCsvText(name, text)`, `Table->FromJson(name, text[, names])` and
> `Table->FromMatrix(name, matrix, names)`. Writers stream a line or an object at a time and
> check every write. Numbers are exact (a Float as its shortest round-trip text, via
> `Float->ToShortString`). CSV dates are `#MM/dd/yyyy hh:mm:ss#`, the form `CsvSource` reads
> back as dates; JSON dates are ISO 8601 with `Z` for GMT. What a format cannot carry is
> documented rather than invented: CSV has no Bool (it reads back as the text `true`/`false`)
> and no GMT flag; JSON has no NaN or infinity (written as `null`) and no member order (so
> `FromJson` takes the column order as an optional argument). `ToMatrix` reads INT, FLOAT and
> BOOL cells (and numbers in OBJECT columns); an empty or non-numeric cell is an error naming
> the column and row unless a fill value is given. `Describe()` already existed.

### 8. Making it fast

Columns, views and compiled predicates remove the structural costs. These go further, in the
order they pay off:

- **Dictionary-encoded strings.** A `StringColumn` with few distinct values, such as a category,
  a label or a run name, stores each distinct string once and an `Int[]` of codes per row. An
  equality filter compares integers, `GROUP BY` and joins hash integers, and memory falls with
  the number of repeats. The column switches between plain and encoded storage by itself, at a
  distinct-value ratio fixed in phase 1 and recorded so tests can check it. In the benchmark
  above, `grp = 'g7'` is a string comparison per row and takes as long as the plain-array
  loop; on codes it becomes an integer comparison.
- **Row groups and zone maps.** Columns are stored in groups of 64K rows, each with its minimum,
  maximum and null count. A filter like `value > 0.9` skips every group whose maximum is below
  0.9 without reading it, which costs nothing for unsorted data and is decisive for data loaded
  in time or ID order, the common case for logs and experiment output. The same groups are the
  unit of the file format (section 9), so a query against a file reads only the groups it
  needs.
- **Batch evaluation.** Predicates run over a row group at a time and produce a selection
  vector, an `Int[]` of the rows that pass. Each predicate node is one tight loop over arrays
  instead of one call per row, which is the code the JIT compiles best.
- **No allocation in the hot path.** Selection vectors and scratch arrays are reused across
  groups. The only allocations during a query are its result arrays.
- **Parallel scans, later.** Row groups are independent, so a scan can split them across
  threads. This waits until a single-threaded engine meets its targets, and needs a check that
  the collector's multithreaded paths hold up under it. The repository's notes record earlier
  multithreaded-GC defects, so it gets its own stress test.

### 9. Persistence: a native table file

Research programs need to keep what they computed: a cleaned dataset, a feature table, results
across runs. CSV and JSON stay for interchange, but they are slow to parse and lose types. The
engine gets its own file format, `.otab`, which stores exactly what the column storage holds.

> **As built:** [OTAB_FORMAT.md](OTAB_FORMAT.md) is the byte-exact specification of version 1,
> and it differs from the plan below in four ways. The schema sits in the footer, not the
> header, so opening a file is one read at its end. The codecs are zlib at level 1 (`FAST`) and
> level 9 (`SMALL`); `lang`'s Brotli entry point turned out to be raw deflate. Each block carries
> its own dictionary of just the texts it uses. Delta and run-length encodings are not in
> version 1.

**Layout.**

```
header      magic "OTAB", format version, schema (names, types, dictionaries)
row group   for each column: type, encoding, null bitmap, data block, min, max, CRC32
row group   ...
footer      offset of every row group and column block, row count, footer CRC32
trailer     footer offset, magic
```

- **Fast save and load.** Numeric columns are written and read as whole arrays through
  `Serializer`, whose array writes are single VM instructions (`SERL_INT_ARY`,
  `SERL_FLOAT_ARY`), with no per-cell parsing. The target is save and load at a large fraction of
  disk speed: one million rows of three numeric columns in well under a second. The first commit
  of the phase measures the array paths; if they fall short, they are made faster in the VM
  rather than the target relaxed.
- **Partial reads.** The footer lets a reader seek straight to the blocks it needs. A query
  over `runs.otab` reads only the columns it names and only the row groups its zone maps cannot
  rule out. Opening a large file costs a footer read, not a load.
- **Compression per block, never per file.**
  - **Codecs:** `lang` provides zlib (`CompressZlib`, with levels 0 to 9), gzip and raw
    deflate (`CompressDeflate`), each compressing a `Byte[]` in memory. All three are zlib's
    deflate underneath; there is no Brotli (the trap once named `CompressBr` was raw deflate,
    and a real Brotli codec would be a new native dependency). Each column block in each row
    group is compressed on its own and records its codec, so partial reads and HTTP range
    reads keep working.
  - **Choosing one:** `Save(path)` stores blocks uncompressed, for speed;
    `Save(path, Compression->ZLIB)` is balanced, and a higher zlib level gives smaller files
    for archives and network transfer. Reading needs no option, since every block names its
    codec.
  - **Only when it helps:** a block whose compressed form is not smaller is stored raw, as
    dense data such as random floats often is.
  - **Whole-file compression is transport only:** a `runs.otab.gz` still opens through the
    general `.gz` handling, but is decompressed in full before reading.
- **Light encodings before compression,** recorded per block:
  - dictionary codes for repetitive text;
  - delta encoding for sorted or nearly sorted integers and timestamps, so IDs and log times
    become small numbers that compress very well;
  - run-length encoding for long runs of one value.

  Each is used only where it shrinks the block, and the format version allows more later.
- **Integrity.** Every block and the footer carry a CRC32 (`lang` has one), computed over the
  bytes as stored, after compression, so corruption is caught before anything is decompressed. A truncated or
  corrupted file is reported with the block it failed on, never loaded as wrong data.

**Saving safely.** `table->Save(path)` writes a temporary file beside the target, flushes it,
then renames it over the target, so a crash mid-save leaves the old file intact. One
prerequisite: a rename over an existing file must replace it atomically on every platform.
POSIX `rename` does; on Windows it needs `MoveFileEx` with `MOVEFILE_REPLACE_EXISTING`, so the
VM's rename must be checked and fixed there first if it does not.

**Appending.** `table->Append(path)` writes only the new rows as additional row groups and a new
footer, without rewriting the existing groups. Loading a dataset that grows by a run a day costs
one row group a day.

**A change log between saves.** A table opened with `Table->Open(path)` records every insert,
update and delete in a log file beside it (`runs.otab.log`), one length-prefixed and
checksummed record per change, flushed per statement. Re-opening replays the log over the last
save, so a crash loses at most the statement in progress. `Checkpoint()` folds the log into a new
save and empties it. Durability is per statement; there are no multi-statement transactions.

> **As built (phase 3b):** `TableStore->New(path)` opens a table for writing and reports why
> it could not (`IsOpen`, `GetError`, `GetReplayed`); `Table->Open(path)` is the short form that
> answers Nil. A durable table's `Checkpoint()` and `Close()` act on its store, and `Save` of its
> own file is a checkpoint. Each log record stores its length twice (once inverted), so damage
> to a length is told apart from a crash's torn tail instead of dropping every later record; a
> durable table refuses an OBJECT value the log cannot record. The byte layout of the log and
> the lock is in [OTAB_FORMAT.md](OTAB_FORMAT.md#the-change-log).

**One writer, many readers.** A process that opens a table for writing takes a lock file. Others
can open it read-only and see the state of the last save or checkpoint. A second writer is
refused with an error naming the lock's holder, and a stale lock left by a crash is detected and
cleared. Exclusive creation of the lock file needs an exclusive-create primitive (`O_EXCL` on
POSIX, `CREATE_NEW` on Windows), which the file API does not offer today, so it is added as a
new VM trap first (appended after the existing ones, never by widening one).

**Serialization.** Tables, views and catalogs serialize in two ways, for two jobs:

- **With the language's own serializer.** `Serializer->Write(table)` and
  `Deserializer->ReadObject()` already handle any object graph, so a table can travel inside
  another serialized object, such as a model that holds its feature table, or be sent over a
  socket, with no table-specific code. The design keeps this working on purpose:
  - no field holds anything the serializer cannot write, such as a native handle, a closure or
    a lock;
  - state that can be rebuilt is not trusted after a round trip, because the serializer has no
    custom hooks. Indexes, the statement cache and view version counters carry a generation
    number that marks them stale, and they rebuild on first use.

  This form follows the classes' field layout, so it suits data passed between parts of one
  program, or between processes running the same build, and not files kept across library
  versions.
- **As versioned bytes.** `table->ToBytes() ~ Byte[]` and `Table->FromBytes(bytes)` produce and
  read the `.otab` format in memory, and `Save`/`Load` write and read it to a file or any
  stream. This form is self-describing and versioned, so it survives library upgrades, reads
  only what a query needs, and is what to keep on disk or send to another program.

Both round trips are tested: every column type, `Nil` cells, dictionary-encoded columns, deleted
rows, views and catalogs, and, for the generic form, a table nested inside another object.

**Indexes are rebuilt on load** in the first version, since hash and sorted indexes rebuild in
one pass. Storing them in the file is a later option if load time shows it matters.

### 10. Network: reading from and serving to other machines

- **Every file source reads URLs.** A path that starts with `https://` or `http://` is fetched
  through `Web.HTTP` (with TLS verification on, the library's default) and read by the same
  source as a local file. That covers CSV, JSON Lines and the other formats, `.gz`, and `.otab`.
  So `from 'https://example.org/data/runs.csv'` works anywhere a path does.
- **Downloads are cached** under a cache directory, keyed by URL and revalidated with the
  server's `ETag` or `Last-Modified`, so a query repeated in a loop does not download again.
  `HttpJsonSource` (section 6) shares the same cache and paging.
- **Partial reads over the network.** For `.otab` on a server that supports HTTP range
  requests, the reader fetches the footer and then only the blocks the query needs, as it
  does on disk. Without range support it downloads the file once and reads it locally.
- **Serving tables.** `TableServer` publishes tables and a read-only query endpoint through
  `Web.HTTP.Server`. `GET /tables/runs` returns a table, and `GET /query?sql=...` returns a result,
  as CSV, JSON or `.otab` by the `Accept` header. It answers only `SELECT` (statements that
  change data are refused) and only for the tables it was given. It runs a statement with row
  and time limits, and it can be bound to localhost or put behind the library's existing auth.
  `RemoteSource(url)` on the client side queries a `TableServer` as if it were a local table,
  sending simple filters to the server so only matching rows cross the network.
- **Writing to the network** is export, not shared storage: `ToCsv`, `ToJson` and `Save` accept
  any stream, so a result can be uploaded or sent as a response. Two processes keeping one
  remote table in sync is out of scope, as a multi-user database is.

### 11. Compatibility

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
- **Each source has its own test** of schema, round trip, malformed input, edge cases, pushdown,
  compression, globs and caching, with inputs generated at run time (section 6, "Source tests").
- **Persistence is tested by breaking it** (phase 3): interrupted saves at every step, truncated
  and corrupted files, log replay after a simulated crash, a refused second writer, and both
  serialization forms round-tripped, including a table nested inside another object.
- **Network tests run offline** against an in-process `Web.HTTP.Server` (phase 5), covering
  caching, revalidation, range and non-range servers, and the server's read-only limits.
- Unit tests per phase: column typing and promotion, null bitmaps, view invalidation, each
  predicate node, each join and aggregate, each index kind against a linear scan, prepared
  statements and the statement cache, the compatibility API (the #1291 tests must keep passing
  unchanged).
- **A performance gate.** `programs/regression/query_bench.obs` (the benchmark above, at 100k rows)
  with thresholds as multiples of the plain-array baseline measured in the same run, so it is
  machine-independent: scan and aggregate ≤ 5×, `where` ≤ 5× plus result size, `order by` ≤
  0.3 s, and save and load of one million rows × three numeric columns well under a second. Whether it gates pull requests or runs only in the nightly job follows the repository's
  existing performance gate (`perf-results/PERF_GATE.md`); timing is noisy on shared runners, so
  the nightly job is the likelier home.
- Every phase passes at `-opt s3`, with `OBJECK_JIT_THRESHOLD=1`, and at `-opt s0` with `--jit=off`.

## Phases

Each phase lands as its own pull request, with its tests, a rebuilt `query.obl`, and the editor
index regenerated for new classes.

1. **Columnar storage.**
   - **Storage:** `Column` types, null bitmaps, typing and promotion, dictionary-encoded strings,
     and row groups with zone maps.
   - **Table API:** `Row` as a cursor, O(1) `Get(pk)`, tombstone deletes with `Compact()`, the
     compatibility API on top, `Describe()`.
   - **Generic serialization:** tables survive a `Serializer`/`Deserializer` round trip.
   - **Sources:** the `Source` interface with `CsvSource`, `FilesystemSource` (flat) and
     `ObjectSource`, and `FromCsv`, `FromFilesystem` and `Finder` reimplemented over them.

   Tests: storage unit tests, `query_serialize_generic`, `query_source_csv`,
   `query_source_filesystem`, `query_source_objects`, and the #1291 tests unchanged.
   Benchmark: insert, scan and aggregates.
2. **Views and compiled predicates.** `View`, copy-on-write result tables, the typed predicate
   nodes with batch evaluation over row groups, zone-map skipping, prepared statements and the
   statement cache, and the method-chaining API. The differential test starts here. Also the
   single-file text sources, which need no joins: `TextSource`, `JsonLinesSource`, and
   transparent `.gz`. Tests: `query_source_text`, `query_source_jsonl`, `query_source_gzip`.
3. **Persistence.**
   - **Prerequisites,** each its own small VM pull request first: rename that replaces
     atomically on Windows, and an exclusive-create trap.
   - **The `.otab` format:** `Save`, `Load`, `ToBytes`, `FromBytes`, stream variants,
     per-block compression and CRC32s, and partial reads by column and row group.
   - **Writing:** `Append`, `Open` with the change log, `Checkpoint`, and the writer lock.

   Tests:
   - `query_persist_roundtrip`: every type, `Nil` cells, dictionaries, deleted rows; bytes and
     files.
   - `query_persist_partial`: only the needed blocks are read, counted by an instrumented
     reader.
   - `query_persist_crash`: a save interrupted at each step, a truncated file, a corrupted
     block, a log replayed after a simulated crash.
   - `query_persist_lock`: a second writer refused, a stale lock cleared, readers unaffected.

   Benchmark: save and load at one million rows.
4. **The relational layer.**
   - **SQL:** `Catalog`, hash joins (on dictionary codes where both sides share a dictionary),
     `GROUP BY`/`HAVING`, multi-key `ORDER BY` `ASC`/`DESC`, `LIMIT`/`OFFSET`, and
     `INSERT`/`UPDATE`/`DELETE`/`CREATE TABLE`.
   - **Bridges:** `ToMatrix`, `FromMatrix`, CSV and JSON.
   - **Hierarchical sources,** which need joins to be useful: `JsonSource` and `XmlSource`
     with child tables, the recursive `FilesystemSource` with parent keys, `MATCHES`,
     `Finder->FindTable`, and scan-hint pushdown.
   - **Files named in SQL:** `FROM` and `JOIN` over CSV, TSV, JSON Lines, JSON, gzipped files
     and `.otab`, globs over several files, and the per-path cache.

   Tests: `query_source_json`, `query_source_xml`, `query_source_tree` (the recursive
   filesystem with parent keys), and `query_source_files_in_sql` (globs, `_file`, the cache).
5. **Network.**
   - **Reading:** URLs in every file source, the download cache with `ETag` and
     `Last-Modified` revalidation, range-request reads of `.otab`, and `HttpJsonSource`.
   - **Serving:** `TableServer` with its read-only query endpoint and limits, and
     `RemoteSource` with filter pushdown.

   Tests, all against an in-process `Web.HTTP.Server`:
   - `query_net_read`: CSV and `.otab` by URL, cache hits, a changed file re-fetched, range and
     non-range servers.
   - `query_net_serve`: tables and queries in each format, a change refused, limits enforced.
   - `query_source_http`.
6. **Hash and sorted indexes** and the rule-based planner.
7. **Tier 2 sources**, one pull request each, in this order:
   1. `query_ml.obl` with `MlResultSource`
   2. `ArchiveSource` (tar first, then zip; raw deflate is `Byte->UncompressDeflate`)
   3. `XlsxSource`
   4. `query_odbc.obl` with `OdbcSource` and its pushdown
   5. `query_vision.obl` with `VisionSource`

   Each comes with its own `query_source_<name>` test. Each add-on library also needs a line in
   `core/compiler/build_libs.sh` and in the other hard-coded library lists, all of which
   `tools/cicd/check_doc_lists.py` keeps in step.
8. **Conditional upgrades,** each only when a benchmark shows the need, and behind interfaces
   that already exist, so nothing above changes:
   - a **B+ tree index**, for frequent inserts mixed with range queries that the sorted index
     serves badly;
   - **parallel scans** across row groups, with a multithreaded GC stress test;
   - **indexes stored in `.otab`**, if rebuilding them on load proves slow.

## As built

What the implementation does where it differs from, or settles something left open in, the
design above. Each phase adds its notes here.

**Phase 1 (#1298).**
- `Table->FromCsv` keeps its per-cell typing by storing OBJECT columns, so its results are
  unchanged; `CsvSource` is the typed path.
- An inferred INT column widens to FLOAT when a Float arrives; any other mix promotes to OBJECT,
  keeping every value. A declared column refuses the mismatch instead.
- Dates are stored as epoch milliseconds of their calendar fields, read as UTC, which round-trips
  exactly and orders correctly.

**Phase 2.**
- The predicate combinators are `AndAlso`, `OrElse` and `Negate`: `And`, `Or` and `Not` are
  reserved words in Objeck.
- `Table->Query` copies its result into a table with one bulk gather per column; `Where`,
  `OrderBy`, `Limit` and `Statement->ExecuteView` return views without copying.
- Empty cells sort last in both directions.
- A comparison with an empty cell is false; only `IS NULL` and `IS NOT NULL` test for one.
  There is no three-valued logic.
- JSON Lines arrays are kept as their JSON text until phase 4 makes them child tables.
- `.gz` files are decompressed whole before reading, through `lang`'s gzip.

**Phase 4.**
- The SQL front end is a new recursive-descent parser. Keywords are contextual, so a column
  named `left` or `values` still works where a column is expected. The public `Parser`, `Term`
  and `Token` classes are kept, and every old statement form still works, including the old
  engine's quirks: `from t where ...` with no SELECT, an unquoted word compared as a string, an
  unknown select column read as empty cells.
- Joins are not hash joins: the right side's keyed rows are sorted once and probed by binary
  search, on whole numbers, numbers or text (an encoded column's dictionary is looked up once
  per code). This gives the same complexity as a hash join, with output in a fixed order: left
  rows in order, each with its matches in the right side's order. The result is a working
  table of only the columns the query reads, rather than a `JoinView`. A WHERE condition on
  one table is applied before the join, except on the right of a LEFT JOIN.
- GROUP BY sorts the rows by the grouping columns (stable, typed) instead of hashing them, so
  groups come out in key order with empty keys last. Aggregates read numbers only. COUNT is
  INT; SUM, MIN and MAX of an INT column stay INT; the rest are FLOAT.
- INSERT, UPDATE and DELETE apply in full or not at all. A bound Nil is NULL, and an unbound
  placeholder is an error.
- A hierarchy's keys are `_id` and `_parent_id` rather than `id` and `parent_id`, so they cannot
  clobber a field of the same name in the data. A table has `_id` only when a child table
  refers to it. JSON object members come out alphabetically, as the JSON parser gives them.
- XML attributes are `@name` columns and an element's own text is `_text`; the SQL lexer reads
  `@name` as a name.
- `MATCHES` is a whole-value match, as `Finder` has always used.
- Files named in SQL are cached per `Catalog`, not process-wide, so the cache needs no lock and
  holds no global state. `Catalog->Run` is the stateless one-off form. Scan hints are not
  pushed into files named in SQL, because the cached table must serve later queries that need
  other columns.

**Phase 5 (reading).**
- Downloads are kept under the system temporary folder in `objeck_query_cache`, revalidated
  with `If-None-Match` and `If-Modified-Since`. A new copy is written beside the old and renamed
  into place. Without a validator, the content's size and CRC32 tell versions apart.

**Phase 5 (serving).**
- `TableServer` lives in its own library, `query_server.obl` (bundle
  `Query.Structured.Server`), so a program that only queries does not link `Web.HTTP.Server`.
  It answers `GET /tables` (names, columns, row counts as JSON), `GET /tables/NAME`, and
  `GET /query?sql=` or `POST /query` with the statement as a UTF-8 body. The POST form exists
  because `Web.HTTP.Url`'s percent-encoding does not carry non-ASCII text intact.
- Only a SELECT is answered (403 otherwise). Its catalog has file access turned off
  (`Catalog->SetFileAccess(false)`), so a query naming a file or a URL is refused (403): it
  would otherwise read the server's disk or make the server fetch an address for the client.
  A result over the row limit is refused (413) rather than cut short. Queries run one at a time
  under a mutex, since a catalog's plan cache is not shared between threads. There is no time
  limit yet: the engine has no cancellation point to stop a running query at.
- `Web.HTTP.Server` runs one server per process, so one `TableServer` serves at a time.
  Binding to localhost only needs a listen address, which the server does not take yet.
- `RemoteSource` sends the scan hint's columns and limit and a condition given with
  `SetWhere`; it does not infer the condition from the local query, because sources are loaded
  before a query runs. The server applies the condition and the local query applies its own
  WHERE again, so pushdown never changes an answer. Results travel as `.otab`.
- `OtabFile->New(url)` reads with range requests: the header, then the footer, then only the
  blocks a `Read` names. A server that answers 200 instead is read from that one download.
- Found on the way and fixed at the source: `Web.HTTP.Server`'s POST requests had no path or
  query string (`GetPath()` was Nil), so a POST handler could not route.

**Phase 7 (`query_ml.obl`).**
- `MlResultSource` lives in its own library, `query_ml.obl` (bundle `Query.Structured.ML`), so
  a program that only queries files does not load `System.ML`'s native module. It is filled
  with `Add` calls, each filed under a run name, and `Load` puts eight tables into a catalog,
  empty ones included: `folds`, `predictions` (row, fold, actual, score, predicted),
  `thresholds` (a full sweep from `Metrics->OperatingPoints`, or single `ThresholdResult`s),
  `confusion`, `features`, `intervals` (bootstrap), `groups` (`RecallByGroup`) and `metrics`
  (named numbers, including each cross-validation's fold means and spreads). A constructor
  prefix renames them all, for a catalog that already has a `folds` table.
- `CrossValResult` does not record which fold scored a row, so `AddCrossValidation(run,
  result, y, seed[, strata])` rebuilds the folds with `StratifiedKFold` from the same labels
  (or strata) and seed, which is how `Evaluate` assigned them. The test proves the rebuild by
  recomputing each fold's AUC from its own `predictions` rows.
- System.ML reports a metric it cannot compute as -1.0 (a fold holding one class); that is an
  empty cell, not a number, so `AVG` skips it. A rate with no denominator is empty too.
- System.ML has no feature importances, so `features` takes any per-feature numbers: a linear
  model's weights or importances computed elsewhere.
- The matrix bridge back is `MlColumns`: `Labels` (a BOOL column as is, a number above 0.5 as
  `CrossValidation->ToBool` reads it, or text equal to a named positive class), `Values` and
  `Features` (`Table->ToMatrix`). An empty cell or text where a number is expected is an
  error naming the column and row, never a silent zero. These close the two seams that had
  every caller hand-writing the same glue: `Float[]` against `Bool[]` labels, and no way to
  pick a fold's rows (now `where fold = 2`).

Toolchain defects found while building these phases were fixed at the source rather than worked
around: gzip on small inputs (#1296), atomic rename on Windows (#1297), ten compiler defects
(#1299), HTTP status codes and a TLS/collector deadlock (#1300), and `XmlElement` attribute order.

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
- Whether `ObjectSource` can read every field through `System.Introspection`, or only public ones,
  needs checking against the reflection API before phase 1; the function-per-column form covers
  whatever it cannot.
- How a JSON or XML document with irregular structure (the same key holding an object in one
  record and a string in another) should map: promote the column to `ObjectColumn`, or split it.
