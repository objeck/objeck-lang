# MCP as a first-class part of Objeck: design

Status: DRAFT, not part of any release. Stage 0 (this document) and Stage 1 (library and tools, no
compiler or VM change) are in review; Stages 2 and 3 are proposals. Every statement about the current
source cites the file; every claim about the future is marked **untested**.

## 1. Goal

Today an author who wants a model to call an Objeck function writes the function, then writes a JSON
Schema for it by hand, then registers both (`core/compiler/lib_src/mcp.obs`, `API.MCP`). The function's
name, its doc comment and its parameter types already say what the schema says. The goal is to remove the
second copy, and to make the other direction (calling a server) typed:

1. **Declare**: write a function, get the tool definition derived from it.
2. **Serve**: `obr --mcp prog.obe` (stdio) or `obr --mcp-http=<port> prog.obe` serves a program's tools with no code.
3. **Consume**: generated typed stubs, so `weather->Forecast("Denver", 3)` is checked by the compiler.
4. **Hand to an agent**: plain functions (or a server's tools) become `API.Agent` tools directly.
5. **Effects**: read-only / destructive hints (the MCP `annotations`) drive the agent's approval rules.
6. **Cancellation, time limits, progress**: mapped onto the existing cancel token and runtime.

Rules for the work: correctness first; no hot-path change; no slowdown in `obc` compile time; **no change
to the generated code of a program that does not use the feature**.

## 2. Research questions and claims

The feature set is the subject of a thesis chapter, so each claim below is stated so it can fail.

| RQ | Claim | How it is evaluated | Status |
|---|---|---|---|
| RQ1 equivalence | The schema derived from a declaration equals, as JSON, the schema a person wrote by hand for the same tool. | Oracle: every hand-written schema in the repository's servers and examples (`mcp_core_test`, `mcp_stdio_fixture`, `mcp_server_demo`, `mcp_canvas`: fifteen schemas at Stage 1) is re-declared; `McpToolSpec->Same` compares them. Fails on the first difference. Stage 2 re-runs the same oracle against the compiler's derivation. | Stage 1: passes for the type-spec path (`mcp_declare_test`). The doc-comment path is **untested**. |
| RQ2 agent accuracy | An agent given derived schemas makes no more wrong calls (schema-invalid arguments, wrong tool, refused or failed calls per task) than with hand-written ones. | Equal schemas give identical wire bytes, so for them the difference is zero by construction; the question is only about descriptions derived from doc comments, which differ in wording. Run the same task set, N runs per model, per variant; count calls by `AgentToolResult` status (`ok`, `invalid`, `error`, `refused`, `denied`, `unknown`). | **Untested** (needs the doc-comment path and paid model runs). Stage 1 has one live run (section 9), which is an existence proof, not a rate. |
| RQ3 cost | Derivation adds no tokens: the tool definitions sent to the model are the same size or smaller. | Bytes and tokens of the `tools` array, and `AgentResult` token totals, derived vs hand-written; latency per call from `McpService->GetStats`. | Stage 1: counters exist; first numbers in section 9. |
| RQ4 effort and defects | Declaring cuts the lines a tool costs, and typed stubs catch argument errors at compile time that JSON blobs catch at run time (or never). | Lines per tool, hand-written vs declared (the canvas example is the corpus); a catalogue of argument mistakes and when each is reported (compile, schema check, handler). | Stage 1: stub compile error shown (`mcp_stubs_test.py`); line counts in section 9. |
| RQ5 unused cost | A program that does not use the feature runs the same instructions it did before. | Compare the emitted `.obe` of the regression corpus before and after (byte equal), compile time of `obc` over the corpus, and in-process call cost of the library with counters off. | Stage 1: library cost measured (section 9); compiler claims **untested** until Stage 2. |
| RQ6 cancellation | A cancelled agent run stops a running tool within a bounded time, and a tool that never polls is abandoned, not killed. | Latency from `AgentCancel->Cancel` to handler return, as a distribution; the honest limit is that a thread cannot be interrupted. | Stage 1: mechanism and a bound test (`mcp_agent_test`); distribution **untested**. |

Instrumentation that exists after Stage 1: `McpService->EnableStats(true)` / `GetStats()` (calls, errors,
rejected arguments, cancellations, total and maximum handler milliseconds per tool; off by default, one
Bool test when off); `AgentResult` already carries tokens and per-step results; `AgentToolResult` status.
Stage 2 adds a compile-time report (`obc --mcp-report`: tools found, schema derived, types that fell back).

## 3. What the compiler and VM have today (Stage 0 evidence)

### (a) Annotations and doc comments

* **No annotation or attribute syntax.** `core/compiler/scanner.cpp` lines 108-157 list every keyword
  (`and not or xor virtual if else do while ... from true false New Nil As TypeOf critical`). The only
  declaration modifiers are `public private virtual native static` (`parser.cpp` ~1929-2019). `@` starts an
  instance-variable name (`scanner.cpp` 1642-1652), so `@tool` before a function is already taken.
* **Doc comments are discarded by the scanner.** `Scanner::Comments()` (`scanner.cpp` 1323-1345) skips
  `#`, `//`, `#~ ... ~#` and `/* */`; no token or tree node carries the text. The comments are read again,
  as text, by `code_doc` (`core/lib/code_doc/doc_parser.obs`, an Objeck program) for the API reference;
  that is a second, heuristic parser ("every declaration starts at column ...").
* **The `.obe`/`.obl` carries no comments and, normally, no parameter names.** The loader reads a
  declaration's name only when the file was built with `-debug` (`core/vm/loader.cpp` 471, 551). Method
  entries are name, return type string, parameter type string, space (`loader.cpp` 596-605).
* **Default arguments exist** (`New(start : Bool := false)`, `lang.obs` Timer), which is the natural
  meaning of "optional parameter" for derivation.

### (b) Runtime reflection

`System.Introspection` (`lang.obs` 14270-14450) has `Class` (`GetName`, `GetMethods`), `Method`
(`GetName`, `GetClass`, `GetParameters`, `GetReturn`) and `DataType` (`GetType`, `GetDimension`,
`GetClassName`); `Class->Instance(name)` makes an object of a class with a zero-argument constructor.
Measured on a method with `(city : String, days : Int, tags : Vector<String>, scale : Map<String, IntRef>,
c : Color, p : Point, f : (Int) ~ Int) ~ Vector<String>` (scratch probe, run on the current toolchain):

| declared | reflection reports |
|---|---|
| `String` | CLASS `System.String` |
| `Int` | INT, class name `Nil` |
| `Vector<String>` | CLASS `Collection.Vector<String>` (generic arguments are present, as text) |
| `Map<String, IntRef>` | CLASS `Collection.Map<String\|IntRef>` |
| an enum `Color` | CLASS `Color` (not distinguishable from a class) |
| a record `Point` | CLASS `Point`; its fields cannot be listed |
| `(Int) ~ Int` | FUNC, no signature |

What it **cannot** do, so a library alone cannot: give **parameter names** (not stored, see (a)), give
**doc comments**, give **default values** or which parameters are optional, list the **fields** of a
record or the **values** of an enum, or **call** a method (there is no `Invoke`; `Method` has getters
only). A function can only be called through a `FuncRef`/`Func2Ref` built at compile time with the
function's name written in source. The tests for the mapping from reflection spellings are in
`mcp_declare_test` (`Collection.Map<String|IntRef>` and friends reach the same schemas).

Consequence for **`McpService->ExposeObject(obj)`**: reflection yields method names and parameter types
but not parameter names or a way to call, so a library cannot implement it. Stage 1 does **not** fake it
(no schema with invented parameter names, no registered tool that cannot run).

### (c) Mapping Objeck types to JSON Schema

The honest table (implemented as `McpToolSpec->SchemaFor`, tested row by row):

| Objeck | JSON Schema | Notes |
|---|---|---|
| `Int` | `{"type":"integer"}` | Objeck `Int` is 64-bit; JSON numbers are doubles in many peers, so values above 2^53 do not round-trip everywhere. The checker accepts any integer literal. |
| `Float` | `{"type":"number"}` | |
| `Bool` | `{"type":"boolean"}` | |
| `String` | `{"type":"string"}` | A `Nil` string is "absent", never JSON `null`. |
| `Char` | `{"type":"string","minLength":1,"maxLength":1}` | Lossy on the way back: a stub types it `String`. |
| `Byte` | `{"type":"integer","minimum":0,"maximum":255}` | `McpSchema` does not check the bounds (only type, enum, item type). |
| `T[]`, `T[,]` | array of `T`, nested once per dimension | Rectangular only; a stub maps 1-D arrays of scalars to `Int[]`, `Float[]`, `Bool[]`, `String[]`, anything else to `JsonElement`. |
| `Vector<T>`, `List`, `Stack`... | array of `T` | Collections hold `IntRef`, `FloatRef`...; the spellings reflection produces are accepted. |
| `Map<String,T>` | `{"type":"object","additionalProperties":T}` | Only String keys. |
| enum | `{"type":"string","enum":[names]}` | Needs the names: **compiler only**; the library form is `enum(a,b,c)`. An Objeck enum is an Int underneath, so the generated adapter must convert name to value. |
| record | `{"type":"object","properties":{...},"required":[...]}` | Needs the fields: **compiler only**. |
| any other class | `{"type":"object"}`, `Known()` false | Says nothing about the contents; a stub types it `JsonElement`. |
| `JsonElement` | `{}` | Any value. |
| function types, `Nil`-returning handles, native handles | not derivable | Rejected at derivation time (Stage 2: a compile error naming the parameter). |

Return values: `String` becomes text content; `JsonElement` or a record becomes `structuredContent` plus
its text rendering, and an `outputSchema` when the type is a record (Stage 2). A raised `Error` or a `Nil`
result is an `isError` result with the message (already what `McpService` does for a raising handler).

### (d) Options for "declare a tool by writing a function"

| Option | What changes | Cost and risk | Test oracle |
|---|---|---|---|
| **D0 library builder** (`McpToolSpec`, Stage 1) | Types written as strings next to the name; no compiler change. | Types are written twice (once in the function, once in the spec); a mismatch is found only by tests. Cannot see enum values or record fields. | RQ1 oracle; round trip through the in-process transport. |
| **D1 doc-comment tag `@tool`** + compiler | The scanner keeps a `#~ ~#` comment that immediately precedes a `function`/`method` and contains `@tool` (only then), attaches the text to the method node; a context pass derives the schema from the declared types, defaults and `@param` lines and **synthesizes an ordinary registration method** (`Class->McpTools(service)`) containing the schema constant and a lambda adapter that reads arguments and calls the function. | No grammar change, so old sources and old compilers (which see a comment) are unaffected. Programs with no `@tool` produce byte-identical `.obe`. A stale or wrong tag is silently a comment. The adapter code is generated source, so VM/JIT/GC are untouched. | RQ1 oracle against every hand-written schema; `.obe` byte equality for the corpus without tags; compile-time delta. |
| **D2 annotation syntax** `[tool(...)]` or `#[...]` | New grammar. `[` already starts array indexing and literals; `#[` is a legal comment today, which keeps old compilers working but is a second comment dialect. | A grammar change is a language commitment (and for the LSP, `code_doc`, syntax highlighters). Costs the most; buys structured arguments (`[tool(readonly)]`) that D1 gets as `@readonly` tags. | Same, plus parser tests for each form and the error messages. |
| **D3 `tool` keyword** | A declaration form `tool Name(...) ~ T {}`. | Reserves a word (breaks programs using `tool` as a name: `mcp_canvas.obs` has a local `tool`), new tree node, new emit path. Highest risk and no capability D1 lacks. | Same as D2. |
| **D4 source generator** (no compiler change) | A tool (like `code_doc`) reads the doc comments as text and writes the registration class. | Zero compiler risk; but it is a second parser that can disagree with `obc` (the failure mode `code_doc` already has), and a build step users forget. Useful as a bridge. | Same oracle, plus parser-disagreement tests. |

Recommendation: **D1**. It is the smallest change that has the information (names, types, defaults,
descriptions) and cannot change anything for programs that do not use it. D2/D3 add syntax without adding
capability. D4 is acceptable only as a stopgap.

### (e) Staged plan

* **Stage 0**: this document.
* **Stage 1 (library and tools, no compiler or VM change)**, implemented in the stacked PR:
  `McpToolSpec` (declare from types; derived schema; annotations; output schema), `McpToolAnnotations` on
  `McpTool` (`readOnlyHint`, `destructiveHint`, `idempotentHint`, `openWorldHint` and `outputSchema` on the
  wire), schema checks for `enum` and array items, `McpService->EnableStats`, `McpStubGenerator` and
  `tools/mcp/mcp_stubs.obs` (typed client stubs), `API.Agent` effect hints and approval rules
  (`AgentTool->SetReadOnlyHint/SetDestructiveHint/NeedsApproval`, `Agent->SetApproveUnlessReadOnly`), and the
  new library `mcp_agent` (`McpAgentTools`: a declared function or a server's tools become agent tools; the
  agent's cancel token reaches the handler). Tests, examples, a demo and a walkthrough.
* **Stage 2 (compiler)**: D1. Scanner keeps tagged comments; parser attaches them; a pass derives schemas
  (enum values, record fields, defaults, `@param` descriptions, `@readonly`/`@destructive`/`@idempotent`
  tags) and synthesizes `McpTools(service)`. Diagnostics for non-derivable parameters. The oracle of RQ1 and
  the byte-equality test of RQ5 gate it. No VM change.
* **Stage 3 (launcher)**: `obr --mcp prog.obe` / `obr --mcp-http=<port> prog.obe`: after load, find the synthesized
  method by name, build a `McpService`, serve. The only VM-side work, startup only, behind the flag.
* **Stage 4 (consumption in the compiler)**: `obc -mcp-stub <server>` as a step that runs the generator.

## 4. Design details of Stage 1

* **Hints are tri-state** (`not stated`, `true`, `false`): a tool listed with no annotations is listed
  exactly as before (no `annotations` key), and an absent hint changes no behaviour. MCP says unannotated
  tools must be treated as untrusted, so the agent has an explicit policy for that, not an implicit one.
* **Approval rules** (`Agent`): a tool needs approval when it is `SetDangerous`, **or** its
  `destructiveHint` is true; `SetApproveAll` asks about everything; `SetApproveUnlessReadOnly(true)` asks
  about every tool not known to be read-only. With nobody to ask, a call that needs approval is refused,
  as before.
* **Stub shape**: one class per server, one method per tool, parameters typed from the input schema, a
  second overload without the optional parameters, `Nil` plus `GetError()` for `isError` and transport
  failures, a `...Json` method returning `structuredContent` when the tool declares an `outputSchema`. A JSON
  object does not keep the order its names were written in, so required parameters come first in the order
  of the `required` array and optional ones by name. Output is deterministic (no timestamps), so it can be
  compared with a golden file.
* **Where code lives**: schema derivation and the generator are in `mcp.obs` (so they are testable in
  process), the command line is `tools/mcp/mcp_stubs.obs`, and agent glue is a new library `mcp_agent`
  because `mcp` must not start linking `agent` (every existing consumer's `-lib` line would break).

## 5. Open questions and limits

* `Int` above 2^53 across JSON; `Char`/`Byte` lose their bounds in a stub; optional `Int`/`Float`/`Bool`
  parameters have no `Nil`, so the stub offers a shorter overload instead of a nullable.
* Cancellation: the mechanism is cooperative. The agent's token reaches a declared function as
  `McpCall->IsCancelled()` through a watcher thread (2 ms poll); a handler that never polls runs to its end
  (or, under `AgentTool->Mode->BOUNDED_THREAD`, is abandoned).
* Progress notifications map to `McpCall->ReportProgress` already; surfacing them to an `AgentObserver` is
  Stage 2.
* `.obl` files built from `mcp.obs`, `agent.obs` and the new `mcp_agent.obs` are at the master toolchain
  version and **must be rebuilt after the 2026.11.4 version bump merges**.

## 6. Stage 1 results (what was built and measured)

All numbers are from one Windows x64 machine (Ryzen 9 7950X3D), the current master toolchain, libraries
rebuilt from source into a scratch copy. They are first measurements, not a study.

| Item | Result |
|---|---|
| RQ1 oracle | 15 hand-written schemas from the repository (`mcp_core_test` 3, `mcp_stdio_fixture` 1, `mcp_server_demo` 3, `mcp_canvas` 8) plus 1 mixed example: all 16 re-declared with `McpToolSpec` equal as JSON. The canvas example's own text mini-language was replaced by `McpToolSpec` and its 8 schemas are the oracle for that change. |
| Tests | `mcp_declare_test` 89 checks, `mcp_agent_test` 29, `mcp_stubs_test.py` 21, `weather_server` self-check, `mcp_canvas --selftest` 23; `mcp_core_test` (122) and the stdio test unchanged. |
| RQ5, library cost with the hooks unused | In-process `tools/call`, 20,000 calls, five alternating runs: 76.4 us (library before) vs 77.5 us (after, counters off), +1.4 percent, inside the run-to-run spread (72 to 84 us); 79.3 us with counters on. The enum and item checks return before any lookup for a number or a boolean. |
| RQ4, lines | The canvas example's 8 tool registrations: 36 lines of schema text mini-language (`Schema`) plus 8 `Reg` calls became 8 `McpToolSpec` declarations with no schema helper. The scripted painters' call sites became typed methods; the generated client is 197 lines that nobody writes. A wrong argument type or count in a stub call is a compile error (`mcp_stubs_test.py`), where `CallTool` with JSON text finds it at run time. |
| RQ6, cancellation | `AgentCancel->Cancel` to handler return through the watcher: 30 runs, median 17.7 ms, p95 33.7 ms, max 50.1 ms, minimum 1.7 ms, on Windows with a handler that polls every 1 ms; the figure includes thread start and Windows timer granularity (about 15 ms), so it is an upper bound on the mechanism, not a floor. **Untested** on Linux and macOS, and for a handler that does not poll. |
| Live, typed stub behind a model | Claude `claude-sonnet-5-5`: 3 tool calls, 2 turns, 1,711 input and 323 output tokens, 2.6 s. OpenAI `gpt-5.4-mini`: the same 3 calls, 4 turns, 5.1 s; the library exposes no token counts. Server-side handler time 0.01 to 0.77 ms per call. One run each: an existence proof. |
| Live canvas | Claude and `gpt-5.4-mini` painted one canvas through the declared tools: Claude 33 calls, 4 turns, 15,659 input and 3,036 output tokens; GPT 24 calls, 11 turns; 57 calls, none refused. |

**Not possible without the compiler** (and not faked): `McpService->ExposeObject(obj)`; deriving parameter
names, defaults, enum values, record fields and descriptions from the function; `obr --mcp prog.obe`;
a compile error for a spec that disagrees with its function.

## 7. Stage 2 (compiler): `@tool`, what was built and measured

Option D1 of section 3(d), as designed. No new keyword, no grammar change, no `.obe` or `.obl` format change, no
VM, JIT or GC change (`git diff core/vm` is empty); the compiler changes are in `scanner.{h,cpp}`,
`parser.{h,cpp}` only.

**How it works.**
1. *Scanner.* `Scanner::Comments()` already walks every comment. For a `#~ ... ~#` comment that was closed, one
   pass over its text looks for a line that starts with `@tool` (after blanks, followed by a blank, the end of the
   line or the end of the comment). Only then is the text kept (`ToolDoc`) and its index put on the next token
   (`Token::doc_id`, `-1` for every other token). Only the last comment before a token counts. Every other
   comment is discarded exactly as before; `@tools`, `@tool_name`, a mention mid-line, the word in a string, in a
   `//`, `#` or `/* */` comment, in a comment cut off by the end of the file, are not tags.
2. *Parser.* `ParseClass` reads the token's `doc_id` where it meets `function`. A tagged comment before anything
   else (a method, a constructor, a variable, an enum, a class) is an error at the comment.
3. *Derivation.* Once every file has been read (so a record or an enum may be declared after the class that
   takes it, or in a later file; a file with a syntax error is left alone), `GenerateMcpTools` reads the
   comment's description, tags and `@param` lines and the function's declared types (a Declaration's default
   expression says optional), looks up enums and records among the parsed declarations, and writes
   **ordinary source text**, which the same parser parses with a second scanner and adds to the class:
   `McpTools(service)` (one `McpToolSpec` chain per tool, with `AddSchema` for each parameter's schema),
   `McpTool_<function>(call)` (reads each argument with `GetInt`/`GetString`/..., calls the function, converts its
   result) and `McpRead_<n>(json)` helpers for arrays, `Vector`, `Map`, enums and records. The text is one line,
   padded with newlines, so an error in it points at the tagged function. A class with no tagged function takes
   none of these paths.
4. *Runtime.* The registered handlers are the library's `McpToolSpec` and `McpService` unchanged, so the checks,
   the hints, the counters, the stub generator and `McpAgentTools` all work on the compiler's output.

**Gates.**

| Gate | Result |
|---|---|
| (a) RQ1 oracle | The 15 hand-written schemas of the repository's servers and examples, re-declared as `@tool` functions, are equal as JSON (`McpToolSpec->Same`) to the originals (`mcp_tool_tags_test`, 62 checks); every row of the mapping table (Int, Float, Bool, Byte, Char, String, JsonElement, `Int[]`, `Vector<String>`, `Map<String,IntRef>`, an enum, a record, a record with a record, an enum and an array in it, `Point[]`) has a schema check and a call check; the library's `SchemaFor` and the compiler give the same schema for the same type. |
| (b) Byte-identical `.obe` | Re-run on the final Stage 2 compiler (the one with the fuzzer fix below): 5,600 compilations (1,400 sources of `programs/regression`, `programs/deploy`, `programs/examples` and `tools`, each at `-opt s0` and `s3`, with its own library list and with a wide one) by the reference obc (the same commit with `scanner.{h,cpp}` and `parser.{h,cpp}` reverted to master) and by the new obc: 3,844 built, and every `.obe` has the same bytes; 1,732 failed (library mixes that do not resolve) with the same text and exit code; **0 differences in files without a tag**. The 24 that differ are 6 files that contain `@tool` (the converted examples and tests: the reference has no `McpTools` to call, or refuses the tag's file). |
| (c) Compile time | 60 programs of `programs/regression` (random sample, seed 7), `-opt s3`, sequential, 5 alternating runs: new 13.28 s median, reference 13.59 s (-2.3 percent; runs 12.6 to 14.8 s). The two compilers are not distinguishable by compile time. (An earlier 9-run measurement of 60 programs gave -0.5 percent, a 5-run one of 80 programs +2.8 percent: the sign changes between runs.) |
| (e) Scanner fuzz | `mcp_tool_tags_diag_test.py` (a tag in a string, in `//`, `#` and `/* */` comments, mid-line, `@tools`, `@tool_name`, a comment between the tag and the function, a doc comment in a method body, nested markers, a comment cut off by the end of the file, CRLF, a 1 MB comment, non-ASCII text in a description and a default, and the crash below) plus a mutation fuzzer over real sources (truncate, delete, duplicate a slice, insert a tag or a nested `#~ #~ @tool ~#` anywhere, CRLF, Unicode, an unterminated comment; 3 seeds, 1,800 mutants, each compiled by both compilers and twice by the new one). **It found a crash**: a tagged function followed by a syntax error inside its own parameter list reached the derivation with a half-built method (access violation, exit 139); derivation now runs only when the parse is clean and the case is in the diagnostic test. After the fix: 0 crashes or hangs, 0 mutants without a tag that differ from the reference, 0 that differ between two runs, except one mutant that the *reference* also answers differently from run to run (an uninitialised line number in a top-level syntax error, `Expected 'use', 'bundle', 'class'...`; not caused by this change, not fixed here). The loader and verifier are untouched. |
| (d) Regression | Windows x64, deploy tree built from this branch (obc, obr, obd rebuilt, libraries from `core/lib`): default mode 952 passed, 16 skipped, 3 failed; `OBJECK_JIT_THRESHOLD=1` 952 passed, 16 skipped, 3 failed; `run_vm_flag_tests.py` 576 passed, 0 failed. The 3 failures are the same in both modes: `http_request_hardening` and `static_files_test` (the known AdGuard environment failures on this machine) and `bad_use_unlinked_bundle`, whose expected hint `Add it with '-lib query'` is missing from the hand-assembled deploy tree: the reference compiler gives the same output against the same tree (control), and both compilers give the hint against a clean library directory, so it is the tree, not this change. |
| Live | `tools/mcp/mcp_live_stub.obs` (a shop server made of three `@tool` functions behind the typed client): Claude `claude-sonnet-5-5` 3 calls, 2 turns, 1,710 input and 316 output tokens, 2.6 s; OpenAI `gpt-5.4-mini` the same 3 calls, 2 turns, 3.5 s; the server's counters show 1 call each, 0 errors, 0 rejected arguments, handler time 0.01 to 0.18 ms. `mcp_canvas.obs` rebuilt on plain `@tool` functions (the switch is gone), headless `--shot`: Claude 35 calls in 4 turns (16,883 input, 3,301 output tokens), GPT 20 calls in 12 turns, 55 calls on one canvas, none refused. One run each: existence proofs, not rates. |
| RQ6 (re-measured) | `AgentCancel->Cancel` to handler return through the watcher, 60 runs, Windows, a handler that polls every 1 ms: median 17.5 ms, p95 33.4 ms, max 50.9 ms, min 1.0 ms. The distribution is multi-modal (about 2, 17 and 33 ms) because of the Windows timer granularity (about 15 ms); it is an upper bound for the mechanism. Untested on Linux and macOS. RQ5: the library is unchanged since Stage 1 (its measurement stands); the compiler side is rows (b) and (c). |

**Honest limits** (also in the AI guide): enums and records must be declared in the sources being compiled, in any
file and order (library enums are not visible while parsing); an enum cannot be an optional parameter or
an array element (a default must be a literal; arrays of enums need a helper that cannot hold an enum); multi
dimensional arrays, `ref` parameters, function types and plain classes are refused; a record result is not
derived (return `JsonElement`), and a `JsonElement` result declares only `{"type":"object"}` as its output
schema; the file must `use API.MCP`; a generated name (`McpTools`, `McpTool_x`, `McpRead_n`) that the class
already uses is an error. The editor grammars highlight the tags; the language server's index is built from the
library sources and does not know the tag.

**What remains.** Stage 3: `obr --mcp prog.obe` and `obr --mcp-http=<port> prog.obe`, which find `McpTools` by name after load
and serve (the one VM-side piece; startup only, behind the flag). Stage 4: a compiler step that runs the stub
generator. Not done: a derived `outputSchema` from a record return type; progress and cancellation reaching an
`AgentObserver`; reading enums and records from a library `.obl`.

## 8. Stage 2b (compiler): `@resource` and `@prompt`

The other two server primitives, by the same mechanism and the same gates as `@tool`. `@resource <uri-template>`
and `@prompt [name]` are recognised by the scanner exactly as `@tool` is (a line that starts with the word, in a closed
`#~ ~#` comment directly before a token); the parser attaches the comment to the function; `GenerateMcpTools` writes,
into the same `McpTools(service)`, a `service->AddResource(McpResource->New(uri, name, description, mime, handler))` or
a `McpPrompt` with its `AddArgument` calls, and one adapter function for each.

| | `@resource` | `@prompt` |
|---|---|---|
| Wire | `resources/list` (a fixed URI), `resources/templates/list` (a URI with `{name}`), `resources/read` | `prompts/list`, `prompts/get` |
| Parameters | exactly the URI's variables, `String`/`Int`/`Float`/`Bool`/`Char` read from the URI text by `McpUriTemplate->Match` (a library function, source only, no VM); a default is an error | the prompt's arguments, which are text on the wire: `String`/`Int`/`Float`/`Bool`/`Char` converted from the text; a default makes one optional (the library already refuses a call that lacks a required one); a `McpCall` parameter is not an argument |
| Result | `String` (text, with `@mime`, default `text/plain`) or `McpContents` (as is); `Nil` reads nothing | `String` (one `user` message) or `McpPromptResult` (as is) |
| Name | the function's name in snake_case | `@prompt name`, else snake_case |

Decisions. *One registration function*: tools, resources and prompts all go into `McpTools(service)`, so Stage 3 looks
for one name. *Text parameters only*: a URI is text and a prompt's arguments are `string` -> `string` in the protocol, so a
structured parameter would be a convention the client cannot see; a function that wants one takes the text and parses
it. *Mixed tags are an error*: one function is one of the three, and the hints (`@readonly`...) belong to tools. *No
percent-decoding* of URI variables yet (limit). A tag word that is not followed by a blank or the end of the line is not
a tag (`@prompts`, `@resource_x`), and `@prompt` / `@resource` in a doc comment are new tags: the byte-identity gate was
re-run with them to see whether any existing doc comment used them (none does).

Evidence. `mcp_res_prompt_tags_test` (31 checks: the library function, fixed and template resources, Char/Float/Bool/String
variables, `McpContents` pass-through, mime, listing, required and optional prompt arguments with descriptions, defaults,
Int/Float/Bool from text, `McpPromptResult`, a `McpCall` parameter, a missing required argument); 18 more compile-error
cases in `mcp_tool_tags_diag_test.py` (each with its line); `programs/deploy/mcp_resources_43.obs`. Byte identity of the
corpus with the Stage 2b compiler (both compilers given the same libraries): 5,600 compilations, 5,576 identical, the 24 that
differ are the same 6 tagged files, 0 in files without a tag. Scanner fuzz with `@resource`/`@prompt` mutations: 1,200
mutants, 0 crashes, hangs, or differences from the reference for untagged ones.

## 9. Stage 3 (launcher): `obr --mcp`, `obr --mcp-http=<port>`

The one VM-side piece, and it runs at startup only.

**How it works.**
1. `vm_options.h` (the one parser every `obr` main shares) reads `--mcp`, `--mcp-http=<port>` and `--mcp-http-host=<address>` like its other flags (a bad port, both transports, a host without HTTP are errors that say what was expected; `obr --help` lists them) and `ApplyVmOptions` puts the request in one environment variable, `OBJECK_MCP`.
2. `Loader::LoadImage` reads that variable once, and only then (a run without the flag takes the one lookup and nothing else): it puts the transport (`stdio`, or `http <address> <port>`) in front of the program's arguments and, after the classes are loaded and before the verifier runs, makes the start method the program's `McpMain(args : String[])`. A `McpMain` the program wrote wins; otherwise the compiler's `McpMain_<class>` must be unique, and nothing to serve or several servers is a load error with the fix in the message. The verifier then checks the new entry point exactly as it checks `Main`. Nothing else in the VM, the interpreter, the JIT or the collector knows about the feature (`git diff core/vm`: `vm_options.h`, `loader.{h,cpp}`).
3. The compiler writes `McpMain_<class>` next to `McpTools` in a class with a tagged function: `McpService->New(<class>, "1.0")`, `<class>->McpTools(server)`, `McpServe->Run(server, args)`. `McpServe` is library source (`mcp.obs`): `stdio` calls `ServeStdio`; `http` calls `ServeHttp(address, port)` and starts a thread that prints `mcp: serving http://<address>:<port>/` on standard error once the (possibly system-chosen) port is bound.

**Decisions.** *An environment variable instead of a changed `Execute` signature*: the mains and `vm.h` keep their interface and the Windows DLL export does not change. *The program keeps a `Main`*: what runs without the flag is the author's business. *Standard output under `--mcp`*: the protocol owns it. The alternative, redirecting the program's standard output to standard error in the VM, cannot be done without a second output path for the protocol (the sink and `PrintLine` share one console handle), which would be a VM change on a hot path; so the rule is documented in `obr --help`, the AI guide and the walkthrough, `ErrorLine` is the logging call, and the HTTP announcement uses it. Progress and cancellation are the `McpCall` of Stage 1; nothing new. *The generated entry is named for its class* (`McpMain_<class>`) so that the VM can tell it from one the program wrote, which wins.

**Evidence.** `mcp_launch_test.py` (regression suite, its own CI step): stdio in the default mode, `--jit=off` and `OBJECK_JIT_THRESHOLD=1` (initialize, tools/list, tools/call, a refused call, resources/read with a URI variable, prompts/get, templates and prompts listed, five progress notifications, standard output only JSON-RPC lines, exit 0 at end of input); hostile input (an empty and a blank line, `{`, `[]`, `null`, a number, a binary line, a 5 MB line, a message cut off by the end of input): the server answers or ignores, never crashes, answers the good request after them and exits 0; HTTP on port 0 (the announcement, initialize with the session header, tools/call, resources/read, stop on terminate); the option errors; nothing to serve; two servers; a program's own `McpMain` serving two classes; and a tagged program run without the flags runs its own `Main`.

## 10. Stage 4 (typed clients at build time): `mcp_stubs --manifest`, `--check`

Consuming a server was already typed at Stage 1 (`mcp_stubs` writes a client class from `tools/list`, and `weather->Forecast("Denver", 3)` is checked by `obc` against it). What Stage 4 adds is the part that makes that a *build* property: the checked-in client must be the server's current one, and the build must not need the server.

**Decision: a companion command, not an `obc` flag.** The two shapes were `obc -mcp-stubs <manifest|--cmd ...>` (the compiler launches or reads a server) and a pre-build step. The compiler already does the type check that matters (a call against the generated class: wrong argument type or count is a compile error, `mcp_stubs_test.py` shows it); what is missing is only knowing when the *server* has moved on. That needs no compiler support and a compiler that starts processes and opens sockets while compiling is a security and reproducibility cost for nothing, so it is a step of the build: `obr mcp_stubs.obe --check WeatherClient.obs ...` before `obc`. Nothing in `obc` changed for Stage 4.

* `--save-manifest <file>` keeps what the server said (name, version, `tools/list`) as JSON, next to the generated client, in version control.
* `--manifest <file>` generates the client from that file: the build needs no server and the result is the same bytes as from the live one (tested).
* `--check <file>` writes nothing: it compares the client the server (or the manifest) describes with the checked-in one and, when they differ, exits 1 naming each method that was removed, added or changed (`- method : public : Forecast(city : String, days : Int, ...)` and `+ method : public : Forecast(city : String, days : String, ...)`) and how to regenerate. A schema change is a failed build with that message; after regenerating, every call written for the old signature is a compile error (tested).

Evidence: `mcp_stubs_test.py` (21 checks at Stage 1, 33 now): manifest round trip, `--check` pass/fail against the live server and the manifest, removed, added and retyped methods named, missing manifest and missing client are errors, a stale program no longer compiles against the regenerated client. **Not done** (honest): stubs for resources and prompts (`ReadX`, `GetPromptX`) and a derived `outputSchema` type; the generator still emits `JsonElement` for a tool's structured result.

## 11. Stage 5 (integration and measurement): what exists, and what does not

**Built: the harness that reproduces the numbers** (`tools/mcp/rq/`, usage in each script's docstring; nothing here is part of a build).

| Script | Reproduces |
|---|---|
| `corpus_identity.py <repo> <reference_bin> <new_bin> <work>` | RQ5 byte equality: every `.obs` of the regression, deploy, examples and tools sources compiled with two compilers at `-opt s0` and `s3`, own and wide library lists; bytes, text and exit code compared; exit 1 if a file without a tag differs |
| `compile_time.py <repo> <reference_bin> <new_bin>` | RQ5 compile time: a random sample of programs, alternating runs, medians |
| `scanner_fuzz.py <repo> <reference_bin> <new_bin> <work> [seed] [mutants]` | the scanner gate: mutants of real sources, tagged and untagged, no crash, no hang, no difference from the reference for untagged ones |
| `cancel_latency.obs` | RQ6: `AgentCancel->Cancel` to handler return, 30 runs per start, printed in milliseconds |
| RQ1 | `mcp_tool_tags_test` and `mcp_declare_test` (the oracle: 15 hand-written schemas against the derived ones) |
| RQ3, RQ4 | `McpService->EnableStats`, `mcp_live_stub.obs` and `mcp_canvas.obs` print calls, turns, tokens and handler time; section 6 and 7 hold the numbers |

**Not built** (so that the claim of "all stages" cannot be read off this list): the live map of MCP traffic (the SDL2 showcase with Claude and OpenAI agents on several Objeck servers); an eval harness for RQ2 (wrong-call rates for derived against hand-written descriptions over a task set, with `llm_eval`); stubs for resources and prompts; `outputSchema` from a record result; percent-decoding of resource URI variables; language-server hover for a tagged function (the language server indexes library documentation, not user code, so the editor grammars highlight the tags and nothing more); the playground libmap and generated web API pages for the new classes (`McpServe`, `McpUriTemplate`) beyond what the generators already produce.
