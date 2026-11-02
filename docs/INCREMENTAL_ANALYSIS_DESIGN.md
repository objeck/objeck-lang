# Incremental Analysis for the Language Server — Design

Issue: [#1236](https://github.com/objeck/objeck-lang/issues/1236), item 15 of the
2026-10-17 audit of `tools/lsp` against LSP 3.17. It follows #1217 (debounce) and #1218
(reuse across requests), which took the waste out of the server. What remains is the cost of
the compiler's whole-program analysis.

## Why

Every keystroke costs work proportional to the whole folder. Measured in the audit (Windows
x64):

- a 6-line `main.obs` alone: 12 ms per keystroke;
- the same, with one 2.5k-line file in `build.json`: 146 ms;
- `obc` on about 10k lines with clashing names: 16.9 s, which is superlinear.

Research code is often one large notebook-like file plus helpers. The editor should not slow
down as the project grows.

## What happens on an edit today

Measured by reading the code (file references as of 2026-10-20):

1. **Everything is invalidated.** `didChange` bumps one workspace-wide generation
   (`LspWorkspace::Invalidate`, frameworks.obs:1337). The next request drops every cached
   program in every folder (`GetProxy`, 1282).
2. **The whole folder is re-parsed.** `LspFolder->GetTexts` sends every `build.json` file
   to `diag_parse_text`, which builds one `Parser` and one `ParsedProgram` over all of them
   (diags.cpp:306, parser.cpp:257). Bundles that span files are merged by name and share one
   symbol table.
3. **Every library is reloaded from disk.** Each `ContextAnalyzer` makes a new `Linker`,
   which reads and inflates `lang.obl` and every listed `.obl` (linker.cpp:498, 854-895).
   Nothing is cached across programs.
4. **The whole program is analyzed, once.** `ContextAnalyzer::Analyze` (context.cpp:381-551)
   works through the entire program:
   - it resolves aliases in place;
   - it runs a pre-pass that adds methods to classes (default alternates, named-argument
     forwarders driven by call sites in *any* file);
   - it links parents, which writes into the Linker's library classes (`AddChild`);
   - it analyzes every method.

   Analysis mutates the tree (eval types, resolved methods, inserted casts, generated
   methods), so it cannot run twice on one tree: diags.cpp:68-74 and 210-216 record that a
   second `Analyze()` reports bogus errors. A new analysis needs a fresh parse.

So the cost of an edit is parse(all) + link(all libraries) + analyze(all). The phases below
take those costs out in order of cost and risk.

## Phases

### Phase 0 — measure

Before changing anything, time the three parts per request:
- an `OBJECK_LSP_TIMING=1` switch in diags.cpp records parse, link (`Linker::Load`) and
  analyze times for each program;
- a `run_lsp_tests.py` case drives a fixed edit sequence on `lib_src/json.obs` (the 2.5k-line
  fixture `check_large_real_file` already uses) and prints the split.

The audit's 146 ms is the baseline. This decides whether phase 1 or phase 2 comes first: if
reloading the libraries is a large share, phase 1 is nearly free.

#### Phase 0 results (2026-10-27)

How to repeat it: `OBJECK_LSP_TIMING=1` makes `libobjk_diags` write one `[lsp-timing]` line
to stderr per parse and per analysis. `tools/lsp/tests/bench_incremental.py <bin_dir>
[server_dir]` turns it on, runs the server, and prints the table below. It is a separate script
rather than a `run_lsp_tests.py` case because it reports numbers and does not pass or fail.
Per workspace it opens one document, makes 10 body-only edits (a comment line), sends a
hover after each edit, and waits for the diagnostics. It then times `obc` on the same
program. The `link` column is a second `Linker` loading the same libraries, alone, right after
the analysis: `Linker::Load` runs inside `Analyze()`, and timing it there would mean changing
the compiler. `analyze` is the analysis total minus that load.

Windows x64, Ryzen AI 9 HX 370, master at 36923202bd, JIT pinned off as the editor clients
set it. Medians, in ms per edit, summed over the programs analyzed for that edit:

| workspace | lines | parse | link | analyze | native total | hover after edit | programs per edit | obc |
|---|---:|---:|---:|---:|---:|---:|---:|---:|
| tiny: a 6-line file, standalone (13 default libraries) | 8 | 0.1 | 76 | 0 | 76 | 274 | 2 | 172 |
| two_files: `tests/ws_multi_a`, gen_collect | 23 | 0.3 | 27 | 0 | 27 | 107 | 2 | 111 |
| json_standalone: `lib_src/json.obs` alone (13 default libraries) | 2.8k | 9 | 52 | 65 | 126 | 299 | 1 | 212 |
| lsp_server: `tools/lsp/server`'s own sources, 5 files, 6 libraries | 8.6k | 121 | 38 | 775 | 933 | 1209 | 2 | 1188 |

Runs vary by about ±30% (a second run gave 90 and 1340 ms native for tiny and lsp_server), so
read the shares, not the last digit. Opening the same document (one program, no hover) took
150 to 600 ms to diagnostics.

What the numbers say:

1. **Every edit analyzes the program twice.** Diagnostics read the current text (`text|`
   proxy) and hover, completion and signature help read the last clean text (`alt|`). An edit
   bumps the generation, `GetProxy` releases both, and after a clean edit the two texts are
   the same. So the second parse and analysis repeat the first. This was not in the plan, and it
   is the cheapest win: half of every row's native time, with an LSP-side change and no compiler
   change. (`json_standalone` shows 1 because its errors keep the `alt` text from moving.)
2. **Small programs are all library loading.** For tiny and two_files the analysis is the
   `Linker` reading and building `lang.obl` plus the listed libraries: 38 ms for the 13 default
   libraries and 14 ms for gen_collect, per program, every edit. That matches how Objeck code is
   written today, with most of it in precompiled libraries. Phase 1 removes it.
3. **Large programs are all analysis.** For the 8.6k-line workspace, `Analyze()` without its
   library load is 83% (380 to 460 ms per program), parsing 13% (about 60 ms per program) and
   the libraries 4%. Phase 3 (re-parse one file) can save at most the parse share, less the cost
   of cloning, which confirms that it should wait. Only phase 4 reaches the large share.
4. **`obc` is the same picture.** It takes 1.2 s on the 8.6k-line program and 170 ms on a
   6-line file, where the second figure is mostly loading 13 libraries. A library cache and
   repeatable analysis help the command-line compiler too, as the Decisions section expects.
5. **Not attributed:** hover after an edit takes about 200 ms more than the native work on the
   small workspaces. That is not parse, link or analysis, and running the server with the JIT
   on does not change it. The hover's own native queries are not timed. A profile of that path
   should come before anything else is changed for small files.

Recommended order:
- **First: analyze once per edit.** Keep the `alt` proxy, or share the `text` one, when the
  texts are equal. This is a small LSP-side change.
- **Then phase 1, step 1 (the byte cache).** It is worth about 15 to 40 ms per program on every
  edit, and it is all of the time on small files. The cache saves the read and the inflate. Step
  2 is needed to save building the library objects too. These numbers do not split those two
  parts, so measure that split before step 2.
- **Phase 4 next** for large workspaces, with phase 3 after it.
- **Phase 2** pays only in multi-root workspaces; nothing here measured that case.

### Phase 1 — load each library once per server

`Linker` objects are per program because user classes write into them
(`LibraryClass::AddChild`), and the AST holds raw `LibraryMethod*` pointers into them. Two
steps make a shared copy safe:
1. **Cache the bytes.** A process-wide cache of inflated `.obl` contents, keyed by path,
   size and mtime. `Library::Load` reads from it instead of from disk. This needs no other
   change, because each program still builds its own objects from the bytes.
2. **Share the objects.** Move the per-program part of a library class, its children, out of
   `LibraryClass` and into a side table on the program, keyed by the library class. Then one
   `Linker` per library set can be shared read-only, and the AST's pointers stay valid as long
   as the cache entry lives. That entry is reference-counted by the programs that use it.

Expected gain: the library share of the measured time, which is the whole cost for a small
file in a large `libs` list.

#### Phase 1 results (2026-11-02)

Before step 2, the load was split further (Windows x64, Ryzen 9 7950X3D, a 6-line file with
the 13 default libraries, ms per program):

| part of `Linker::Load` | ms |
|---|---:|
| read the 13 files | 1.1 |
| inflate them | 3.4 |
| build every library method's instructions | 24 |
| everything else: classes, methods, signatures, literal tables | 8 |
| **total** | **36** |
| and later, freeing that `Linker` with the program | 20 |

The instructions were not in the plan, and they are two thirds of the load and most of the
free. They feed code generation only (`intermediate.cpp`), which the diagnostics library
does not compile. So phase 1 landed as step 1 plus a step the plan did not have:

1. **The byte cache**, as planned, except for the key: a library is read from disk on every
   load and compared with the cached compressed bytes, so a rebuilt library is never served
   stale whatever its size or timestamp. Only the inflate is saved.
2. **Instructions are read but not built** in the diagnostics library, and the class and
   method names they carry are skipped rather than decoded.

Both are compiled into the diagnostics library only (`_DIAG_LIB`); `obc` builds every
library and program byte for byte as before. `OBJECK_LSP_NO_CACHE=1` turns both off, so it is
still the from-scratch reference. After them the load is 1 ms read, 1 ms skipping the
instructions and 9 ms for the rest, and the free about 4 ms.

Measured with `bench_incremental.py`, six rounds alternating the diagnostics library built
from master and from this phase (same tree, same server), medians in ms per edit. CPU load on
the machine was 0 to 51% during the runs. `release` is the new timing line for freeing the
programs an edit made stale; `main_with_json` is the issue's second workload, a 6-line
`main.obs` with `json.obs` beside it in `build.json`:

| workspace | parse+link+analyze before | after | release before | after | hover after edit before | after |
|---|---:|---:|---:|---:|---:|---:|
| tiny (6 lines, 13 default libraries) | 44.2 | 12.9 | 29.6 | 6.6 | 165 | 53 |
| two_files (`ws_multi_a`, gen_collect) | 8.8 | 4.0 | 5.8 | 2.2 | 46 | 29 |
| main_with_json (2.8k lines, gen_collect) | 92.2 | 83.4 | 7.8 | 4.4 | 168 | 151 |
| json_standalone (2.8k lines, 13 default libraries) | 133.6 | 83.2 | 31.9 | 8.2 | 318 | 188 |
| lsp_server (8.8k lines, 6 libraries) | 538.7 | 513.4 | 22.1 | 15.2 | 738 | 713 |

`obc` is unchanged: the same 6-line program with the 13 libraries takes 136 ms before and 135
after, and the server's 8.8k lines 898 and 899 (medians of ten, alternated).

`tools/lsp/tests/validate_cached_analysis.py` checks the result against a from-scratch
analysis: a second server with `OBJECK_LSP_NO_CACHE=1` answers the same requests over the
language server's fixtures, the regression programs and the library sources, with a seeded,
randomized edit script per file (comment lines, deleted and duplicated lines, stray
characters, renamed identifiers, undo, revert). Diagnostics must be byte-identical and every
hover, completion, symbol and definition answer equal. `--control` sends the cached server a
different edit script and must fail, which shows the comparison can tell answers apart.

**Step 2 is deferred.** What it could still save is the "everything else" row and most of the
free: about 9 + 4 ms per program for the 13 default libraries, against the 36 + 20 this phase
removed most of. It would also have to move more than the plan said: library signatures are
`frontend::Type` objects made by the program's own `TypeFactory` (per program since #659), and
the analyzer writes into library objects in several places besides `AddChild` (`SetCalled`,
lazily cached user names, interface ids). Phase 4 is the larger prize for any program bigger
than a few lines.

### Phase 2 — invalidate per folder, and skip unchanged folders

LSP-side and small. The generation becomes per folder:
- an edit invalidates only the folder that contains the document;
- standalone files invalidate only themselves;
- `workspace/symbol` invalidates on any change, as today.

In a multi-root workspace, an edit in one root no longer costs a re-analysis of the others
(`check_multi_root` covers the shape).

### Phase 3 — re-parse only the edited file

Keep the token stream and the parse result per file, and re-parse only the files whose text
changed. This needs a per-file unit the program does not have today:
- **A `ParsedFile`** holding the file's bundles, classes, `use`s and its own node pool.
  `ParsedProgram` merges files by bundle name, as the parser now does inline.
- **Analysis still runs on the whole program**, and it mutates the tree. So a cached
  `ParsedFile` must be *cloned* before it is merged. Cloning the AST is cheaper than lexing
  and parsing it, but not free; phase 0's numbers decide whether this phase is worth its cost
  before phase 4.
- **Per-file state the parser keeps globally** must become per file or be recomputed at
  merge:
  - `named_call_shapes`, which call sites fill and which generate forwarders on other files'
    classes;
  - lambda numbering;
  - the program-wide literal tables.

### Phase 4 — analysis that can be repeated, scoped to what an edit reaches

The deep change, and the one the issue's "re-run context analysis only for the classes the
edit can reach" needs:
- **Analysis writes its results to side tables** keyed by node, not into the nodes:
  - eval types;
  - resolved methods;
  - inserted casts.

  The pre-pass's generated methods become a derived layer that is rebuilt per analysis. A
  tree then survives analysis unchanged and can be analyzed again.
- **A dependency index.** Each class records the classes and library classes it named
  (parents, interfaces, field and parameter types, call targets). An edit to a file
  re-analyzes:
  - that file's classes;
  - classes that depend on a class whose *signature* changed;
  - nothing else for a body-only edit.

  A method body edit that changes no signature re-analyzes one class.
- **Generated methods follow their inputs.** Named-argument forwarders are keyed by call
  shape and regenerated when a call site's shape set changes; default alternates are
  regenerated when a signature changes.

This touches every `Set*` the analyzer performs, so it is a series of PRs, each moving one
kind of result to a side table behind the existing accessors, with the suite and
`OBJECK_LSP_NO_CACHE=1` comparisons as the check.

## Decisions

- **Correctness first** (the LSP rule: correct-slow over fast-brittle). Every phase keeps
  `OBJECK_LSP_NO_CACHE=1` as a reference. A test compares cached against fresh diagnostics,
  hover and completion over the edit sequence, and they must agree exactly.
- **The command-line compiler gains from phases 1 and 4 too** (library loading, and
  analysis that does not need a fresh parse). It is not the target, so no phase may slow
  `obc`.
- **Not in scope:**
  - incremental *code generation*;
  - parallel analysis across classes (the analyzer's lambda and capture state is per
    analyzer, and that change is separate);
  - the superlinear `obc` case with clashing names. Its cause is not known yet:
    `ParsedProgram::GetClass` walks the bundles with a hash lookup in each, which is
    linear in bundles, not classes, so profiling has to find it before an issue can say
    what to fix.

## First steps

1. ~~Phase 0's timing switch and test, and the measured split recorded in this document.~~
   Done; see "Phase 0 results".
2. ~~One analysis per edit: share the `text` and `alt` programs when their texts are equal.~~
   Done in #1437: a program is reused for the same texts and libraries under any key.
3. ~~Phase 1, step 1: the byte cache, behind a switch, compared with `OBJECK_LSP_NO_CACHE=1`.~~
   Done, with library instructions no longer built; see "Phase 1 results".
4. Profile the superlinear 10k-line `obc` case and file what it finds as its own issue.
