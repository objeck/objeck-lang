# Agent syntax: typed results, prompt literals, agent declarations, scoped limits

Status, decisions of 2026-11-26: (1) typed results use the opt-in `class : json` modifier, no call-site type arguments, built on the MCP derivation once #1819-#1822 are in master; (4) scoped limits are a LIBRARY call, `AgentScope` (implemented, section 4); `prompt` literals (2) and `agent` declarations (3) are DEFERRED until after v2026.11.4. Part of v2026.11.4 per the maintainer. Composes with the MCP
stack (#1819-#1822, `docs/design/MCP_FIRST_CLASS.md` on those branches) and does not redo `@tool`.
Rule for all four: pure source-to-source lowering to existing constructs (no VM, JIT or opcode change), and
a program that does not use a feature compiles to a byte-identical `.obe`.

## 0. What the real sources say (facts that shape the design)

* Libraries exist on master: `lib_src/agent.obs` (`Agent`, `ToolRegistry`, `AgentTool`, `AgentBudget`,
  `AgentCancel`, `AgentTraceBuffer`, `ScriptedBackend`, `AgentSchema`) and `lib_src/llm_util.obs`
  (`JsonSchema`, `StructuredCall`, `PromptTemplate`, `PromptVars`, `PromptBuilder`, `TokenEstimator`,
  `JsonRepair`). `StructuredCall->New(chat : ChatFn, schema : JsonElement)` validates and repairs; there is
  no Agent-level `Ask`, so lowering targets `StructuredCall` over a `ChatFn` adapter for the backend.
* Reflection cannot list record fields, enum values, parameter names or doc comments
  (MCP_FIRST_CLASS.md 3(b)); schema derivation therefore must happen in the compiler.
* `{$ expr }` string interpolation is already in `parser.cpp` (~1725-1760); `PromptTemplate` uses `{{name}}`.
* Explicit call type arguments are a documented gap (METHOD_GENERICS_DESIGN.md: `Max(v)<IntRef>` "left for
  later"), because `<...>` after a call already means class type arguments (`Vector->New()<PromptTurn>`).
* There is no annotation syntax; `@` begins an instance variable. MCP chose doc-comment tags for `@tool`.
* Keywords are listed in `scanner.cpp` 108-157; new words must be contextual (see below) or they break
  programs that use them as identifiers (`agent`, `prompt`, `budget` are plausible variable names).

## 1. Typed structured results

Requested: `w := agent->Ask(prompt)<Weather>;`. Not sound as written (see below). Recommended form:

```
class : json Weather { city : String; temp_c : Float; tags : Vector<String>; note : String := Nil; }
w := Ask->Typed(backend, prompt)<Weather>;
```

Soundness problems found:
1. `)<Weather>` collides with the existing `<...>` suffix (class type args) and with the unimplemented
   explicit method type args. A parser special case for an `Ask`-named call breaks as soon as the sugar
   chain adds explicit method type args. Until the call-site type-argument syntax is decided by the owner of
   that chain, lower to `StructuredCall` fed by the generated `Weather->Schema()` and `Weather->FromJson()`
   through a library helper. Needs a decision, not forced here.
2. The compiler must ADD members (`Schema()`, `FromJson(JsonElement)`) to the class. That is only possible
   for a class in the program being compiled, never one already in an `.obl`. So derivation is opt-in at the
   declaration (`class : json`, a contextual modifier beside `private`/`virtual`) and the members are
   synthesized for those classes only. Other classes are untouched (byte identity).
3. Mapping (same table as MCP 3(c)): Int integer, Float number, Bool boolean, String string, `Vector<T>`
   array, nested `json` class object (cycles are a compile error), enum string enum (needs name to value
   adapter), optional = has a default (absent from `required`). Maps, function types, native handles and
   non-`json` classes: compile error naming the field and line.
4. FromJson must reject a wrong-typed value, not default it (JsonSchema validates first; FromJson re-checks
   cheaply and raises an `Error`).
5. Oracle: for at least 10 sample classes, `Weather->Schema()` equals the hand-written schema by
   `JsonSchema->Same`. The MCP stage-2 derivation and this one must share one routine in `context.cpp`.

## 2. `prompt` literals (DEFERRED until after v2026.11.4)

```
p := prompt "Summarise {user_text : String} for {audience : String} in {n : Int} bullets.";
```
Lowering: a `PromptTemplate` constant plus a `PromptVars` built from the holes. Values are never scanned for
placeholders (PromptTemplate guarantee) and are additionally fenced with a delimiter that does not occur in
the value. Compile-time checks: unknown hole, undeclared type, declared but unused hole = error with line.
`p->EstimateTokens()` maps to `TokenEstimator`. Risks: `prompt` is a contextual keyword (only when followed
by a string literal, one-token lookahead, so `prompt := ...` still parses); the scanner must not apply
`{$ }` interpolation inside; a literal brace uses the existing `\{{` escape. The guarantee is slot breakout
only; it does NOT stop prompt injection semantically and the docs must say so.

## 3. `agent` declarations (DEFERRED until after v2026.11.4)

```
agent Helper { backend : ScriptedBackend->New(script); instructions : "Be brief.";
               tools : [Add, Clock, mcp_client]; budget : steps 8; trace : buffer; }
```
Lowers to a factory `Helper->Build()` returning an `Agent` with `ToolRegistry`, `AgentBudget` and
`AgentTraceBuffer`. A plain function tool needs a FuncRef plus a schema, which is what the MCP `@tool`
derivation provides, so this depends on #1819 landing; an entry without a derived schema is a compile
error. Contextual keyword `agent` at declaration position only. Risk: it shares the parser's class-member
loop with the sugar chain (ctor promotion, accessors); conflicts are likely in `ParseClass`.

## 4. Scoped limits (IMPLEMENTED as a library, no syntax)

`AgentScope->Run(scope, Work() ~ Nil)` (docs/ai_sections/agent_control.md). `try { } finally { }` exists
(parser.cpp), so the library leaves the scope on every exit path. State is keyed by `Thread->GetCurrentId`
in a mutex-guarded registry, so no thread-local is needed. The syntax proposal below is kept for the record.

```
budget(steps: 10, tokens: 5000, tool_calls: 20) { ... }
```
Lowering: push and pop of a scope record on a library stack consulted by `Agent->Run`. Open problems that
syntax alone does not solve:
* Objeck has no thread-local and no `finally`. The pop must run on normal exit, on `on(e : Error)`, and on
  `return`/`break`/`continue` inside the block, so the lowering must rewrite every exit path. Whether
  context.cpp can do that is NOT verified; the `using` statement in the sugar chain needs the same
  machinery and should be reused, not duplicated.
* Threads/actors: a new thread does not inherit the scope (documented; explicit hand-off); inner may only
  tighten (minimum of limits, validated at push).
* Cancellation: an `AgentCancel` token owned by the scope.

## 5. Tests and gates (per feature)

`programs/regression/*.obs` with ScriptedBackend (auto-picked-up); a test per error case, compile-error
cases via a checker wired into ci-build.yml, ci-windows.yml and `tools/cicd/local_ci.py`; `.obe` byte
identity at -opt s0/s3; compile-time medians; both JIT modes; WSL2; fuzz smoke; docs; CHANGELOG under
v2026.11.4 and regenerated doc_changelog.html; deploy samples agent_45..48; editor grammar and LSP keywords.

## 6. Order, conflicts, open decisions

Order: 1 (needs the `json` modifier decision) then 2, 3 (needs MCP #1819), 4 (needs `using`). Likely
conflict files: `scanner.cpp` (keyword tables), `parser.cpp` (class member loop, statement parser),
`context.cpp` (synthesis hook). Open decisions for the maintainer: call-site type-argument syntax;
`class : json` modifier vs derive-by-use; whether exit-path unwinding exists for scoped limits.
