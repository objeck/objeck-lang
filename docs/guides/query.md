# Query.Structured: a database inside your program

`Query.Structured` is a columnar, typed, in-memory table engine in the standard library
(since v2026.10.2). You build a `Table`, put rows in it, and ask questions in SQL: `WHERE`,
`GROUP BY`, `ORDER BY`, joins, arithmetic, indexes. There is no server and no setup. A table
can be saved to a file, kept durable on disk, or read from a CSV, JSON or XML file, an
archive, an Excel workbook or an ODBC database.

Use it when you would otherwise write loops over arrays of records to filter, group and sort
them. The design and the full list of what it does are in
[`docs/QUERY_DESIGN.md`](../QUERY_DESIGN.md); this page is the short, runnable way in.

Every program below is in
[`programs/examples/guides/`](../../programs/examples/guides/). Each one was compiled and run
with the v2026.10.3 toolchain, and the output shown is what it printed.

## Compiling

`obc` does not pull in a library's own dependencies, so a program that uses `Query.Structured`
names all of these:

```text
obc -src program.obs -lib query,misc,net,json,regex,xml,csv,cipher
```

## Your first table

A table has a name and a list of column names. `Insert()` makes an empty row and `Set()` fills a
cell by column name. Numbers go in as `IntRef` or `FloatRef` objects, text as plain strings.
`Query()` takes SQL and returns another `Table`; `ToCsv()` prints it.

<!-- code: programs/examples/guides/query_start.obs -->
```ruby
use Query.Structured;
use System.IO.Filesystem;

class QueryStart {
  function : Main(args : String[]) ~ Nil {
    # a table has a name and a list of column names
    people := Table->New("people", ["name", "age", "team"]);

    # Insert makes an empty row; Set fills a cell by column name.
    # Numbers go in as IntRef or FloatRef objects, text as plain strings.
    Add(people, "Ada", 36, "compilers");
    Add(people, "Grace", 45, "compilers");
    Add(people, "Linus", 28, "kernels");
    Add(people, "Barbara", 52, "kernels");
    Add(people, "Ken", 61, "tools");
    "rows: {$(people->Size())}"->PrintLine();

    # --- ask a question; the answer is a Table ---
    result := people->Query("select name, age from people where age > 40 order by age desc");
    result->ToCsv()->Print();

    # --- read the cells of an answer yourself ---
    # LiveRows() lists the row numbers, RowAt() gets a row, Get(name) a cell;
    # a cell comes back as an IntRef, FloatRef or String
    rows := result->LiveRows();
    each(i : rows) {
      row := result->RowAt(rows[i]);
      name := row->Get("name")->As(String);
      age := row->Get("age")->As(IntRef)->Get();
      "  {$name} is {$age}"->PrintLine();
    };

    # --- group, count and average ---
    result := people->Query("select team, count(*) as people, avg(age) as average_age from people group by team order by team");
    result->ToCsv()->Print();

    # --- a quick look at a table: its columns and their types ---
    people->Describe()->ToCsv()->Print();

    # --- a mistake is a Nil answer and a message ---
    result := people->Query("select nickname from people");
    if(result = Nil) {
      "error: {$(people->GetError())}"->PrintLine();
    };

    # --- a CSV file, queried by its name ---
    writer := FileWriter->New("scores_guide.csv");
    writer->WriteString("player,level,points\nada,3,1200\ngrace,5,3400\nada,4,2100\n");
    writer->Close();

    result := Catalog->Run("select player, sum(points) as total from 'scores_guide.csv' group by player order by total desc");
    result->ToCsv()->Print();
    File->Delete("scores_guide.csv");
  }

  function : Add(table : Table, name : String, age : Int, team : String) ~ Nil {
    row := table->Insert();
    row->Set("name", name);
    row->Set("age", IntRef->New(age));
    row->Set("team", team);
  }
}
```

```text
obc -src query_start.obs -lib query,misc,net,json,regex,xml,csv,cipher
obr query_start.obe
```

```text
rows: 5
name,age
Ken,61
Barbara,52
Grace,45
  Ken is 61
  Barbara is 52
  Grace is 45
team,people,average_age
compilers,2,40.5
kernels,2,40.0
tools,1,61.0
column,type,count,nulls,distinct,min,max,mean,std
name,string,5,0,5,,,,
age,int,5,0,5,28.0,61.0,44.4,11.60344776348823
team,string,5,0,3,,,,
error: unknown column 'nickname'
player,total
grace,3400
ada,3300
```

Things to notice:

- A column's type is worked out from the first value stored in it, so `age` is an `Int` column
  and `average_age` comes out as a `Float`.
- `avg(age)` over two `Int` cells is `40.5`: aggregates read numbers and return a `Float`.
- A cell read back from a result is an `IntRef`, a `FloatRef` or a `String`; `->As(IntRef)->Get()`
  turns it into an `Int`.
- `people->Query("select nickname from people")` did not stop the program. It returned `Nil`, and
  `GetError()` said `unknown column 'nickname'`. Every statement that fails does this, and the
  error is kept per thread, so threads that query one table do not read each other's errors.
- `Catalog->Run(sql)` answers a query that only reads files, with no tables to set up first. The
  quoted name `'scores_guide.csv'` stands where a table name goes; the format follows the
  extension.

## The SQL you can use

```text
SELECT [DISTINCT] (* | expr [AS name], ...)
FROM table [[INNER | LEFT] JOIN table ON condition ...]
[WHERE predicate]
[GROUP BY expr, ... [HAVING predicate]]
[ORDER BY expr [ASC | DESC], ...]
[LIMIT n [OFFSET m]]

INSERT INTO table [(cols)] VALUES (...), ...
UPDATE table SET col = value, ... [WHERE predicate]
DELETE FROM table [WHERE predicate]
CREATE TABLE name (col type, ...)
CREATE INDEX ON table (col) [USING HASH | SORTED]
```

- **Expressions** (`+ - * / %`, parentheses, unary minus) work in every clause: the select list,
  `WHERE`, `ON`, `GROUP BY`, `HAVING` and `ORDER BY`, inside and around aggregates
  (`having sum(qty * price) > 20`).
- **Tests** are `= <> < <= > >=`, `BETWEEN`, `IN (...)`, `LIKE`, `MATCHES`, `IS [NOT] NULL`,
  combined with `AND`, `OR` and `NOT`.
- **Aggregates** are `COUNT(*)`, `COUNT`, `SUM`, `AVG`, `MIN`, `MAX`, `MEDIAN` and `STDDEV`.
- **Arithmetic reads numbers only.** An empty cell, a cell that is not a number or a zero divisor
  gives an empty result, not an error. `7 / 2` is `3.5`.
- There are no scalar functions: `select upper(name) from people` is a syntax error.
- `INSERT`, `UPDATE`, `DELETE`, `CREATE TABLE` and `CREATE INDEX` run with `Execute()`, which
  returns the number of rows changed (or `-1` on error). A statement that fails changes nothing.
- `?` is a placeholder for a value; see prepared statements below.

## Joins, expressions and prepared statements

A `Catalog` holds named tables, and SQL run through it can join them. This program declares the
type of every column, so a value of the wrong type is refused instead of changing the column.

<!-- code: programs/examples/guides/query_join.obs -->
```ruby
use Query.Structured;

class QueryJoin {
  function : Main(args : String[]) ~ Nil {
    catalog := Catalog->New();

    # --- two tables with declared column types ---
    customers := Table->New("customers", ["id", "name", "city"],
      [ColumnType->INT, ColumnType->STRING, ColumnType->STRING]);
    AddCustomer(customers, 1, "Ada", "London");
    AddCustomer(customers, 2, "Grace", "New York");
    AddCustomer(customers, 3, "Linus", "Helsinki");
    catalog->Add(customers);

    orders := Table->New("orders", ["customer_id", "item", "qty", "price"],
      [ColumnType->INT, ColumnType->STRING, ColumnType->INT, ColumnType->FLOAT]);
    AddOrder(orders, 1, "widget", 10, 2.5);
    AddOrder(orders, 1, "gadget", 2, 10.0);
    AddOrder(orders, 2, "widget", 4, 2.5);
    AddOrder(orders, 2, "gizmo", 8, 0.25);
    AddOrder(orders, 9, "widget", 1, 2.5);
    catalog->Add(orders);

    # --- a declared column refuses a value of another type ---
    row := orders->Insert();
    stored := row->Set("qty", "lots");
    "storing text in an Int column: {$stored}"->PrintLine();
    orders->Delete(row->GetKey());
    ""->PrintLine();

    # --- inner join: only customers that have orders ---
    "-- inner join --"->PrintLine();
    result := catalog->Query("select c.name, o.item, o.qty * o.price as total from customers c join orders o on c.id = o.customer_id order by total desc");
    result->ToCsv()->Print();

    # --- left join: every customer, with empty cells where there is no order ---
    "-- left join --"->PrintLine();
    result := catalog->Query("select c.name, o.item from customers c left join orders o on c.id = o.customer_id order by c.name");
    result->ToCsv()->Print();

    # --- group the joined rows; HAVING may use arithmetic over aggregates ---
    "-- spending per customer, more than 20 in total --"->PrintLine();
    result := catalog->Query("select c.name, count(*) as orders, sum(o.qty * o.price) as spent from customers c join orders o on c.id = o.customer_id group by c.name having sum(o.qty * o.price) > 20 order by spent desc");
    result->ToCsv()->Print();

    # --- arithmetic in WHERE ---
    "-- orders where qty * price is under 5 --"->PrintLine();
    result := catalog->Query("select customer_id, item, qty * price as total from orders where qty * price < 5 order by customer_id");
    result->ToCsv()->Print();

    # --- a prepared statement: parsed once, run with different values ---
    "-- prepared statement --"->PrintLine();
    lookup := catalog->Prepare("select item, qty from orders where customer_id = ? and qty >= ?");
    lookup->Bind(1, 2)->Bind(2, 5);
    result := lookup->Execute();
    result->ToCsv()->Print();
    lookup->Bind(1, 1)->Bind(2, 1);
    result := lookup->Execute();
    "customer 1 has {$(result->Size())} orders"->PrintLine();

    # --- UPDATE and DELETE report how many rows they changed ---
    ""->PrintLine();
    changed := catalog->Execute("update orders set price = 3.0 where item = 'widget'");
    "updated {$changed} rows"->PrintLine();
    removed := catalog->Execute("delete from orders where customer_id = 9");
    "deleted {$removed} row"->PrintLine();
    result := catalog->Query("select count(*) as orders, sum(qty * price) as value from orders");
    result->ToCsv()->Print();

    # --- a mistake is a Nil result with a message, not a crash ---
    result := catalog->Query("select name from nobody");
    if(result = Nil) {
      "error: {$(catalog->GetError())}"->PrintLine();
    };
  }

  function : AddCustomer(table : Table, id : Int, name : String, city : String) ~ Nil {
    row := table->Insert();
    row->Set("id", IntRef->New(id));
    row->Set("name", name);
    row->Set("city", city);
  }

  function : AddOrder(table : Table, customer : Int, item : String, qty : Int, price : Float) ~ Nil {
    row := table->Insert();
    row->Set("customer_id", IntRef->New(customer));
    row->Set("item", item);
    row->Set("qty", IntRef->New(qty));
    row->Set("price", FloatRef->New(price));
  }
}
```

```text
obc -src query_join.obs -lib query,misc,net,json,regex,xml,csv,cipher
obr query_join.obe
```

```text
storing text in an Int column: false

-- inner join --
name,item,total
Ada,widget,25.0
Ada,gadget,20.0
Grace,widget,10.0
Grace,gizmo,2.0
-- left join --
name,item
Ada,widget
Ada,gadget
Grace,widget
Grace,gizmo
Linus,
-- spending per customer, more than 20 in total --
name,orders,spent
Ada,2,45.0
-- orders where qty * price is under 5 --
customer_id,item,total
2,gizmo,2.0
9,widget,2.5
-- prepared statement --
item,qty
gizmo,8
customer 1 has 2 orders

updated 3 rows
deleted 1 row
orders,value
4,64.0
error: no table named 'nobody'
```

Things to notice:

- `Table->New(name, names, types)` declares the types. `row->Set("qty", "lots")` returned `false`
  and stored nothing; with inferred types the column would have been promoted to hold both.
- `left join` kept Linus, who has no orders, with an empty cell where the order would be.
- `Prepare()` parses a statement once. `Bind(index, value)` fills the `?` placeholders, numbered
  from 1, and `Execute()` runs it as many times as you like with new values. A statement that
  changes rows runs with `ExecuteUpdate()`.
- A join is a hash join on equality. `Explain(sql)` on a table or a catalog describes the plan
  without running the query.

## Keeping a table on disk

An `.otab` file stores a table column by column. `Save` writes one (the write goes to a temporary
file that is renamed over the old one, so a crash leaves the old file or the new one), `Load`
reads it back, and `OtabFile` reads only the columns you name. `TableStore` goes further: it
logs every `INSERT`, `UPDATE` and `DELETE` before the statement returns, so a crash loses at most
the statement in progress.

<!-- code: programs/examples/guides/query_otab.obs -->
```ruby
use Query.Structured;
use System.IO.Filesystem;

class QueryOtab {
  function : Main(args : String[]) ~ Nil {
    path := "readings_guide.otab";

    # --- 100,000 readings from four sensors ---
    readings := Table->New("readings", ["sensor", "day", "celsius"],
      [ColumnType->STRING, ColumnType->INT, ColumnType->FLOAT]);
    sensors := ["north", "south", "east", "west"];
    each(i : 100000) {
      row := readings->Insert();
      row->Set("sensor", sensors[i % 4]);
      row->Set("day", IntRef->New(i / 4));
      row->Set("celsius", FloatRef->New(10.0 + ((i * 37) % 200)->As(Float) / 10.0));
    };
    "rows in memory: {$(readings->Size())}"->PrintLine();

    # --- save, load, and query what came back ---
    if(<>readings->Save(path, Compression->SMALL)) {
      "save failed: {$(readings->GetError())}"->PrintLine();
      return;
    };
    "file size: {$(File->Size(path))} bytes"->PrintLine();

    loaded := Table->Load(path);
    "rows loaded: {$(loaded->Size())}"->PrintLine();
    result := loaded->Query("select sensor, count(*) as n, min(celsius) as lowest, max(celsius) as highest from readings group by sensor order by sensor");
    result->ToCsv()->Print();

    # --- OtabFile reads only the columns you ask for ---
    # (the row keys are always read too, so a narrow read is not a tiny one)
    one := OtabFile->New(path);
    if(one->IsOpen()) {
      part := one->Read(["celsius"]);
      "one column:  {$(part->Size())} rows, {$(one->GetBytesRead())} of {$(one->GetFileSize())} bytes read"->PrintLine();
    };
    all := OtabFile->New(path);
    if(all->IsOpen()) {
      part := all->Read();
      "all columns: {$(part->Size())} rows, {$(all->GetBytesRead())} of {$(all->GetFileSize())} bytes read"->PrintLine();
    };

    # --- SQL names a table in a catalog; a loaded .otab goes in with Add ---
    catalog := Catalog->New();
    catalog->Add(loaded);
    result := catalog->Query("select count(*) as hot_readings from readings where celsius > 29.5");
    result->ToCsv()->Print();

    # --- a durable table: changes survive closing and reopening ---
    store := TableStore->New(path);
    if(<>store->IsOpen()) {
      "open failed: {$(store->GetError())}"->PrintLine();
      return;
    };
    durable := store->GetTable();
    durable->Execute("delete from readings where sensor = 'west'");
    durable->Execute("update readings set celsius = 0.0 where day = 0");
    "after changes: {$(durable->Size())} rows"->PrintLine();
    store->Close();

    reopened := TableStore->New(path);
    if(reopened->IsOpen()) {
      table := reopened->GetTable();
      "reopened: {$(table->Size())} rows, {$(reopened->GetReplayed())} log records replayed"->PrintLine();
      reopened->Checkpoint();
      reopened->Close();
    };

    File->Delete(path);
    File->Delete("{$path}.log");
    File->Delete("{$path}.lock");
  }
}
```

```text
obc -src query_otab.obs -lib query,misc,net,json,regex,xml,csv,cipher
obr query_otab.obe
```

```text
rows in memory: 100000
file size: 203504 bytes
rows loaded: 100000
sensor,n,lowest,highest
east,25000,10.2,29.8
north,25000,10.0,29.6
south,25000,10.1,29.7
west,25000,10.3,29.9
one column:  100000 rows, 157157 of 203504 bytes read
all columns: 100000 rows, 203504 of 203504 bytes read
hot_readings
2000
after changes: 75000 rows
reopened: 75000 rows, 2 log records replayed
```

Things to notice:

- `Save(path, Compression->SMALL)` wrote the 100,000 rows as a 203,504-byte file. `NONE` and
  `FAST` are the other settings.
- A narrow read touches fewer bytes than a full one, but not proportionally fewer here: the row
  keys are always read, and they are a large part of this file.
- `TableStore` takes a lock file (`path.lock`) so that one process writes at a time, and keeps a
  change log (`path.log`). After the two changes, reopening replayed 2 log records and the table
  had 75,000 rows. `Checkpoint()` folds the log into a new save.
- Readers that do not write open the file with `Table->Load` and see the last save or checkpoint.
- A named `.otab` file cannot stand in `FROM` the way a CSV can: load it, add it to a catalog and
  query it by table name, as the program does.

## Other sources

A quoted file name in `FROM` reads that file: `from 'runs.csv'`, `from 'weather.json'`. The
format follows the extension (`.csv`, `.tsv`, `.json`, `.jsonl`, `.xml`, `.xlsx`, also compressed
`.csv.gz`), and `csv('data.csv', header => false)` and its relatives set options. A `Catalog` can
also load sources that make tables of other things: `CsvSource`, `JsonSource`, `XmlSource`,
`TextSource` (log lines), `FilesystemSource` (a folder listing), `ArchiveSource` (zip and tar),
`XlsxSource` (worksheets), `ObjectSource` (your own objects), and, in separate libraries,
ODBC results, `System.ML` results and image detections.

The shipped example
[`sql_files_19.obs`](../../programs/deploy/sql_files_19.obs) queries CSV and JSON files and a
folder, and joins them. [`sql_tables_18.obs`](../../programs/deploy/sql_tables_18.obs) is another
tour of tables, indexes and `Explain`.

A catalog that answers queries from other people (as `TableServer` does) should call
`SetFileAccess(false)`: otherwise a query could read any file the process can, or fetch any URL.

## Where next

- [`docs/QUERY_DESIGN.md`](../QUERY_DESIGN.md): the storage layout, the planner, indexes, the
  `.otab` format, sources, and the network server.
- The API reference for `Query.Structured` at <https://www.objeck.org/api/latest/>.
- [Collections](collections.md), if what you need is a `Hash` or a `Vector` and not SQL.
