# The `.otab` table file format, versions 1 and 2

`.otab` is the file format of `Query.Structured` tables: `Table->Save`, `Append`, `ToBytes`,
`Load`, `FromBytes` and `OtabFile`. It stores what the column storage holds, column by column,
so loading a table copies arrays rather than parsing text. A reader can open a file by reading
only its last few hundred bytes, and then read just the columns it needs.

Version 2 differs from version 1 in one place: its footer ends with the table's
[index definitions](#indexes-version-2). A writer uses version 2 only for a table that has
indexes, so a table without any is written exactly as version 1 wrote it, and stays readable by
every version 1 reader.

This document is the format's specification. The design and its reasons are in
[QUERY_DESIGN.md §9](QUERY_DESIGN.md#9-persistence-a-native-table-file). An independent reader written
from this page, [`tools/otab/otab_dump.py`](../tools/otab/otab_dump.py), decodes every byte of
a file and prints an annotated layout; use it to check a writer, or to look inside a file.

## At a glance

```
offset 0      header    "OTAB", format version                                     12 bytes
              blocks    row group 0: one block per column, key column first
                        row group 1: ...
footer_offset footer    table name, next key, schema, then per group:
                        row count and, per column, where its block is and what it holds;
                        version 2: then the index definitions
              trailer   footer offset, footer size, footer CRC32, "OTAB"            28 bytes
```

A reader starts at the end: the trailer gives the footer's place, the footer gives every
block's place. Nothing between the header and the footer needs to be read to open a file.

## Primitive values

Every number is 8 bytes, little-endian, whatever the platform:

| Name | Encoding |
|---|---|
| **Int** | 64-bit two's complement |
| **Float** | IEEE 754 binary64 |
| **Text** | an Int byte count, then that many bytes of UTF-8. A count of `-1` is Nil (an empty cell), with no bytes after it. `0` is the empty string. |
| **Bits** | a run of Ints read as one bitmap: bit `i` is bit `i & 63` of Int `i >> 6` |

There is no padding anywhere: each field follows the one before it.

## Header (12 bytes)

| Offset | Field | Value |
|---|---|---|
| 0 | magic | the bytes `4F 54 41 42` (`"OTAB"`) |
| 4 | version | Int, `1`, or `2` for a file whose footer holds index definitions |

A reader refuses a version below 1 or above the highest it knows. A version 1 reader therefore
refuses a version 2 file by its header, before reading anything else, rather than misreading
its footer.

## Trailer (the last 28 bytes)

| Offset from the end | Field | Value |
|---|---|---|
| −28 | footer offset | Int, where the footer starts |
| −20 | footer size | Int, its length in bytes |
| −12 | footer CRC32 | Int, the CRC-32 of the footer bytes (zlib's `crc32`, unsigned, so `0` to `2^32 − 1`) |
| −4 | magic | `"OTAB"` again |

A file is well formed only if `footer offset ≥ 12`, `footer size ≥ 0`, and
`footer offset + footer size + 28` is exactly the file's size. A file cut short anywhere fails
this test or the footer's CRC, so a truncated file is reported, never half-read.

## Footer

In order:

1. **name**: Text, the table's name.
2. **next key**: Int, the key the next inserted row will get.
3. **column count**: Int, `C ≥ 1`, counting the key column.
4. **columns**, `C` times:
   - **name**: Text;
   - **type**: Int, one of the [type codes](#type-codes);
   - **declared**: Int, `1` if the column's type was declared (so the table refuses values of
     other types), `0` if it was inferred from the data.
5. **row group count**: Int, `G ≥ 0`.
6. **row groups**, `G` times:
   - **rows**: Int, the number of rows in the group;
   - **block info**, `C` times, in column order (see below).
7. **indexes**, version 2 only (see [Indexes](#indexes-version-2)): Int, `I` with
   `0 ≤ I < C`, then `I` index definitions.

The footer must be consumed exactly: bytes left over make the file malformed.

**Column 0 is always the key column**, named `pk`, type INT, declared. Its values are the
rows' primary keys. The remaining columns are the table's own, in the table's order.

### Indexes (version 2)

Each index definition is:

| Field | Type | Meaning |
|---|---|---|
| name | Text | the index's name, not empty and not Nil |
| column | Int | the column's position in the footer's column list, `1 ≤ column < C` (never the key column) |
| kind | Int | `1` HASH, `2` SORTED |

No two definitions share a name or a column. A reader refuses a footer that breaks any of these
rules, or names a kind it does not know, as malformed, saying which index is wrong.

Only the definition is stored, not the index's entries: a reader that restores an index builds
it from the column, which it has read anyway, so a damaged or stale index can never be loaded,
and Append adds rows without rewriting anything. `Table->Load`, `FromBytes`, `OtabFile->Read`
and `TableStore` restore the indexes of the columns they read, unbuilt; the first query that
uses one builds it. A partial read restores only the indexes of the columns it reads.

`Append` keeps the file's version and its definitions: it copies the header with the rest of
the prefix, and writes the same definitions into the new footer. `TableServer` sends tables as
version 1 (`ToBytes(compression, false)`), so clients of any version read them.

### Block info (72 bytes)

| Field | Type | Meaning |
|---|---|---|
| offset | Int | where the block starts in the file |
| stored | Int | the block's length in the file |
| raw | Int | its length after decompression |
| codec | Int | how it was compressed: see [Codecs](#codecs) |
| crc | Int | CRC-32 of the **stored** bytes, so damage is caught before decompressing |
| nulls | Int | how many cells in the block are empty |
| has zone | Int | `1` if `low` and `high` hold the group's range, else `0` |
| low | Float | the smallest non-empty value in the group |
| high | Float | the largest |

**Zones** are kept for INT, FLOAT and DATE columns (DATE as its milliseconds), and only when
the group has at least one non-empty cell. A reader may use them to skip a group whose range
cannot match a filter, without reading the block. They are Floats, so for an INT column they
are exact only while every value's magnitude is at most `2^53`.

## Type codes

| Code | Type | Cell values |
|---|---|---|
| 1 | INT | Int |
| 2 | FLOAT | Float |
| 3 | STRING | text, dictionary-coded or plain |
| 4 | DATE | milliseconds, plus a GMT flag |
| 5 | BOOL | Int, `0` or `1` |
| 6 | OBJECT | any of the above, tagged per cell |

A reader refuses a file with a type code it does not know, naming the column.

## Row groups and blocks

Rows are stored in **row groups** of at most 65,536 rows, in table order. Each group holds one
**block** per column, the key column's first, laid out one after another in the file. Every
block decodes on its own: nothing in a block refers to another block.

`Save` writes full groups of 65,536 rows and a last, shorter one. `Append` adds groups after the
existing ones without touching them, so groups of any size from 1 to 65,536 can follow each
other. A reader must not assume any group is full.

A group's rows are its cells in order: the table's rows are the groups' rows concatenated, in
group order. Deleted rows are never stored; a saved table has only live rows.

### Block contents (after decompression)

Every block starts with the column's empty cells for the group:

1. **bitmap words**: Int, `W = ceil(rows / 64)`;
2. **empty cells**: Bits, `W` Ints. A set bit means the cell is empty (Nil). A writer leaves
   the bits past the group's last row 0, and a reader ignores them.

The values follow, by type. Each type stores a value in **every** row, empty or not. A writer
stores 0 for an empty cell (0.0, code 0, GMT bit clear), so a file never carries a value that was
cleared, and two equal tables save to the same bytes. A reader ignores it.

| Type | After the bitmap |
|---|---|
| INT, BOOL | `rows` Ints |
| FLOAT | `rows` Floats |
| DATE | `rows` Ints of milliseconds, then `W` Ints of Bits: a set bit marks a cell created as a GMT date |
| STRING | an Int **encoding**, then one of the two layouts below |
| OBJECT | `rows` tagged values: see below. Here an empty cell's tag is 0, not a placeholder value |

**STRING, encoding 1 (dictionary)**: an Int dictionary size `D`, then `D` Texts, then `rows`
Ints, each an index into this block's dictionary. The dictionary belongs to the block: a reader
joining several groups maps each block's codes onto one dictionary of its own. A writer stores
exactly the texts the block's non-empty cells use, in order of first use, so a text removed from
the table is never saved and a group repeats none of another's. A reader accepts any dictionary
that covers the block's codes, and ignores an empty cell's code.

A table keeps a STRING column dictionary-coded until it has 4,096 distinct texts **and** more
distinct texts than half its non-empty cells; from then on it stores the texts plainly. The
file records whichever the column used, block by block.

**STRING, encoding 0 (plain)**: `rows` Texts, Nil (`-1`) for an empty cell.

**OBJECT** cells start with an Int tag:

| Tag | Cell | Followed by |
|---|---|---|
| 0 | empty | nothing |
| 1 | `IntRef` | Int |
| 2 | `FloatRef` | Float |
| 3 | `String` | Text |
| 4 | `Date` | Int milliseconds, Int GMT flag (`1` or `0`) |
| 5 | `BoolRef` | Int, `0` or `1` |

A table whose OBJECT column holds any other kind of value cannot be saved; `Save` reports the
column instead of writing a file.

**DATE milliseconds** encode a date's calendar fields, read as UTC: days since
1970-01-01 in the proleptic Gregorian calendar, times 86,400,000, plus the time of day. No time
zone conversion is applied in either direction, so a date reads back with exactly the fields it
was saved with, and ordering the numbers orders the dates. The GMT flag is kept separately and
does not change the number.

A block must be consumed exactly, like the footer.

## Codecs

| Code | Codec | Written by |
|---|---|---|
| 0 | none: the stored bytes are the block | `Compression->NONE` (the default), or when compressing would not help |
| 1 | zlib stream (RFC 1950), compression level 1 | `Compression->FAST` |
| 2 | zlib stream (RFC 1950), compression level 9 | `Compression->SMALL` |

Codes 1 and 2 decompress the same way; the code records how the block was written. Each block
is compressed on its own, so a reader decompresses only the blocks it reads, and a byte range
of the file is still meaningful.

A writer stores a block uncompressed, with codec 0, when the block is under 64 bytes or when
compressing it would not make it smaller, so one file usually mixes codecs. After
decompressing, a reader checks the length against **raw**.

## Reading a file

1. Read the 12-byte header and check the magic and version.
2. Read the 28-byte trailer, check the magic and the size equation, and read the footer.
3. Check the footer's CRC, then parse it, refusing unknown type codes, a malformed index list
   (version 2) and leftover bytes.
4. For each column wanted, and for each group: read `stored` bytes at `offset`, check their
   CRC, decompress by `codec`, check the length against `raw`, and decode the block.

Every failure names what it found: the column and row group of a damaged block, a footer that
does not match its checksum, a file that ends early. A reader never returns partly read data.

`OtabFile` does exactly this, reading only the footer when it opens a file and only the
requested columns, plus the key column, on `Read(names)`.

## Writing a file

`Save` and `Append` never write in place. They write the whole new file to `path.part`, then
rename it over `path`, which replaces the old file in one step (POSIX `rename`;
`MoveFileExW` with `MOVEFILE_REPLACE_EXISTING` on Windows). A reader
therefore sees the old file or the new one, never a mix, and a crash during a save leaves
the old file as it was.

`Append(path)` copies the existing file's bytes up to its footer unchanged. It adds the new
rows' groups after them and writes a new footer listing the old groups and the new ones. The
new rows' keys continue from the old footer's **next key**. Their columns must match the file's
in name, order and type.

## The change log

A table opened for writing (`TableStore->New(path)` or `Table->Open(path)`) records every change
in `path.log` beside the file, and reopening replays it over the last save. It uses the same
primitive values as the file.

**Header (28 bytes):**

| Offset | Field | Value |
|---|---|---|
| 0 | magic | `"OLOG"` |
| 4 | version | Int, `1` |
| 12 | base CRC32 | Int, the footer CRC32 of the save this log follows |
| 20 | base size | Int, that file's size in bytes |

A log whose base does not match the file belongs to an earlier save: a checkpoint saved the
file but crashed before writing the new, empty log. The file already holds those changes, so
the log is skipped, never replayed twice.

**Records**, one per statement, in order:

| Field | Type | Meaning |
|---|---|---|
| length | Int | payload bytes |
| check | Int | the length with every bit inverted |
| CRC32 | Int | of the payload |
| payload | | an Int count of changes, then the changes |

Each change starts with an Int kind:

| Kind | Change | Followed by |
|---|---|---|
| 1 | insert | Int key: the row the table's next key creates |
| 2 | set a cell | Int key, Int column index, the value as an OBJECT cell (tag, then value; tag 0 empties it) |
| 3 | delete | Int key |
| 4 | add a column | Text name, Int type code |

A statement is everything one API call or one SQL statement changes; a SQL statement that
fails is recorded with its rollback, so replay ends in the same state, keys included. The record
is written and flushed before the call returns.

**Reading.** A crash can only cut the log short, so a last record that ends early (fewer than
24 bytes left, or a length past the end) is a torn write: it is dropped and cut from the file.
A length whose check disagrees, or a CRC32 that fails on a record that is not the last, is damage,
and opening is refused with the byte offset. Replay that does not land on a live key, a known
column or the expected next key is refused the same way.

**Checkpoint** writes the table as a new save (by `path.part` and rename), then replaces the log
with an empty one naming that save. `Save` of a durable table's own file is a checkpoint, and
`Append` onto it is refused. The log records rows, not indexes, so creating or dropping an index
on a durable table is saved by a checkpoint: at once, or, inside a SQL statement, when the
statement ends. Opening restores the file's indexes before replaying the log, and the replayed
changes reach them as any write does.

**The lock**, `path.lock`, is created only if absent (`File->CreateNew`) and holds the writer's
process id as text. A writer that finds it held by a running process is refused with that id. One
held by a process that is gone, or left empty for a second (a writer that crashed between
creating and writing it), is removed and taken. Readers (`Table->Load`) take no lock and see the
last save or checkpoint. Two processes clearing the same stale lock at the same moment can both
proceed; stale-lock recovery assumes that does not happen.

## A worked example

A table `t` with an INT column `n` and a STRING column `s`, and two rows,
`(n = 7, s = "hi")` and `(n = empty, s = "hi")`, saved uncompressed (495 bytes).
`python tools/otab/otab_dump.py t.otab` prints:

```
     0  4f 54 41 42                                          magic "OTAB"
     4  01 00 00 00 00 00 00 00                              format version = 1
        == row group 0: 2 rows
        -- column pk (INT): 32 bytes stored, codec 0 (none), 32 raw
    12  01 00 00 00 00 00 00 00                              bitmap words = 1
    20  00 00 00 00 00 00 00 00                              empty cells: 0
    28  00 00 00 00 00 00 00 00 01 00 00 00 00 00 00 00      values
                                                             => [0, 1]
        -- column n (INT): 32 bytes stored, codec 0 (none), 32 raw
    44  01 00 00 00 00 00 00 00                              bitmap words = 1
    52  02 00 00 00 00 00 00 00                              empty cells: 1
    60  07 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00      values
                                                             => [7, None]
        -- column s (STRING): 58 bytes stored, codec 0 (none), 58 raw
    76  01 00 00 00 00 00 00 00                              bitmap words = 1
    84  00 00 00 00 00 00 00 00                              empty cells: 0
    92  01 00 00 00 00 00 00 00                              encoding = 1 (dictionary)
   100  01 00 00 00 00 00 00 00                              dictionary size = 1
   108  02 00 00 00 00 00 00 00 68 69                        dictionary ['hi']
   118  00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00      codes
                                                             => ['hi', 'hi']
   134  01 00 00 00 00 00 00 00 74 02 00 00 00 00 00 00 ...  footer, 333 bytes
                                                             name 't', next key 2
                                                             column pk INT, declared
                                                             column n INT, declared
                                                             column s STRING, declared
                                                             group 0 pk: offset 12, 0 empty, zone 0.0..1.0
                                                             group 0 n: offset 44, 1 empty, zone 7.0..7.0
                                                             group 0 s: offset 76, 0 empty
   467  86 00 00 00 00 00 00 00                              footer offset = 134
   475  4d 01 00 00 00 00 00 00                              footer size = 333
   483  5b f5 90 ea 00 00 00 00                              footer CRC32 = 3935368539
   491  4f 54 41 42                                          magic "OTAB"
2 rows in 1 row groups, 3 columns, 495 bytes
```

Row 1's `n` is empty: bit 1 of `n`'s bitmap is set (`02`), and its value slot holds 0. Both
rows' `s` share the dictionary's one entry, code 0. The footer holds the name (`01 …` then
`t`), next key 2, the three columns (all declared, since the table was created with declared
types), one group of two rows, and three 72-byte block infos; `pk` and `n` have zones, `s` has
none.

## Compatibility

- A reader must refuse a **newer** version rather than guess at it. Versions only grow; a
  version 1 file stays readable by every later reader, and a version 2 reader reads both.
- New type codes, codecs, STRING encodings, OBJECT tags or index kinds come with a new version
  number, so a reader never meets one in a file it accepts. Inside a file of a version it
  knows, an unknown code means the file is malformed, and the reader says which column or
  index holds it.
- A writer uses the lowest version that can hold the table: version 1 unless the table has
  indexes. So adding version 2 changed nothing for tables without indexes; their files are
  byte for byte what a version 1 writer makes (`query_persist_indexes` checks this against an
  image written before version 2 existed).
- Version 2 added index definitions. Not yet in any version, though the design leaves room
  for them: delta and run-length encodings, and stored index entries.
