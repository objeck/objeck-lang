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
  - the superlinear `obc` case with clashing names, which is a lookup-cost bug in
    `GetClass` (a linear scan over all bundles) and wants its own issue.

## First steps

1. Phase 0's timing switch and test, and the measured split recorded in this document.
2. Phase 1, step 1: the byte cache, behind a switch, compared with `OBJECK_LSP_NO_CACHE=1`.
3. File the `GetClass` linear-scan finding as its own issue, with the 10k-line case.
