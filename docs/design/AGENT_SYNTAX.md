# Agent syntax: typed results, prompt literals, agent declarations, scoped limits

Status, decisions of 2026-11-26: (1) typed results use the opt-in `class Name : json` modifier, no call-site type arguments (implemented, section 1); (4) scoped limits are a LIBRARY call, `AgentScope` (implemented, section 4); `prompt` literals (2) and `agent` declarations (3) are DEFERRED until after v2026.11.4. Part of v2026.11.4 per the maintainer. Composes with the MCP
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

## 1. Typed structured results (IMPLEMENTED, v2026.11.4)

As built: `class Weather : json { ... }` or `class Weather : json(note, tags) { ... }` (the names are the optional
fields). The modifier follows the class name, so no lookahead is needed and every program that parsed before parses
the same. After the class body the parser writes, as source text parsed by the same parser (the way `@tool` does):

* `Weather->JsonSchema() ~ JsonElement`, from the fields;
* `weather->JsonFill(json)` (a method, because a function cannot assign another object's fields);
* `Weather->FromJson(json) ~ Weather`: validates with `API.LlmUtil.JsonSchema->Check`, raises an `Error` naming the
  field, then `New()` and `JsonFill`;
* `Weather->Ask(agent, prompt) ~ Weather` when the file has `use API.AgentJson;`: `API.AgentJson.AgentAsk->Value`
  (StructuredCall over the agent's backend, repair on, two retries, `Error` on failure) then `FromJson`.

The derivation is the one `@tool` parameters use (`McpAnalyze`, one new kind `MK_JSONCLS`, and the record-field walk
factored into `McpAnalyzeFields`), so a json class is also a valid `@tool` parameter and the schemas cannot drift.
Decisions: no call-site type argument (`)<Weather>` collides with class type arguments); the helpers are named
`JsonRead_n` so a class that is both json and a tool host has no clash; a json class nested in another must be
declared above it (the parser has not read a later class yet; the error says so); an optional field is
`anyOf [T, null]` because models write null for what they do not know; the class needs a `New()` with no parameters
(or none declared); `Ask` goes straight to the backend, so an `AgentScope` does not count it (a limitation).
A class without `: json` is not touched (byte-identical `.obe`, checked over the regression and deploy corpus).

Superseded notes of the first draft: the `class : json Name` position and the `Ask`-as-`)<T>` form.

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
