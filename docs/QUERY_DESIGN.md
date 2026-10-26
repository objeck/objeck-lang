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
- **Concurrent writers.** Writes to a table are not locked; a program that writes a table while
  other threads use it orders them itself (`critical`). Any number of threads may query one
  table, or one catalog, at once ("Concurrent readers" and "One catalog, many threads" below).

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

**Expressions in the select list.** `expr` is a column, a number, or arithmetic over them:
`+ - * / %`, with `*`, `/` and `%` binding tighter, unary minus and parentheses
(`select id, score * 100 as pct, (hi - lo) / 2 from runs`). Values follow the aggregates' rule
of reading numbers only:

- whole numbers stay whole under `+ - *` and `%`; `/` always gives a Float (`7 / 2` is 3.5), and
  a Float anywhere gives a Float (`%` of Floats keeps the dividend's sign);
- an empty cell, a cell that is not a number, and a zero divisor give an empty cell rather than
  an error (a Float division by zero would otherwise stop the program);
- the default label is the expression as written (`n + 1`); `AS` names it, and `ORDER BY` can
  use that name or the position;
- `n -1` (a name, then a number with its sign) is a subtraction, as it reads.

Expressions are computed after WHERE, over the rows that pass, before DISTINCT, ORDER BY and
LIMIT. Not yet: expressions in WHERE, ON, GROUP BY or HAVING, or in a grouped or aggregate
query's select list (`sum(x) / count(*)`): each is refused with an error that says so, never a
silent Nil. Every statement that fails returns Nil with `GetError` set; a program that calls a
method on that Nil gets the language's ordinary `NilError`, which a `?->` region recovers from.

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
  first, with the gzip support already in `lang`. Archives and workbooks are named the same
  way, with a member or sheet after a dot: `from 'data.zip'.'sales/2024.csv'`,
  `from 'budget.xlsx'.'Q1'` ("Archives and workbooks in SQL" below).
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
| `query.obl` (no native code) | `ArchiveSource(path)` | Zip and tar contents as a tree, like `FilesystemSource`, and any member readable as a source, such as a CSV inside a zip. Tar and stored zip members need only `lang`; deflated zip members use `Byte->UncompressDeflate` (raw deflate, RFC 1951) from `lang`. Built in phase 7; see "As built". |
| `query.obl` | `XlsxSource(path[, sheet])` | Each worksheet as a table, with typed cells and the first row as the header. An `.xlsx` file is a zip of XML parts, so this builds on `ArchiveSource` and `XmlSource`. Built in phase 7; see "As built". |

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

> **As built:** [OTAB_FORMAT.md](OTAB_FORMAT.md) is the byte-exact specification of versions 1
> and 2 (2 adds index definitions; see "Indexes in .otab files" below), and it differs from the
> plan below in four ways. The schema sits in the footer, not the
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

**Indexes are rebuilt on load**, since hash and sorted indexes rebuild in one pass. The file
keeps their definitions (format 2, "Indexes in .otab files" below), not their entries.

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
   - **index entries stored in `.otab`**, if rebuilding them on load proves slow (their
     definitions already are: format 2).

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
  with `If-None-Match` and `If-Modified-Since`. Without a validator, the content's size and
  CRC32 tell versions apart. Each URL has a folder of versions and a note naming the current
  one, which threads and processes share ("One cache folder, many processes" below).

**Phase 5 (serving).**
- `TableServer` lives in its own library, `query_server.obl` (bundle
  `Query.Structured.Server`), so a program that only queries does not link `Web.HTTP.Server`.
  It answers `GET /tables` (names, columns, row counts as JSON), `GET /tables/NAME`, and
  `GET /query?sql=` or `POST /query` with the statement as a UTF-8 body. The POST form exists
  because `Web.HTTP.Url`'s percent-encoding does not carry non-ASCII text intact.
- Only a SELECT is answered (403 otherwise). Its catalog has file access turned off
  (`Catalog->SetFileAccess(false)`), so a query naming a file or a URL is refused (403): it
  would otherwise read the server's disk or make the server fetch an address for the client.
  A result over the row limit is refused (413) rather than cut short. Requests are answered at
  once, each on its own thread; the program changes a published table only between
  `BeginWrite` and `EndWrite` ("A server that answers at once" below). There is no time
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

**Phase 6 (indexes and the planner).**
- The API is `table->CreateIndex(column, IndexKind->HASH | SORTED)` (or with a name first),
  `DropIndex`, `HasIndex`, `GetIndexKind`, `GetIndexName` and `FindIndex(name)`; in SQL,
  `CREATE INDEX [name] ON t (col) [USING HASH | SORTED]` (SORTED when `USING` is left out, as
  section 5 recommends; `USING BTREE` is refused until phase 8 builds one) and
  `DROP INDEX name [ON t]`. An unnamed index is called `table_column`. A column has at most
  one index; creating another replaces it. `pk` needs none (`Get` finds a key directly).
- The **plan** is reported, not just chosen: `View->GetPlan()`, `Statement->Explain()`,
  `Table->Explain(sql)` and `Catalog->Explain(sql)` give steps such as
  `where runs: HASH index runs_grp on grp (=)`, `where runs: SORTED index runs_value on value
  (>= <=)`, `where runs: union(...)`, `where runs: scan` and `order runs.value: SORTED index
  runs_value`. Explain runs a SELECT to find out; for UPDATE and DELETE it plans the WHERE
  clause and changes nothing.
- The index lives in its `Column`, not the `Table`, so every path that writes a cell reaches
  it: `Column->Set` (the one cell-write path, used by `SetCell`, UPDATE, INSERT, `Row->Set` and
  log replay) and `CopyFrom` report each write; `Table->Delete` reports each delete. A **hash**
  index is updated in place (an `IntMap` from key to the first slot of a chain, linked through
  per-slot arrays, so adding or removing a slot touches only its neighbours). A **sorted**
  index is marked stale by any write and rebuilt by the next query that uses it; a delete does
  not make it stale, since lookups skip deleted slots. Anything that rewrites a column whole
  (Compact, Gather, Restore, the Fill paths, a promotion to FLOAT or OBJECT, a dictionary
  turning into plain text) marks either kind stale. `CreateIndex` builds at once.
- Keys: INT, BOOL and DATE by value; an encoded STRING by dictionary code (codes are per
  column, and Compact keeps the dictionary, so they stay valid); a plain STRING by its hash;
  a FLOAT by a key under which equal numbers meet (`0.0` and `-0.0` together). Keys may
  collide: **a lookup gives candidate slots, a superset of the matches, and the whole
  condition is then tested on the candidates in slot order.** That is why results, and their
  order, are identical to a scan's by construction, and why an index can only change speed.
  The differential test (`query_index_differential`) checks it.
- OBJECT columns are **not indexable**: their cells mix kinds with no one order or key.
  `CreateIndex` refuses one; a column promoted to OBJECT keeps its index, which serves
  nothing (the plan says scan) until it is dropped.
- The planner, as section 5 says: for an AND, a term that reads no rows (a value no cell holds,
  a NULL value), then equality on a hash index, then the index probe reading fewest slots
  (exact for a sorted range: the binary search gives it); ranges on one sorted index are
  narrowed together, so `BETWEEN` is one range. An OR (so IN) uses indexes only when every
  alternative has one, as a union. NOT, `<>`, LIKE, MATCHES, IS [NOT] NULL, a value of another
  type and NaN always scan. Index probes are made only over a whole table; a view's `Where`
  filters its own rows. Prepared statements plan with the bound values.
- A sorted index also gives **ORDER BY** (`View->OrderBy` and SQL, one key or the last-applied
  key of several), **GROUP BY**'s grouping order on one table, and `Table->Min`/`Max` of an
  INT or FLOAT column. Equal values keep row order and empty cells come last, as Sorter does.
  Joins, DISTINCT and SQL aggregates do not use indexes: the join's key matcher builds its own
  order across mixed key domains, and DISTINCT and grouping by hash would not keep the order
  the engine gives, so none of them is a clean fit yet.
- Indexes were not stored in `.otab` files when phase 6 landed; their definitions are now
  (format 2, "Indexes in .otab files" below). Query results and other copies have none. The
  generic `Serializer` carries them with the table (they hold only arrays and an `IntMap`, and
  string hashes are computed from the text, so they stay valid).
- A lazy rebuild writes the index; several threads may query a table at once, including right
  after a change, as "Concurrent readers" below describes.
- Measured at 1,000,000 rows (`perf-results/libs/query_index_bench.obs`, Windows x64,
  `-opt s3`), per query, scan against index: equality on an Int with one row per value
  73 ms against 0.02 ms; equality on text with 1,000 rows per value 47 ms against 0.7 ms; a
  0.1% range 109 ms against 0.7 ms; a 10% range 54 ms against 16 ms; `ORDER BY value LIMIT
  10` 2.8 s against 0.08 s. Building an index takes 0.2-0.6 s.
- Defects found by the differential test and fixed at the source: zone maps kept Int bounds
  as Floats and skipped groups strictly, so `n > 2^53` missed `2^53 + 1`
  (`query_zone_map_wide_ints`); and NaN compared equal to every number, so a NaN cell matched
  `=`, `<=` and `>=` against anything, or not, depending on zone-map skipping, a group whose
  first value was NaN was skipped for every comparison, and where NaN sorted depended on the
  sort's steps. NaN now satisfies only `<>`, like any value that cannot be ordered, zone maps
  ignore it, and it sorts after every number (`query_nan_compare`).

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

**Phase 7 (`query_vision.obl`).**
- `VisionSource` lives in its own library, `query_vision.obl` (bundle
  `Query.Structured.Vision`), so a program that only queries files needs neither ONNX
  Runtime nor OpenCV. Linking it loads neither: a native module loads on the first call into
  it, so a folder can be listed with no model and no OpenCV installed.
- Two tables. `images`: id, path, name, folder (the subfolder below the root, which is how a
  per-camera folder layout becomes a GROUP BY), format, width, height, channels, bytes,
  modified, detections (a count, empty with no detector) and error. `detections`: id,
  image_id, path, label, class_id, score, x, y, width, height and area, with the box in the
  image's own pixels. Files are taken by extension (.png, .jpg, .jpeg, .bmp, .gif, .webp),
  in name order, subfolders after their parent's files, hidden folders skipped.
- Format, size and channels come from `ImageHeader`, a pure-Objeck reader of PNG, JPEG (the
  first start-of-frame segment), BMP (either row order), GIF and WebP (lossy, lossless and
  extended) headers, so metadata needs no decoder. A file whose bytes are not the image its
  name says, or that a detector fails on, is a row with an `error`, not a failed load.
- The detector is the `Detector` interface, as the design says, so the test drives the
  source with a scripted detector. `YoloDetector` adapts `API.Onnx.YoloSession` (YOLO 8
  and later); boxes come back in image pixels because the native code undoes its letterbox.
  The test runs it on the repository's sample photos when `OBJECK_YOLO_MODEL` and
  `OBJECK_YOLO_LABELS` name a model, and checks every box lies inside its image; where
  OpenCV is deployed it decodes the generated PNG and BMP and writes a JPEG and a WebP whose
  headers must agree.
- A scan hint's row limit caps the images read; its columns are not used, since the two
  tables share column names.

**Phase 7 (`ArchiveSource`).**
- One source reads `.zip`, `.tar` and gzipped tar (`.tar.gz`, `.tgz`), told apart by content
  (gzip magic, then a tar header whose checksum holds, then a zip end record), not by name. It
  reads from a file, a URL (through the download cache) or bytes in memory
  (`ArchiveSource->FromBytes`). It lives in `query.obl` with no new library.
- Tables: a listing named after the archive (`results.zip` gives `results`; `SetName`
  changes it), and one table per member in a readable format, named by the member's full
  path (`data/runs.csv`), which SQL names in double quotes. A JSON or XML member's child tables
  follow its own rules (`meta.json_tags`). Formats follow the extension as files in SQL do,
  with two differences: `.txt` is not read as CSV (archives carry READMEs), and an XML member
  becomes a table only once `SetRowElement` names its row element. `SetPattern` (a glob over
  member paths) chooses which members become tables. `ReadBytes`, `ReadText` and `ReadTable`
  read single members.
- Listing columns: FilesystemSource's tree columns (`id`, `parent_id`, `depth`, `name`, `path`,
  `ext`, `is_dir`) plus `type` (`file`, `dir`, `symlink`, `hardlink`, `other`), `size`,
  `compressed_size`, `method` and `crc32` (zip only), `modified` and `link`. A folder the
  archive implies but does not list gets a row, so every member has a parent; a tar's `./`
  root has none.
- Modification times: a zip's MS-DOS time is local time with no zone and two-second steps; an
  extended timestamp field (0x5455), where present, is used instead and is UTC. Tar times are
  UTC, including a pax `mtime` (its fraction dropped).
- Safety, since an archive is untrusted input. Nothing is extracted to disk. A member whose
  path is absolute (`/x`, `C:\x`) or contains a `..` part refuses the whole archive, naming it,
  as do duplicate names in a zip (the case behind several extraction exploits) and zip
  members whose data overlap (the "overlapping files" bomb). A tar may repeat a path, as
  appending does; the last copy is the table, and the listing shows both.
- Zip bombs: each member is inflated with the new `Byte->UncompressDeflate(input, limit)`,
  limited to the size its central directory states, so a member that expands further is
  refused after producing at most that many bytes, and one that inflates to less is refused
  too. Every member is then checked against its CRC32. A member over 256 MB, or a read taking
  the archive's total past 1 GB, is refused (`SetLimits` changes both). A gzipped tar, or a
  gzipped member, is inflated with `Byte->UncompressGzip(input, limit)` within the total.
  Before these two overloads the VM's own 512 MB inflate buffer was the only bound.
- Zip: stored and deflate members; ZIP64 end records and extra fields; a self-extracting
  prefix (offsets are found from where the central directory actually sits); data
  descriptors (the central directory's sizes are used). Refused with a reason: encryption,
  any other method (named: `bzip2`, `lzma`, `zstd`, ...), archives split across disks, a local
  header that names a different member, and ZIP64 values with no ZIP64 record.
- Tar: POSIX ustar with its name prefix, pax extended headers (`path`, `linkpath`, `size`,
  `mtime`; global headers are skipped), GNU long names and link targets (`L`, `K`), base-256
  sizes, and both checksum conventions. Links are listed with their targets and never
  followed. A tar cut at a block boundary has only lost its end marker and still reads; one
  cut inside a header or a member's data is an error.
- Loading is all or nothing: a member that does not parse fails the load with the archive,
  member and reason, and nothing reaches the catalog. Scan hints are not pushed into members.
- Not done at first: archives named in SQL, since added ("Archives and workbooks in SQL"
  below). Still not done: reading a member without holding the whole member in memory.

**Phase 7 (`XlsxSource`).**
- Each worksheet becomes a table named after its sheet (`SetPrefix` prepends text, so two
  workbooks' `Sheet1` can share a catalog); `XlsxSource->New(path, sheet)` reads one. Chart
  and dialog sheets are skipped; hidden sheets are read. Parts are found through the package
  and workbook relationships, not by fixed names, and the zip is read with `ArchiveSource`'s
  reader, so its CRC32 checks and limits apply to every part.
- Cells: shared strings (rich-text runs joined, phonetic runs skipped), inline strings and
  formula strings are text; whitespace is kept, through the new `XmlElement->GetDecodedText()`
  (`GetContent` trims, which lost `xml:space="preserve"` text). Booleans are `0`/`1`; error
  values (`#DIV/0!`) are their text; a formula cell is its cached value, and one with no
  value, or an empty one (openpyxl writes `<v></v>`), is an empty cell. ISO 8601 `d` cells
  are dates.
- Dates are numbers whose cell format shows a date or time: built-in formats 14-22, 27-36, 45,
  47 and 50-58, and custom codes with `d`, `m`, `y`, `h` or `s` outside quotes, escapes and
  brackets. Elapsed-time formats (`[h]:mm`, built-in 46) stay numbers of days, since they are
  durations. The 1904 system is honoured. In the 1900 system serials below 60 move a day to
  undo Lotus 1-2-3's phantom 29 February 1900, and 60 itself reads as 28 February 1900 (as
  openpyxl does). Times round to the nearest second; dates are wall-clock (not GMT).
- Typing reads whole columns, as `CsvSource` does: whole numbers INT, other numbers FLOAT,
  dates DATE, booleans BOOL, anything else or any mix STRING, with each cell as it would
  display (`1.5`, `TRUE`, `2026-03-15`, `2026-03-15 10:20:30`).
- Shape: the used range, from the first row holding a value (or `SetFirstRow`) to the last,
  and from the first used column to the last. A missing row or cell is empty, so table rows
  line up with sheet rows. The first row names the columns by default; an empty header cell
  is named by its column letter, and a repeated name gets `_2`, `_3`. `SetHeader(false)`
  names every column by letter. A range of more than 50 million cells is refused rather than
  allocated.
- Merged cells: decided to keep Excel's storage by default (the value in the top-left cell,
  the rest empty), because filling changes column types and counts; `SetFillMerged(true)`
  copies the value across the range.
- Errors name the workbook, sheet and cell: a missing part, malformed XML, a shared string
  index out of range, a cell reference outside its row or malformed, a number that is not
  one. Loading is all or nothing.
- Not done: reading formulas themselves (only their values) and styles beyond date
  detection. `.xlsb` and the old `.xls` are other formats and are not read. Workbooks named in
  SQL were added later ("Archives and workbooks in SQL" below).

**Phase 7 (`query_odbc.obl`).**
- `OdbcSource` lives in its own library, `query_odbc.obl` (bundle `Query.Structured.Odbc`), so
  a program that only queries files does not load the ODBC native module. It reads a table
  (`New(connection, table)`, with `SetSchema`) or a query's result (`New(connection, name,
  sql)`).
- Columns are typed from the driver's description (`SQLDescribeCol`), not by scanning values:
  integer types and NUMERIC/DECIMAL with no fraction digits (up to 18 digits) are INT; other
  numeric types FLOAT; BIT BOOL; DATE and TIMESTAMP DATE, to the second; TIME is text; binary
  columns are lowercase hexadecimal text (`OdbcSource->FromHex` turns it back into bytes,
  since the engine has no byte column); anything else is text of any length. NULL is an empty
  cell. A column the driver describes as text holds text even when it holds numbers.
- Pushdown, the design's "simple `where` terms": the scan hint's columns (only those the
  result has, by name then ignoring case) and row limit, and a condition given with
  `SetWhere`, either raw SQL or with `?` placeholders and bound values. As with
  `RemoteSource`, the condition is not inferred from the local query, which applies its own
  WHERE again. Names are always quoted, with the closing quote doubled inside them
  (`SetQuotes` for `` ` `` or `[]` styles), so a name cannot end its identifier; values go in
  as parameters, never as SQL text. The limit is `LIMIT n` by default, `TOP n` or `FETCH
  FIRST n ROWS ONLY` by `SetLimitSyntax`, and the reader stops at the limit either way. A
  query is wrapped as a derived table (`FROM (sql) q`) only when something is pushed into it,
  so its own ORDER BY and column names otherwise reach the table unchanged. `GetSql` shows
  what was sent.
- Found on the way and fixed at the source, in `libobjk_odbc` and `odbc.obs`: text was read
  through one 8 KB buffer, so a longer value was silently cut short, and a NULL was converted
  from that buffer before anything was written to it; `GetBlob` cannot report a value's
  length, so `ResultSet->GetBytes` returns exactly the bytes; and a SELECT the database
  refused freed its statement with its diagnostic, so it read as an empty result with no
  reason. `ResultSet->IsOpen` now tells a refused statement apart, and its `GetLastError`
  gives the database's reason (`programs/regression/odbc_whole_values.obs`). A prepared
  statement that is run several times, and a blob with zero bytes bound by `SetBlob`, were
  fixed separately in #1337 (statement-owned maps and parameter copies).

Toolchain defects found while building these phases were fixed at the source rather than worked
around: gzip on small inputs (#1296), atomic rename on Windows (#1297), ten compiler defects
(#1299), HTTP status codes and a TLS/collector deadlock (#1300), `XmlElement` attribute order,
no way to bound how far deflate or gzip data inflates (phase 7: two new traps), and `XmlElement` text
that could only be read trimmed (phase 7: `GetDecodedText`). Also in
phase 7: the label merging at `-opt s2`/`s3` could give a merged label the id of another label
in a linked library method, so adding classes to `query.obl` made a query loop forever.

## Concurrent readers

**The contract.** One thread writes a table, or several threads read it, never both at once:
writes are not locked, and a program that mixes them must order them itself (as `TableServer`
does with its readers-writer lock). Within that contract, any number of threads may query one table at the
same time, including the first queries after a write, and every answer is the answer a scan
gives on one thread. `Table->GetError` reports the calling thread's own last error ("Errors
per thread" below), and a `Catalog` may be shared too ("One catalog, many threads").

**What a read writes.** Three things a query may rebuild on a table it does not otherwise
change, and each was a defect under concurrent readers:

| State | Made stale by | Defect before |
|---|---|---|
| A sorted index (and a hash index after a whole-column rewrite) | a cell write; Compact, promotion and the other whole-column rewrites | two threads rebuilt it at once, each publishing its own copy; harmless only while the fields happen to be seen in program order |
| A row group's zone map | a cell write that replaces a value | the bounds were reset to "no values" and marked fresh *before* being recomputed, so a reader arriving meanwhile skipped the group and lost its rows (wrong answers, reproduced) |
| A table's plan cache (`Table->Query`, `Execute`, `Prepare`) | a statement not seen before | every thread that missed inserted into one `Map` at once, corrupting its tree: a crash or a query that never returns (reproduced) |

**The fix: one rebuild lock, taken only on the slow path.** A `ThreadMutex` held in a class
variable (`RebuildLock`), one per process. It cannot be a field of `Table`, `Column` or the
index: the generic `Serializer` would copy its native handle (section 9 keeps every field
serializable). It is created by the first `Table` constructor, before any table exists to be
shared, so threads never race to create it.

- An index or zone map is checked without the lock. When stale, the thread takes the lock,
  checks again (another thread may have rebuilt it meanwhile), and rebuilds: the new state is
  built aside in locals, stored in its fields, and marked fresh last. A reader without the lock
  sees either a stale flag (and waits for the lock) or a whole state.
- The plan cache is read and written under the lock; parsing a missed statement happens
  outside it, so two threads may parse the same text and the later insert wins (plans are
  immutable after parsing, so either serves).
- One lock for all tables keeps the cost of a fresh read at zero and is held only while
  something is rebuilt; a thread rebuilding a large sorted index delays only other rebuilds.

**The test.** `query_index_concurrent` writes to a shared table, then starts four threads that
meet at a barrier before each query (ranges, equality, ORDER BY and Min/Max through the
indexes, ranges that scan with zone maps, and SQL through the plan cache) and compares every
answer with one an unindexed twin gave on one thread. Before the fix it failed in every mode:
short answers from skipped groups, a Nil dereference, or a hang in the plan cache's `Map`.

## Errors per thread

**The gap.** A table kept one `@error` for every thread querying it, and so did a catalog:
`GetError` answered whichever thread failed last, and a thread whose query succeeded could
read another thread's failure.

**The fix: each thread reads its own.** `GetError` keeps its signature and its meaning on one
thread; with several, it answers the calling thread's last error on that object. Alternatives
weighed:

- *Return the error with the result.* A failed query returns `Nil`, which carries nothing, and
  `GetError` would still have to work; it would also mean a second API for every call.
- *A thread-local slot in the VM.* Objeck had no thread-local storage and no way to name the
  calling thread at all (`Thread->GetExecuteID` names a `Thread` object, and the program's
  first thread has none), so something had to be added to the runtime either way.

So the runtime gained the least it could, two functions on `System.Concurrency.Thread` (new
traps `THREAD_CURRENT_ID` and `THREAD_IS_ALIVE`, appended after the others):
`Thread->GetCurrentId()` gives the calling thread a number, counted up from 1 the first time
it asks and never given to another thread, even after this one ends (an operating system's
thread ids are reused); `Thread->IsAlive(id)` says whether that thread still runs (the VM
keeps the numbers of running threads in a set a `thread_local`'s destructor leaves).

A table and a catalog hold a `ThreadErrors`: a `Map` from thread number to message, `Nil`
while no thread has an error pending. A failed call sets the calling thread's entry, the next
call that succeeds removes it, and the entries of threads that have ended are dropped whenever
the map changes, so a thread that ends with an error unread leaves nothing behind for long.
The common case costs one field read and no lock: a call that succeeds on an object with no
pending errors sees the map is `Nil` and returns. That is safe without the lock because only a
thread adds its own entry and the map goes back to `Nil` only when empty, so a thread that
sees `Nil` has no entry. The lock, held only while the map is read or changed, is a class
variable for the reason `RebuildLock` is one: the map is serializable, a lock is not.

A table or catalog carried through the generic `Serializer` carries its pending errors, as it
carried its one error before; the numbers are this process's, so a copy read in another process
may answer a stale error on a thread that has the same number until that thread's next call.

`Statement`, `View`, `Query`, `OtabFile`, `TableStore` and the sources keep one error each:
each is one thread's object (a statement holds its bound values; a view, a file and a source
are made by the call that uses them), and "One catalog, many threads" says so where it matters.

**The test.** `query_thread_errors`: four threads fail at once on one table and one catalog,
each for its own reason (an unknown column or table named after the thread, a syntax error at
a position of its own) while one of them succeeds, then read `GetError` after all have
finished: each must see its own reason, the one that succeeded none, and the main thread,
which never failed, none, over 40 rounds of new threads. Before the fix it failed in every
mode: 640 of its 884 checks, or, in runs where the threads' statements corrupted the catalog's
unlocked plan cache, a crash in `Map->Insert` or a hang.

## One catalog, many threads

**The gap.** A catalog was one thread's: its table map, its plan cache and its file cache were
`Map`s written without a lock, and a file named in SQL was read by every thread that missed
the cache. Two threads querying through one catalog could corrupt a map (a crash in
`Map->Insert`, a Nil dereference, or a query that never returns, all reproduced) and read the
same file twice.

**The contract.**

| Call | With other threads querying the catalog |
|---|---|
| `Query`, `Prepare`, `Explain`, `Get`, `Has`, `GetNames`, `GetFileReads`, `GetError`, and files, archives and workbooks named in SQL | safe; any number at once |
| `Add`, `AddNew`, `Remove`, `Load`, `CREATE TABLE` | safe: a query sees the table map before or after the change; a query already running keeps the tables it found |
| `INSERT`, `UPDATE`, `DELETE`, `CREATE INDEX`, `DROP INDEX` (through `Execute` or a statement) | they write a table, so the table's contract applies: no other thread may use that table meanwhile. Writing a table no other thread uses (one a thread created for itself) is safe |
| `SetFileAccess`, `SetArchiveLimits` | apply to queries that start after them; a query running meanwhile may read either value. Set them before sharing the catalog |
| a `Statement` from `Prepare` | belongs to the thread that binds it; each thread prepares its own (a plan cache hit, so cheap) |

**Locks, all on slow paths or held for a map operation only** (`CatalogLocks`, class variables
made by the first table or catalog, before there is one to share):

- *The catalog lock*, one per process, held only while a map of any catalog is read or changed:
  a table lookup, a plan cache lookup or insert, a file cache lookup or insert, the read count.
  Parsing a statement and running a query happen outside it: two threads that miss the plan
  cache for one statement both parse it and the later insert wins, as in a table's cache.
  `CREATE TABLE` still refuses a name it finds, and then adds through `AddNew`, a check and an add
  in one step, so of threads creating
  one name at once exactly one succeeds and the others are refused, where before both passed
  the check and the second silently replaced the first.
- *Sixteen file locks*, chosen by a hash of the file's cache key (or of a URL), for single-flight
  loading. A query checks the file's stamp (its names, sizes and modification times) against
  the cache without a file lock; on a miss it takes the file's lock, checks the cache again, and
  only then reads, so threads that miss together read the file once and the rest find it
  cached when the lock comes free. Files that share a lock by their hash wait for each other's
  reads, which only costs time, and only when both are being read.
- *URLs.* The revalidating request (`If-None-Match`, `If-Modified-Since`) goes out with no lock
  held, so concurrent queries of one URL revalidate concurrently. Before, two threads
  downloading one URL wrote one `.part` file at once, and the loser's rename failed its query;
  this section first fixed that with the URL's file lock around the copy and its note, which
  kept threads apart but not processes. The download cache now needs no lock at all ("One
  cache folder, many processes" below): a fetch returns a version that never changes, with the
  stamp that belongs to it, and a catalog reads that version into a table under the URL's file
  lock only so that threads that miss together read it once.

The locks cannot deadlock: nothing waits for a file lock while holding another lock, a thread
holding a file lock may take only the catalog lock or an error lock, and those two take no other
lock while held (an error lock asks the VM whether threads are alive, under the VM's own lock).

Two defects found on the way and fixed: an `INSERT` with no column list filled the column list
of the cached statement with the table's columns on its first run, so after `AddColumn`, or
with another table under that name, the same statement kept inserting into the old columns
(now each run takes the table's columns as they are; `query_sql_relational` covers it); and the
`CREATE TABLE` race above.

`TableServer` answered one request at a time under its mutex until it, too, was made to answer
at once ("A server that answers at once" below).

**The test.** `query_catalog_concurrent`: six threads share one catalog over 30 rounds. Each
round rewrites a CSV and a zip a little longer (so every thread misses the file cache for them
at once), computes every answer alone with an oracle catalog, then the threads meet at a
barrier before each statement: files in SQL (a CSV, a glob over three, members of a zip, a
worksheet), joins of files with each other and with the catalog's tables, `GROUP BY` and
`HAVING`, a statement new to the plan cache every round, statements that fail, a failure of
each thread's own whose reason it must read back, prepared statements bound per thread, and a
table each thread adds (`Add`) and creates and fills (`CREATE TABLE`, `INSERT`) while the others
query. Every answer must equal the oracle's, and the shared catalog must read files exactly as
often as the oracle did alone (once per changed file per round). Before the fix it crashed in
`Map->Insert` or on a Nil, or hung, in every mode; it passes at `-opt s3` and `s0`, with and
without the JIT, and with `OBJECK_JIT_THRESHOLD=1`. `query_net_read` adds the URL half: four
threads sharing a catalog query a URL with no validator (so every query downloads it) and one
that answers 304, over eight rounds that each change the first; every answer must be right and
the changed file read once per round. Before the fix a thread's query failed with "cannot
write its copy" in the first round of every run.

## One cache folder, many processes

**The gap.** The download cache lives in one folder under the system's temporary folder, so
every process on a machine shares it, and "One catalog, many threads" made it safe only within
one process: the URL's file lock kept threads apart, and nothing kept processes apart. A URL
had one copy and one note (its `ETag`, `Last-Modified` and stamp), each replaced by a rename,
so two processes could:

- *fail a query.* On Windows a rename cannot replace a file another process has open, and a
  reader holds the copy open while it reads it, so the writer's rename failed and so did its
  query ("cannot write its copy"). With six processes querying a changing file, about one
  query in ten failed.
- *pair a copy with another download's note.* The copy and the note were two renames. Two
  processes that downloaded different versions could leave one's copy beside the other's note,
  and a note's validators are what the next fetch sends: with the newer note beside the older
  copy, the server answers 304 and the older copy is served as current until the file changes
  again. A reader could also read a note, then a copy another process had replaced since.

**The design: immutable versions and one small note, published by one rename.** Each URL has a
folder of its own (named by the URL's hash) holding:

- *versions*: each download, under a name no other writer uses (`PID-THREAD-COUNT_name.csv`:
  the process id, the thread's number from `Thread->GetCurrentId`, and a count of this process's
  writes; a name a process with the same id left behind is skipped). A version is written once,
  whole, and never changed, so a reader never sees it change, and since no other writer uses its
  name, writing it cannot fail because another process reads something. The URL's own file name
  ends it, so its extension still says how to read it.
- *the note*, `current`: four lines, the `ETag`, the `Last-Modified`, the stamp of the version
  and the version's name. It is written under a name of the writer's own and renamed over the
  old note: one rename of one small file publishes a version together with its validators. A
  reader reads the note once and uses the version it names, so the validators it sends are
  always the version's own and a 304 answer means that version is current.

A fetch reads the note, revalidates with its validators, and on a 304 returns the version the
note named. On a 200 it reads the note again: if it names the same content (the same stamp:
another process published it meanwhile, or a server without validators sent it again), it
returns that version and writes nothing; otherwise it writes a new version, publishes it, and
returns it. A fetch always returns the version it decided on, whether or not its note is the one
left in place, so two processes that download at once each get a whole, correct answer, and the
last note wins, which costs at most one more download if it names the older version.

The note is the only file ever replaced, and readers hold it open only for one small read.
Where that collides on Windows, the writer tries the rename again for up to about a second,
and a reader that cannot open the note while it is being replaced tries again for a few
milliseconds; a note that still cannot be read counts as none (the file is downloaded again),
and a note that cannot be published leaves the old one, which names a whole version too.
Neither is an error.

**Collecting old versions without a lock.** A reader learns a version's name from the note or by
writing the version itself, and opens it at once. So a version the note no longer names can be
in use only by a reader that read the note while it named that version, or that wrote it. The
note's age bounds the first: while the note is older than the grace period (five minutes), every
version it does not name was superseded at least that long ago. The version's own age bounds the
second. A fetch therefore deletes, in its URL's folder, every file other than the note, the
version the note names and the version it is returning that is older than the grace period,
but only while the note is older than that too; it reads the note before its age, so a note
replaced in between looks new and nothing is deleted. A file another process has open cannot be
deleted on Windows, and a later fetch tries again. Versions a process wrote but never published,
and notes left by a process that stopped mid-write, go the same way. No lock file is needed:
nothing is ever read, changed and written back, and each step is one atomic rename or the
creation of a new name.

The cache of earlier versions of the library (a copy `HASH_name` and a `.meta` beside it in the
cache folder itself) is not read: the first fetch of a URL downloads it again into its own
folder, and the old files stay until the temporary folder is cleaned.

**Within a process,** the catalog keeps its URL file lock only for single-flight reading: a
fetch returns a version and its stamp, and threads that miss the catalog's file cache for that
stamp together read the version once. It no longer re-reads the note under the lock to find the
stamp of a copy that might have changed, because a version cannot change.

**The test.** `query_url_cache_processes` starts an in-process server whose file changes every
15 ms (two URLs: one with an `ETag`, one with no validator, so every fetch of it downloads),
and in each of ten rounds starts six processes of its own program that point their cache at
one shared folder and query the file 24 times each: through a catalog they keep, a new catalog
each time, and a `CsvSource`. Every version of the file has a different number of rows, each
naming the version, so an answer shows whether it read one whole version, and each query first
asks the server which version it has, so an answer older than that is caught. After each
round the server stops changing and the parent, sharing the folder, must read the current
version. Before the fix it failed in every run on Windows, in every mode (133 to 184 of the
1,440 queries failed with "cannot write its copy"), and in most runs on Linux, where nothing
fails but answers are wrong: two to five per run read a version older than the server had when
the query began (a 304 for a note paired with an older copy), and some read a copy cut short.
After it, none fail on either, at `-opt s3` and `s0`, with and without the JIT, and with
`OBJECK_JIT_THRESHOLD=1`.

## Archives and workbooks in SQL

**The gap.** Phase 7's `ArchiveSource` and `XlsxSource` could only be loaded into a catalog
by code; a query could not name `results.zip` or `budget.xlsx` the way it names `runs.csv`.

**The syntax: a member after a dot, like a table after its schema.** A quoted path is already
a table in `FROM` and `JOIN`; a container's member follows it as a second quoted string:

```
select * from 'results.zip'                                  -- the archive's listing
select * from 'results.zip'.'2026/runs.csv'                  -- a member, by its extension
select * from csv('results.zip'.'notes.txt', delimiter => ';')    -- a member read a given way
select * from xml('results.zip'.'feed.xml', row => 'item')
select _member, count(*) from 'results.zip'.'2026/*.csv' group by _member
select * from 'runs/*.tar.gz'.'metrics.csv'                  -- one member of each archive; _file
select * from 'budget.xlsx'                                  -- the first worksheet
select * from 'budget.xlsx'.'Q1 2026'                        -- a worksheet by name
select _sheet, sum(amount) from 'budget.xlsx'.'Q*' group by _sheet
select * from xlsx('budget.xlsx'.'Report', header => false, first_row => 3)
```

Alternatives weighed: a separator inside the path (`'results.zip#runs.csv'`, `'results.zip!/
runs.csv'`) would make a legal file name mean something else and need escaping; a function
argument (`zip('results.zip', member => 'runs.csv')`) reads well but repeats the format the
extension already gives. The two-string form reuses the dotted reference SQL already has
(`schema.table`), keeps each name a plain literal, and composes with the existing function
forms, whose options then apply to the member. The lexer gained `.` as a symbol where it is
neither inside a name (`t.col` stays one word) nor the start of a number; it was an error
everywhere else before, so no statement that parsed changes meaning.

**What each form reads.**

- A path is an archive by its name: `.zip`, `.tar`, `.tar.gz`, `.tgz` (by name, as every file
  in SQL is read; `ArchiveSource` itself then tells zip from tar by content). `.xlsx` is a
  workbook, as is anything read with `xlsx(...)`.
- An archive alone is `ArchiveSource`'s listing table. A member is read by its extension,
  exactly as `ArchiveSource` reads members (CSV, TSV, JSON Lines, JSON, `.otab`, gzipped
  forms; XML needs `xml(..., row => ...)`); a function form chooses the format and its options
  (`delimiter`, `header`, `row`), so a `.txt` member reads with `csv(...)`. A JSON or XML
  member's child tables are not reachable from SQL; load the source for those.
- A workbook alone is its first worksheet; a sheet is named after the dot. `xlsx(...)` takes
  `header` and `first_row`, as `XlsxSource->SetHeader` and `SetFirstRow`.
- A member or sheet with `*` or `?` reads every match as one table with a `_member` or
  `_sheet` column, in archive or workbook order; a glob over the archives' own names adds
  `_file`, as for any file. Both together give both columns.
- The default table name is the member's base name (`runs` for `'2026/runs.csv'`) or the
  sheet's name; `AS` gives another.

**Safety and access, unchanged.** Everything goes through `ArchiveSource` and `XlsxSource`, so
their checks apply: nothing is extracted, a member climbing out with `..`, an absolute path,
duplicate or overlapping zip members, a zip bomb, a CRC32 mismatch and the decompression
limits are all refused. `Catalog->SetFileAccess(false)` refuses archives and workbooks like
any file (so `TableServer` never reads one), and the new `Catalog->SetArchiveLimits(member,
total)` sets the limits for archives and workbooks named in its queries (256 MB and 1 GB by
default, as the sources' own); a refusal names `SetArchiveLimits` rather than the source's
`SetLimits`. A URL archive is fetched through the download cache like any URL file.

**Caching.** As any file named in SQL: per catalog, keyed by path, member, format and
options, and re-read when the archive's size or modification time changes.

**Errors** name the archive and member or sheet: a missing member or sheet, a member in no
table format, an XML member without its row element, a pattern that matches nothing, a
function form on an archive with no member, a member named on a plain file, and an unquoted
or empty member.

**Tests.** `query_archives_in_sql`: a zip (deflated and stored members, a `.txt` read by
`csv()`, XML by `xml()`), a `.tar.gz` and a `.tgz` (and a glob over both), a workbook (first
sheet, a named sheet, a sheet pattern, `header => false`, `first_row`), joins across all
three, the catalog cache, and every refusal above, each archive and workbook written by the
test. Before the change every query fails at the lexer (`unexpected character '.'`).

## A server that answers at once

**The gap.** `TableServer` answered one request at a time: every request took its mutex, from
parsing the statement to writing the answer, because a catalog could not be queried by two
threads at once. Once it could ("One catalog, many threads"), the mutex served only to keep a
request from reading a table while it was written, and the server could not even do that: the
program that publishes the tables writes them from its own threads, which never took the server's
mutex, so a request could read a table half changed while every other request waited in line.

**What can write.** Nothing over the wire: the server answers only `SELECT` (a statement that
would change data, `INSERT`, `UPDATE`, `DELETE`, `CREATE` and `DROP INDEX`, `CREATE TABLE`, is
refused with 403, before it runs) and has file access off. Every request only reads. The writer
is the program, through the `Table` objects it published.

**The design: a readers-writer lock, and an API for the program's writes.**

- Each request takes a read share for as long as it reads: from parsing to the last byte of
  its answer (a table sent as CSV, JSON or `.otab` is read while it is written out). Any number
  of requests hold read shares at once, each on the thread the web server gave its connection.
- `TableServer->BeginWrite()` waits until no request is being answered and holds new ones off
  until `EndWrite()`. A program changes a published table only between the two: `INSERT`,
  `UPDATE`, `DELETE`, an index, a column, `Compact`, or anything else that writes a `Table`.
  The server's guarantee is exactly that: it keeps its own requests from overlapping the program's
  writes made inside `BeginWrite`/`EndWrite`, and nothing else; a write made outside them, or a
  table also read by other threads of the program, is the program's to order, as the table
  contract says ("Concurrent readers").
- A waiting writer holds new readers off, so a steady stream of requests cannot keep a change
  waiting for ever; the requests already in finish first. The lock is not reentrant: a thread
  calls `EndWrite` before it calls `BeginWrite` again, and calls neither while answering a request.
- `Publish` and `Unpublish` need no lock: they are a catalog's `Add` and `Remove`, which are safe
  while threads query, and a request already being answered keeps the tables it found. The table
  list skips a table unpublished while it was being written.
- The lock is a `ThreadMutex` and a `Condition` in the server (a waiting thread is parked for
  the collector). The count of answered requests has its own small lock.

A callback form (`Write(change)`) was weighed and left out: the pair is the smallest API that
lets a program hold the write across several statements, and Objeck has no exceptions that
could skip the `EndWrite`.

**The test.** `query_server_concurrent`: eight client threads meet at a barrier before each of
nine requests (grouped and joined SELECTs, an ordered limit, `DISTINCT`, tables as JSON and
`.otab`, and bad SQL), in CSV, JSON and `.otab`, over six rounds, and every answer must equal the
one a catalog of the same tables gives on one thread. Then the program changes a published table
sixty times between `BeginWrite` and `EndWrite`, cell by cell (every `a` is its row's id plus the
changes so far and every `b` minus that, so a read during a change sees a sum or a range that is
off), while four clients read it and another sends statements that would write, each of which
must be refused with 403. On master the test does not compile (there is no `BeginWrite`); with
the program's writes left unguarded, which is all master offered, 18 to 136 reads per run saw a
change half done, in every mode. It passes at `-opt s3` and `s0`, with and without the JIT, and
with `OBJECK_JIT_THRESHOLD=1`.

**Throughput.** One SELECT (a `GROUP BY` over 20,000 rows) sent as fast as each client thread
can, for six seconds, on this machine (Windows, 24 logical processors):

| clients | before (mutex) | after |
|---|---|---|
| 1 | 37.8 req/s | 34.2 req/s |
| 4 | 49.1 req/s | 86.7 req/s |
| 8 | 48.2 req/s | 112.4 req/s |

## Indexes in .otab files

**The gap.** Phase 6 left indexes out of the file, so a table lost them on every `Save` and
`Load`, every `ToBytes` and `FromBytes`, and every `Open` of a durable table; a program had to
remember which indexes it made and create them again.

**What is stored: the definition, not the entries.** Each index's name, column and kind go in
the footer. The entries are rebuilt from the column by the first query that uses the index,
as a stale index is today. Storing the sorted order too was considered and left out:

- it is not cheap to keep right. A partial read renumbers nothing but drops columns, `Append`
  adds rows the stored order does not cover, and deleted rows are never saved, so slots shift;
  each case would need the order rewritten or thrown away.
- it is not safe to trust. A stored order that disagrees with its column (a bug, or bytes
  damaged in a way the CRC32 misses) gives wrong answers silently, and checking it costs a pass
  over the column, about what rebuilding a hash index costs. Rebuilding is never wrong.
- it is not slow to skip: phase 6 measured 0.2-0.6 s to build an index on a million rows, paid
  once per index and only if a query uses it. Phase 8 lists stored entries as an option if a
  workload shows the rebuild matters.

**The format: version 2, written only when needed.** OTAB_FORMAT.md's rules say a version 1
footer must be consumed exactly and that new content comes with a new version, which a version
1 reader refuses. So the definitions are a new last part of the footer in version 2, and the
writer picks the lowest version that holds the table: **version 1 for a table without indexes,
byte for byte what it wrote before**, and version 2 only for a table with indexes. Libraries
before this change read every file they read before, and refuse an indexed table's file by its
header ("an .otab file of format 2; this library reads format 1") rather than misread it. This
library reads both. A version 2 index list is validated like the rest of the footer (a known
kind, a data column, no repeated name or column, a count below the column count) and refused
with the index it found wrong.

**Each path:**

- `Save`, `ToBytes`, `Load`, `FromBytes`: definitions written and restored. Restored indexes
  are attached unbuilt, so loading stays a copy of arrays; the first query that uses one
  builds it, as after any change, and "Concurrent readers" above makes that first build safe
  when several threads query a just-loaded table.
- `OtabFile->Read(names)`, including range reads over HTTP: the definitions are in the footer,
  which opening reads anyway; a read restores the indexes of the columns it reads, and
  `GetIndexNames` and `GetFormatVersion` report what the file holds.
- `Append`: keeps the file's version and definitions (the copied prefix holds its header);
  the appending table's own indexes are not added.
- Durable tables: `Open` restores the file's indexes, then replays the log through them, as
  any write reaches an index. `Checkpoint` saves the definitions. The log records rows, not
  indexes, and its format is left alone: `CreateIndex` and `DropIndex` on a durable table, and
  SQL `CREATE INDEX`/`DROP INDEX`, are saved by an immediate checkpoint (inside a statement,
  at its end, since a checkpoint replaces the log the statement is writing), with the
  compression the file already had. An index change is rare and a checkpoint is one save.
- `TableServer` sends tables as version 1 (`ToBytes(compression, false)`, new), so a client
  library of any age reads them; a remote reader has no use for the server's indexes.
- The generic `Serializer` already carried indexes and is unchanged.

**Tests.** `query_persist_indexes`: every index kind on every indexable type through Save/Load,
bytes, a compressed image and a partial read, each query through a restored index equal to a
scan and naming the index in its plan; a table without indexes written byte for byte as an
image made by master's library (kept in the test as hex) and that image read back; `Append` to
version 1 and version 2 files; a durable table's indexes through Open, replay, `CreateIndex`,
`DropIndex` and SQL; and footers whose index lists are wrong in each way above, each with a
correct checksum so only the defect is refused. `query_net_range` reads the definitions over
HTTP range requests; `query_net_serve` checks that served tables are version 1.
`tools/otab/otab_dump.py` reads version 2.

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
