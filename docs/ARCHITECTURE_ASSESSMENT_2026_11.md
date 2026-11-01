# Architecture assessment, November 2026

An assessment of the language, compiler, runtime, JIT and collector, debugger, language server, standard library and the build, test and release machinery, as of master `5faaca629f` (v2026.10.3 in development).

## How this was done

Seven reviewers each read one component with no ability to build or run anything: compiler and language, VM runtime and native interface, JIT and garbage collector, debugger and command-line tools, language server, standard library, and build/test/release engineering. The coordinator then checked the claims below that could be checked cheaply, and ran one experiment of its own (the `Int[]` finding).

Every finding carries a tag:

- **V**: verified against the code (or by running it) by the coordinator.
- **R**: reported by a reviewer who read the code and cited `path:line`; not independently re-checked.
- **I**: an inference the reviewer marked as such.

Cost estimates are rough, from the reviewers. Nothing here was benchmarked except where stated. This is a static review: it can miss what only running the system shows.

## Verdict

The design is sound where it matters most and disciplined about proving itself: the collector has named invariants and a verifier that can inject faults to show it notices; CI checks carry negative controls; the updater verifies a signature before trusting a byte of the manifest; the debugger's object-layout knowledge now lives in one header. The recurring weakness is not any single component but **facts that must stay in agreement by hand**: a new intrinsic touches about nine places, a version bump touches 58 files, frame-layout offsets must agree across five, and a dead copy of a parser function silently swallows edits. The second theme is **trust and robustness at the edges**: the VM treats an `.obe` as trusted native code, and library-load failures call `exit(1)` inside processes that embed the compiler (the language server, the REPL, the debugger).

None of this is alarming for a project of this size and age. The risk is that the same few classes of defect keep recurring (recent releases show stale-layout, stale-version and skipped-gate bugs), so the best investments are the ones that turn a convention into a check.

## What is well done

| Area | Strength | Tag |
|---|---|---|
| GC | Invariants A1 to C are named, tiered by cost, and checkable (`memory_verify.cpp`); `OBJECK_GC_VERIFY_INJECT` plants faults to prove the verifier catches them; the lock-order hazard is written down where it lives | R |
| GC | `OBJECK_GC_ROOTS=audit` checks the operand-stack maps against live stacks; interpreted frames no longer rely on a guess | R |
| Testing | Differential runs under five JIT and optimization configurations; checks that reduce coverage need a written reason; about 770 regression tests; skipped tests count as skipped | R |
| CI | About 20 build-free checks, several with "can still detect" negative controls; `runtime_deps.json` is one source of truth enforced in both directions | R |
| Compiler | Phases are separate units; AST nodes come from factories (thread-local), so ownership is leak-free by construction; version mismatches name both toolchains and say which side to upgrade | R |
| VM | Call and operand stacks reserve address space and commit on demand, with three checked limits; trap handlers allocate without collecting; process exit uses `_Exit` when other threads run (#877) | R |
| Updater | The signature is checked before the manifest is trusted; the swap is all-or-nothing with rollback and a health check; non-ASCII install paths are handled | R |
| Debugger | One header owns object-layout knowledge for both front ends; the DAP adapter reports honest errors and honors paging | R |
| Query | Per-block CRC32, write-then-rename, a framed change log that truncates a torn tail; a byte-level format document with an independent decoder | R |
| LSP | Content-keyed analysis caching, debounce, cancellation of queued requests, negotiated position encoding, per-window servers | R |

## Findings

### 1. Facts that must agree by hand

| # | Finding | Evidence | Tag | Recommendation (cost) |
|---|---|---|---|---|
| 1.1 | A dead 1,891-line copy of the intrinsic parser. `Parser::ParseSystemStatement` has no caller; the live code is the inline `#ifdef _SYSTEM` block in `ParseStatement`. An edit to the dead copy has no effect and no error. | `parser.cpp:4510`, `parser.h:228`; `MakeSystemStatement` appears 626 times | V | Delete it (hours) |
| 1.2 | A new intrinsic or trap touches about nine places across six files, and nothing checks they agree. Three trap enumerators have no VM handler. | `traps.h`, `scanner.h/.cpp`, `parser.cpp` (two sites), `intermediate.cpp`, `lang.obs`, `common.cpp`, `common.h`; `LOAD_CLS_BY_NAME`, `FILE_LOGGER_DIR`, `LOAD_INST_UID` | R | A CI check that every trap has a handler (hours); one table that generates the enum, scanner map and switch (about a week) |
| 1.3 | A version bump touches 58 files. The `.obl` set must be rebuilt through WSL/Linux and committed, and a `VER_NUM` mismatch surfaces only at run time or in a Windows CI failure. | the 2026.10.3 bump, commit `8b4f1c4d7b` | V | Stamp every file from one script; assert each `.obl` header equals `VER_NUM` in CI (1 to 2 days) |
| 1.4 | JIT frame layout is spread over five places with almost no `static_assert`: about 50 hand-written offsets per ABI in the AMD64 header, about 40 in ARM64, the two `RegisterRoot` functions, and the collector's frame walkers (walk direction, a 16-word window). The design doc records an entry-zeroing count that went stale when the slots widened from six to ten. | `jit_amd_lp64.h:38-96`, `jit_arm_a64.h:41-83`, `memory.h:69`, `memory.cpp:1893-1899` | R | One `JitFrameLayout` header with `static_assert`s (2 to 3 days) |
| 1.5 | The library list for API-doc generation is copied in five places; four build systems (MSBuild, Makefiles, Xcode, CMake for dependencies) have no cross-check of source lists. | `ci-build.yml`, `release-build.yml`, `code_doc64.in`, `gen_json.sh/.cmd` | R/I | One list consumed by all (1 day) |
| 1.6 | `VER_NUM` is a release id used as a format version, so every point release invalidates every user `.obl` and `.obe`. It is the dotless concatenation of year, month and release, so it is not monotonic across ranges (`2026.1.10` and `2026.11.0` both give 2026110). Only equality is tested today. | `linker.cpp:924`, `loader.cpp:71` | V (the check) / I (the collision) | A separate format version plus an instruction-set hash (2 to 3 days; creates a compatibility promise) |

### 2. Trust and robustness at the edges

| # | Finding | Evidence | Tag | Recommendation (cost) |
|---|---|---|---|---|
| 2.1 | There is no bytecode verifier, no stated trust model and no sandbox hooks. The loader validates structure, not meaning (jump targets, local ids against frame size, trap ids and stack depth are not checked); the interpreter dereferences results that are null when out of range. | `interpreter.cpp:2439` and `:2655` chain `GetClass(...)->GetMethod(...)`; the loader checks counts and ids it stores | V (the unchecked chain) / R (the rest) | Write the trust model (a day); null-guard the chains; a libFuzzer target on `Loader::Load` (2 to 3 days); a verification pass (1 to 2 weeks) |
| 2.2 | Failures end the process: the loader calls `exit(1)` 16 times and the compiler back end 24 times (context 5, intermediate 3, linker 13, emit 3). Any embedding host (the language server, the REPL, the debugger) can be killed by a library-load failure. | `loader.cpp`, `linker.cpp:173-969` | V | A fatal exception caught in `compiler.cpp`, and a diagnostics sink instead of formatted strings (3 to 5 days) |
| 2.3 | The native interface has no ABI version or size field, so a newer library reads past a shorter `VMContext` on an older VM. Parking around blocking calls is a convention, not enforced. | `lib_api.h` (no `abi_version` or `struct_size`) | V | Append `abi_version` and `struct_size` (a day); a scoped-blocking wrapper (1 to 2 days) |
| 2.4 | The language server runs analysis inside its own process; a native crash ends it, and many Objeck-side handlers dereference without Nil checks. | `diags.cpp:187-205` (an SEH guard under MSVC only), `server.obs:126` | R | A Nil-safe parameter helper (a day); a child process for analysis (2 to 3 weeks) |
| 2.5 | The updater does not bind the downloaded version to the signature: the signed trusted comment is parsed but only printed, and the post-install health check runs `obc -v`, never `obr` against `lang.obl`. | `obu.cpp:1456`, `:1994-1998` | R | Sign "version, asset" and compare to the tag; run a trivial program as the health check (2 days plus a key-rotation design) |

### 3. The collector's root model (work in progress)

Phases 0 to 2 of `docs/GC_ROOTS_DESIGN.md` shipped in v2026.10.2: the interpreter's operand stacks are typed, so an `Int` is no longer rewritten there. Phases 3 to 6 (compiled frames, removal of the conservative scans, closure capture blocks) are under way.

| # | Finding | Evidence | Tag |
|---|---|---|---|
| 3.1 | Compiled (JIT) frames are still scanned conservatively: an `Int` equal to a moved object's address, pending in a compiled method, is rewritten by a minor collection. | the fixture `vm_flag_fixtures/gc_address_int_pending.obs` fails 245 times with `--jit=1`; pinned as a known gap in `run_vm_flag_tests.py` section 28 | V |
| 3.2 | **Not in the design's phase list.** Heap `Int[]` shares `INT_TYPE` with object arrays, so an `Int[]` element equal to a moved object's address is rewritten by a minor collection, in default mode, interpreted and compiled alike. Measured: 242 of 2000 iterations. | see the reproduction below; `memory.cpp` around 2313-2398 and 2473-2490 | V |
| 3.3 | Four copies of the object-layout walker (`CheckMemory`, `ScanDirtyObject`, `FixupMemory`, the verifier's walk) must agree on field widths. | `memory.cpp:2352` ("mirroring CheckMemory and FixupMemory") | R |
| 3.4 | Every allocation from every thread contends on one `young_offset` compare-and-swap; there are no thread-local allocation buffers. `CheckObject` takes `allocated_lock` per candidate word. | `memory.cpp:719-724`, `:2867-2874` | R/I (not measured) |

**Reproduction of 3.2** (needs the test hook from PR 1483: `Runtime->TestAddressOf`, inert unless `OBJECK_GC_TEST_HOOKS=1`):

```
class Box { @v : Int; New(v : Int) { @v := v; } method : public : Get() ~ Int { return @v; } }
class IntArrayAddr {
  function : Main(args : String[]) ~ Nil {
    changed := 0;
    for(i := 0; i < 2000; i += 1;) {
      o := Box->New(i);
      held := Int->New[2];
      held[0] := Runtime->TestAddressOf(o);
      before := held[0];
      Churn();
      if(held[0] <> before) { changed += 1; };
    };
    "Int[] element changed by a collection: {$changed} of 2000"->PrintLine();
  }
  function : Churn() ~ Int {
    s := 0;
    for(j := 0; j < 200; j += 1;) { b := Box->New(j); s += b->Get(); };
    return s;
  }
}
```

Run with `OBJECK_GC_TEST_HOOKS=1 obr --nursery=64k`. Exposure today is low: since #1472 an Objeck program cannot produce an address-valued `Int` without the hook, and native handles live outside the nursery range. It is a hole in the model all the same. The fix is a distinct reference-array tag emitted by the compiler (a format change).

### 4. Concurrency details

| # | Finding | Evidence | Tag | Recommendation (cost) |
|---|---|---|---|---|
| 4.1 | `StackInstr::operand3` is plain data patched at run time from several threads (`CheckAutoJit`, `PatchCallSites`) while the method-level fields are atomic. | `common.h:818` | V (plain) / R (patched concurrently) | Relaxed atomics if the cost is under 1% (2 days) |
| 4.2 | The DAP adapter's outgoing sequence number is a plain `int`, and its JSON dump has no `error_handler`, so non-UTF-8 program output can throw on a thread with no `try`. | `dap.h:64`; no `error_handler` in `dap.cpp` | V (both facts) / I (the crash) | Atomic `seq`, `error_handler_t::replace`, carry partial UTF-8 across reads (half a day) |
| 4.3 | The debugger takes one global recursive mutex on every instruction of every thread, so threaded programs run effectively serialized under `obd`. | `debugger.cpp:391`, `:618` | V (the lock) / R (the effect) | An atomic "something to do" fast path (about a week) |
| 4.4 | A worker thread has no top-level `catch`, so a `bad_alloc` there ends the process; signal handling builds an interpreter in signal context. | `interpreter.cpp` `AsyncMethodCall`; `common.cpp:191-322` | R | A catch at thread entry (half a day) |
| 4.5 | Safepoint polls appear only at loop-header back edges, so deep compiled recursion with no loop may not poll. | `jit_amd_lp64.cpp:537-548` | I | Check function prologues; add a poll if absent |

### 5. Test and CI depth

| # | Finding | Evidence | Tag | Recommendation (cost) |
|---|---|---|---|---|
| 5.1 | The deepest test tier does not run on a schedule. The nightly workflow (differential regression, fuzzer, heap verifier, GC stress in both JIT modes) has its cron commented out; its first run after a gap produced 85 new failure signatures. | `nightly-hardening.yml:35-38` | V | Enable the schedule (an hour) |
| 5.2 | No sanitizer or coverage job exists, and the compiler has no unit tests or golden AST/IR dumps; all coverage is whole-program. | no `asan`, `tsan` or `gcov` in the workflows; no test target in `core/compiler` | R / V | A weekly ASAN/UBSAN or TSAN Linux build (1 to 2 days); `--dump-ast` and `--dump-ir` golden files (about a week) |
| 5.3 | Actions are pinned by mutable tag with mixed majors (checkout v4 and v5, upload-artifact v4 and v6); `ubuntu-latest` still appears in 18 workflow files (the linux-x64 legs were pinned in #1480). QUIC dependencies are cloned by tag, not commit; the Windows OpenCV download has no hash. | `.github/workflows/*.yml` | V (tags, runners) / R (rest) | Pin by SHA with Dependabot, move to `ubuntu-24.04`, record hashes (about a day) |
| 5.4 | The release rebuilds from the tag instead of promoting the tested artifacts; `version.h` is regenerated by an inline heredoc that copies the magic numbers from `version.in` with no check that they agree. | `release-build.yml:61-90` | R | Render from `version.in`; later promote the CI artifacts (hours to days) |
| 5.5 | After publish, Windows signing, the release body, the playground deploy and the API-docs upload are manual, and a release can be published with unsigned installers. | `docs/release_process.md:121`, `RELEASE_MANUAL_STEPS` | R | A scheduled or tag-follow-up check that opens an issue (half a day) |
| 5.6 | The `threads_12` retry loop masks a race signature, against the project's own rule. | `ci-build.yml:825-838` | R | Quarantine with a tracking issue, or fix the race |

### 6. Standard library and API design

| # | Finding | Evidence | Tag | Recommendation (cost) |
|---|---|---|---|---|
| 6.1 | There is no single error convention. A process-wide static last-error in the LLM clients (not per thread; the "none" value differs); instance `GetError()` in ML; per-thread `GetError()` in Query; `Raise(LabError)` in `ml_lab`. `System.Error` and `try`/`on` shipped, but no I/O or network library uses them. `Remove` returns Nil (List), the element (Vector) or Bool (Hash). | `openai.obs:6313`, `gemini.obs:2384`, `ml_outlier.obs:129`, `ml_lab.obs:453` | R | One written rule (a doc, then incremental) |
| 6.2 | The LLM clients (OpenAI, Gemini, Ollama, inference) each carry their own endpoint, parsing and last-error scaffolding; OpenAI repeats the authorization header insert 50 times. | `openai.obs:6311`, `gemini.obs:2380`, `ollama.obs:14` | R | A small `ChatClient` interface (3 to 5 days, additive) |
| 6.3 | A failed request dereferences Nil: `QuickGet(...)->GetContent()` with no check. | `openai.obs:4464` | V | Check and return the library's failure value (hours) |
| 6.4 | "Durable" in Query means durable against a process crash only: there is no `fsync` or `FlushFileBuffers` anywhere in the VM, shared code or native libraries. | grep over `core/vm`, `core/shared`, `core/lib` returns nothing | V | A `File->Sync` and calls after write and rename (about a day) |
| 6.5 | Size and overlap: `query.obs` is one 24,590-line file; `Table` is a god class with internals made public; `Set`, `HashSet` and `SetHash` coexist, as do two `Pair` classes and `Map`/`Hash`. | `query.obs`, `gen_collect.obs:1430`, `:7465`, `:7894`, `:24`, `:1849` | V (existence, size) / R (rest) | A "which to use" guide now; split `query.obs` into several source files later |
| 6.6 | Layering leaks: multipart encoding lives in the 5.4k-line `net_server`, so a pure client links the whole server; `misc.obs` is a grab-bag (a spinner and date utilities) that clients depend on. | `openai.obs:39-62`, `build_libs.sh` | R | Move `Multipart*` to `net_common`; split a dependency-free `util` library (1 to 2 days) |
| 6.7 | Deprecated surface despite the no-deprecated rule: the OpenAI Assistants classes (about 1,300 lines) and a documented-deprecated `Data.JSON Decode`. Whether the Assistants API is being sunset was not checked. | `openai.obs:4471-5784`, `json.obs:1662` | R / I | Confirm the sunset; remove or guard |
| 6.8 | Parameter names are public API since named arguments, but CI compares only the class set of the editor index, not signatures. | `ci-build.yml:1923-1938` | R | Diff `objk_apis.json` between release tags (a day) |

### 7. Compiler and language

| # | Finding | Evidence | Tag | Recommendation (cost) |
|---|---|---|---|---|
| 7.1 | Very long functions concentrate the defects: `ParseStatement` about 2,000 lines, `EmitSystemDirective` about 2,100, `AnalyzeCalculationCast` about 540. The defect history (call chains, closures, the s3 inliner) matches. | `parser.cpp`, `intermediate.cpp:1789`, `context.cpp` | R / I | Split by node category (1 to 2 weeks, with the regression suite as oracle) |
| 7.2 | Class and library-class logic is duplicated: parallel `AnalyzeMethodCall` overloads for `Class*` and `LibraryClass*`, 311 references in `context.cpp` (14,183 lines). A fix to overload resolution must be made twice. | `context.cpp:5674`, `:6422` | R | One resolver over a common interface (weeks, incremental) |
| 7.3 | The type system depends on stdlib spelling: about 30 literal checks for `System.String`, `System.Base`, `System.FuncRef` and the box classes, some matched both qualified and unqualified. | `context.cpp:1700`, `:3303-3315`, `:5538-5546` | R | Intern well-known classes once (2 days) |
| 7.4 | Diagnostics are preformatted strings keyed by line, with no codes or ranges, and `ProcessError` is copied four times in the parser and three in the analyzer. | `context.h:255`, `parser.h:96` | R | A `Diagnostic{code, range, message}` sink (3 to 5 days) |
| 7.5 | Back-end singletons are plain statics, unlike the thread-local tree and type factories. | `emit.h:249`, `:950` | R / I | Make them thread-local or owned by the parsed program (a day) |

### 8. Debugger, REPL, language server

| # | Finding | Evidence | Tag | Recommendation (cost) |
|---|---|---|---|---|
| 8.1 | `obd` is a second build of the whole VM (`-D_DEBUGGER -D_NO_JIT`) with direct frame and heap access and hard-coded array and string layouts, so JIT-only bugs cannot be debugged and a layout change silently breaks display. | `debugger.vcxproj:188-224`, `interpreter.cpp:305-315`, `obj_layout.h:276-420` | R | A small read-only `debug_view.h` over the VM (1 to 2 weeks) |
| 8.2 | There is no way to interrupt a running program: `pause` answers an error and the REPL has no Ctrl+C handling. | `dap.cpp:1642`; no `SIGINT` in `core/repl` | V (no SIGINT) / R | A pause flag (1 to 2 days) |
| 8.3 | Expression evaluation uses a fork of the compiler's scanner and parser (2010 to 2013), with hand-re-added number scanning. | `core/debugger/scanner.cpp`, `parser.cpp` | R | A shared literal-grammar test table (a day) |
| 8.4 | The REPL replays the whole buffer on every entry, so side effects repeat and cost grows with the session. Documented. | `core/repl/README.md:122` | R | A warning for I/O now; a persistent VM later |
| 8.5 | The language server re-analyzes the whole program twice per edit and drops every cache on a single global generation; there is no mid-analysis cancellation and no "cached equals fresh" test. | `frameworks.obs:1347`, `server.obs:1065-1081`; `docs/INCREMENTAL_ANALYSIS_DESIGN.md` | R | The design's phases (days to months) |

## Roadmap

**In flight (started with this document).** Six workers each own one workstream and open their own PRs; the coordinator merges when CI is green.

| Workstream | Covers | Findings |
|---|---|---|
| CI hygiene | enable the nightly; `ubuntu-24.04` everywhere; actions pinned by SHA plus Dependabot; a check that every `.obl` carries `VER_NUM` | 1.3, 5.1, 5.3 |
| Compiler hygiene | delete the dead parser copy; a trap-handler consistency check; an "adding an intrinsic" checklist | 1.1, 1.2 |
| Runtime hardening | `abi_version` and `struct_size`; null guards; a worker-thread catch; the operand3 question; the trust-model document | 2.1, 2.3, 4.1, 4.4 |
| Debugger and DAP | UTF-8 and `seq` fixes; unknown requests fail; breakpoint ids; `pause`; the per-instruction lock | 4.2, 4.3, 8.2 |
| Library fixes and guides | Nil-safety sweep; doc inaccuracies; a collections guide; an error-conventions proposal | 6.1, 6.3, 6.5 |
| GC roots | `JitFrameLayout`, then phase 3 (AMD64) | 1.4, 3.1 |

**Next, in order of value (a week or more each).**

1. The rest of the GC roots redesign: ARM64 typed slots (needs ARM64 hardware), removal of the conservative scans and the dead inliner, closure capture blocks, and a reference-array tag for finding 3.2.
2. One table that defines traps and intrinsics and generates the enum, scanner map, parser case and switch (finding 1.2).
3. A loader verification pass with a fuzz target, and trap-category deny flags (2.1).
4. A diagnostics sink and fatal exceptions in place of `exit(1)` (2.2, 7.4), which also helps the language server.
5. `fsync` support and the LLM client interface (6.4, 6.2).
6. A weekly sanitizer build and golden AST/IR dumps (5.2).
7. Promote tested artifacts at release instead of rebuilding (5.4).
8. `debug_view.h`, then the language server's analysis in a child process (8.1, 2.4).

## Not assessed

The JIT code-memory lifecycle beyond the README; lock contention and pause behavior (nothing was profiled); the correctness of generics rules, the optimizer's passes and the Query planner; numerical correctness of the ML and NLP libraries; the TLS, HTTP/2 and HTTP/3 handlers beyond sampling; macOS, Windows ARM64 and Linux arm64 specifics; the contents of CI secrets and variables.
