# Agent syntax: typed results, prompt literals, agent declarations, scoped limits

Release scope (2026-11-26, after the maintainer asked for everything that was designed to be built): typed results (section 1, `class Name : json`, PR #1841), scoped limits (section 4, `AgentScope`, merged in #1837) and `prompt` literals (section 2) are implemented and in v2026.11.4; `agent` declarations (section 3) are designed here and implemented in the pull request that follows the one for prompt literals.

Decisions: (1) typed results use the opt-in `class Name : json` modifier, no call-site type arguments; (2) a hole of a `prompt` literal binds a variable in scope and is checked at compile time, and a reusable template is a function; (3) an `agent` declaration is a class with one `Build` function that returns an `Agent`; (4) scoped limits are a LIBRARY call, `AgentScope`. All compose with the MCP stack (#1819-#1822, `docs/design/MCP_FIRST_CLASS.md`) and do not redo `@tool`.
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

## 2. `prompt` literals (IMPLEMENTED, v2026.11.4)

```
text := "The meeting moved to Friday.";
count := 3;
p := prompt "Summarise {text} in {count : Int} bullets.";
p->ToString()->PrintLine();     # Summarise <text>The meeting moved to Friday.</text> in 3 bullets.
p->EstimateTokens()->PrintLine();
```

**What it is.** `prompt "..."` is an expression of type `API.LlmUtil.Prompt` (library `llm_util`). The
string may run over several lines. Inside it:

| Written | Means |
|---|---|
| `{name}` | a hole: the value of the variable `name` in scope (local, parameter, `@instance` or class variable, loop variable, a variable captured by a lambda) |
| `{name : Type}` | the same, and the variable must be exactly `Type` (`String`, `Int`, `Float`, `Bool`, `Char`, `Byte`) |
| `{{` and `}}` | a literal `{` and `}` (JSON examples in a prompt) |
| `{$expr}` | the ordinary string interpolation, unchanged: trusted text, not fenced |
| escapes | as in any string |

**Decision: a hole binds a variable in scope; it is not a parameter of a template.** The first draft had
`{user_text : String}` declare a parameter to be supplied later. That needs the set of holes to be part of the
value's type so that a later fill can be checked, which Objeck's types cannot express (a function type keeps
order and types but loses the names, so two `String` holes filled in the wrong order would compile). Binding
from scope makes every check a compile-time check with the machinery that exists, and a reusable template is an
ordinary function whose parameters are the holes:
`function : Summary(text : String, count : Int) ~ Prompt { return prompt "Summarise {text} in {count} bullets."; }`.
The call site of that function is then checked for arity and types by the compiler as any call is.

**Compile-time checks, each reported at the line and column of the hole** (also on a later line of a literal
that spans lines, and in a CRLF file):

* a hole names a variable that is not in scope: `Undefined variable: 'txt'` with the usual "did you mean";
* `{name : Type}` whose variable has another type (no conversion is applied: `Int` for a `Float`, `Int` for a
  `Byte`, `String` for a `Char` are all errors);
* a hole whose variable is an object, an array, a collection or a function (only the six types above can be
  written into a prompt; anything else is put into a `String` first, by the program, visibly);
* an empty hole `{}`, a hole that is an expression or two words, an unknown type, a hole not closed on its
  line, a `}` that closes nothing, an unclosed `{$`, a `$"..."` string.

Every bad hole of a literal is reported in one compile, not only the first.

**Escaping (what "cannot break out of its slot" means, exactly).** A `String` value is written between tags
named for the hole: `<text>...</text>`. The tag is chosen when the prompt is rendered so that neither `<tag` nor
`</tag` occurs in the value, compared without case: `text`, else `text_1`, `text_2`, ... The loop ends because a
value is finite. So the closing tag of a slot is by construction absent from the value, and the rendered text
has exactly one closing tag per slot, after the whole value. The value itself is never changed (no character is
escaped or removed) and never scanned for holes. `Int`, `Float`, `Bool`, `Char` and `Byte` values cannot contain
a tag and are written bare. `Prompt->GetNotice()` returns one sentence for a system prompt naming the tags in
use. **Limit, stated in the docs:** the fence marks where a value begins and ends; it does not stop a model
from obeying instructions written inside the value. `{$expr}` is the escape hatch for text the program trusts
and is not fenced.

**Lowering** (parser, `ParsePromptLiteral`; no new statement or expression kind reaches the emitter):

```
API.LlmUtil.Prompt->New()->Text("Summarise ")->Slot("text", text)->Text(" in ")->Slot("count", count)->Text(" bullets.")
```

Each node is created at the line and column of the source text it came from, so the analyzer's own messages
land on the hole. The text pieces are ordinary string literals, which is how `{$...}` and the escapes keep
working with no second implementation. The `Slot` calls carry a tag (`MethodCall::prompt_hole`, 0 for every
other call) and the analyzer checks them in `AnalyzePromptHole` before resolving the call: that is the only
analyzer change, one integer test on the path of a chained call. A bad hole's value is replaced by `""` after
its error is reported, so the call still resolves and the holes after it are checked too.

**Library** (`llm_util.obs`, class `Prompt`): `Text`, `Slot` (six overloads), `ToString`, `EstimateTokens()`
(a `TokenEstimator` at its defaults) and `EstimateTokens(TokenCounter)`, `GetNames`, `Get(name)`,
`GetTemplate()` (the same prompt as `PromptTemplate` source, `{{name}}` per hole, literal `{{` escaped) and
`GetVars()` (a `PromptVars`), `GetNotice()`, and the function `Prompt->TagFor(name, value)`. `GetTemplate` and
`GetVars` are the bridge to the existing `PromptTemplate`/`PromptBuilder`; the literal itself does not go
through `PromptTemplate` because that would parse the template at run time on every evaluation and would
re-read the literal text for `{{...}}` blocks.

**Contextual keyword.** `prompt` is special only when the next token is a string literal
(`Parser::IsPromptLiteral`, a token-type test before any text comparison). Identifier followed by a string
literal was a syntax error in every position before, so no program changes meaning: `prompt := "x"`,
`prompt->Size()`, `F(prompt)`, a function, method, class, enum item, parameter or `@prompt` field named
`prompt` compile as before (tested). The scanner is untouched.

**Needs** `-lib llm_util,regex,json,gen_collect`; without it the error is
`Undefined class: 'API.LlmUtil.Prompt'` with a line naming the libraries. No `use` is needed for the literal
(the lowering names the class in full); `use API.LlmUtil;` is needed only to write the type name `Prompt`.

**Not done, on purpose:** hole expressions (`{user->GetName()}`): a hole is one name so that what reaches the
model is visible at the hole; format specifiers; a hole for a list (use `{$...}` or build a `String`).

## 3. `agent` declarations (designed; implemented in the pull request after prompt literals)

```
agent Helper(script : Vector<AgentReply>, remote : McpClient) {
  backend : ScriptedBackend->New(script);
  instructions : "Answer in one sentence.";
  tools : Tools, AgentTools->Calculator();
  mcp : remote;
  deny : "delete_file";
  budget { steps : 8; tokens : 5000; tool_calls : 20; }
  trace : AgentTraceBuffer->New();
}

helper := Helper->Build(script, client);     # an API.Agent.Agent
result := helper->Run("Add 17 and 25");
```

**What it is.** A declaration at the level of a class (in or outside a `bundle`). It declares a class `Helper`
with one function, `Build`, whose parameters are the declaration's parameters and which returns a new
`API.Agent.Agent` each time it is called. The parameter list is optional (`agent Helper { ... }` gives
`Helper->Build()`). Nothing is evaluated until `Build` is called; the settings are evaluated in the order they
are written.

| Setting | Value | Lowers to |
|---|---|---|
| `backend : e;` (required) | a `ChatBackend`: `AnthropicBackend->New(model, Nil)`, `OpenAIBackend`, `GeminiBackend`, `OllamaBackend`, `ScriptedBackend` | `Agent->New(e, registry)` |
| `instructions : e;` | a `String` (the system prompt) | `SetSystem(e)` |
| `tools : a, b, ...;` | each entry is a class name (every `@tool` function of the class, with the schema the MCP stack derives) or an expression of type `AgentTool` | `Class->McpTools(service)` + `McpAgentTools->AddAll`, or `registry->Add(e)` |
| `mcp : c, ...;` | `McpClient` expressions: every tool of each server | `McpAgentTools->AddAll(registry, c)` |
| `allow : s, ...;` `deny : s, ...;` | tool names (`String`) | `registry->Allow(s)`, `registry->Deny(s)` |
| `budget { steps : e; tokens : e; tool_calls : e; model_calls : e; wall_ms : e; model_call_ms : e; cost : e; }` | `Int` each, `cost` a `Float` | `AgentBudget->New()->SetMax...(e)`, `SetBudget` |
| `trace : e;` | an `AgentTraceSink`, usually `AgentTraceBuffer->New()` | `SetTrace(e)`; read back with `GetTrace()` |

**Decisions.**

* **A factory, not a subclass.** `Helper->Build(...)` returns a plain `Agent`, so everything that takes an
  `Agent` (`AgentScope`, `X->Ask` of a `class X : json`, `SetApprover`, ...) works with it and nothing about
  `Agent` had to change. A subclass would need `Parent(backend, registry)` as the first statement of `New`,
  before the settings written above it could be evaluated in order.
* **Every setting is an expression, no magic words.** The first draft had `trace : buffer` and
  `budget : steps 8`. A bare word that is sometimes a keyword and sometimes a parameter name cannot be told
  apart at parse time, so `trace` takes an expression and `budget` is a block of `name : expression;` pairs with
  the same shape as the settings around it.
* **A tool is a class of `@tool` functions, not one function.** The MCP derivation writes one
  `McpTools(service)` per class; it does not expose one function's schema by itself, and changing what it
  writes would change the `.obe` of every existing `@tool` program. A subset is chosen with `allow`/`deny`,
  which is also what the agent enforces at run time. `tools : Tools->Add;` is a compile error that says so.
* **How an entry of `tools` is read.** A (possibly bundle-qualified) name that is not one of the
  declaration's parameters is a class; anything else is an expression. The declaration has no other names in
  scope, so the rule has no ambiguous case.
* **`mcp` is its own setting** because whether an expression is an `AgentTool` or an `McpClient` is not
  known when the declaration is parsed, and the two are added by different libraries (`agent` cannot name
  `mcp` classes, by design of the MCP stack).
* **Scoped limits compose, with no syntax.** A declared agent run inside `AgentScope->Run(scope, ...)` is
  limited by both its `budget` and the scope; the smaller limit wins (tested).

**Compile-time checks, each at the line and column of the setting or entry:** an unknown setting or limit name
(the valid ones are listed), a setting given twice, no `backend`, an empty list, a missing `;`, a parameter
named `agent__...`, `Class->Function` as a tool, and, from the analyzer at the position of the user's own
expression, a value of the wrong type (`backend : "text"`), an undefined name, a class with no `@tool` function.

**Lowering** (parser, `ParseAgent`). The declaration is skimmed once for its shape, then written as the class
a person would write and parsed by the same parser, as the MCP stack does for `@tool`:

```
class Helper { function : Build(script : Vector<AgentReply>, remote : McpClient) ~ API.Agent.Agent {
  agent__r := API.Agent.ToolRegistry->New();
  agent__b : API.Agent.ChatBackend := ScriptedBackend->New(script);
  agent__i : String := "Answer in one sentence.";
  agent__s0 := API.MCP.McpService->New("Tools", "1.0"); Tools->McpTools(agent__s0);
  agent__c0 := API.MCP.McpClient->InProcess(agent__s0); agent__c0->Initialize("Helper", "1.0");
  if(API.MCP.McpAgentTools->AddAll(agent__r, agent__c0) < 0) { Runtime->Raise(Error->New("...")); };
  agent__t1 : API.Agent.AgentTool := AgentTools->Calculator(); agent__r->Add(agent__t1);
  ...
  agent__a := API.Agent.Agent->New(agent__b, agent__r); agent__a->SetSystem(agent__i); ...
  return agent__a; } }
```

The user's own text (the parameter list and each expression) is not copied into that source. Each piece is
scanned by its own scanner over the original characters, padded so that every token keeps its line and column,
and spliced in where the written class has a placeholder name (`Parser::agent_splices`, null except while a
declaration is being parsed: one pointer test in `ParseSimpleExpression` and one in `ParseDecelerationList`).
So an error inside `backend : ...` is reported exactly where the user wrote it, and the expression is parsed
inside `Build`, where lambdas and parameters mean what they should. `class` and `agent` have the same length,
so the class name also keeps its column.

**Contextual keywords.** `agent` is special only as the first word of a declaration at class level, followed
by a name and `(` or `{`; an identifier there was a syntax error before. The setting and limit names
(`backend`, `budget`, `steps`, ...) are compared as text inside the block only and are reserved nowhere.
`agent`, `budget`, `tools`, `trace` ... as variables, parameters, methods and classes compile as before (tested).

**Needs** `-lib agent,json,net,cipher,misc` (+ `gen_collect`), and `mcp,net_server,mcp_agent` when `tools`
names a class or `mcp` is used; the file needs `use API.MCP;` in that case for the same reason an `@tool`
function does.

**Not done, on purpose:** one function as a tool (above); `model : "..."` shorthand choosing a provider from a
model name (the mapping would live in the compiler and age badly; `backend` names the model in one expression);
approval rules, cancel tokens and observers as settings (they are set on the `Agent` that `Build` returns).

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
v2026.11.4 and regenerated doc_changelog.html; deploy samples agent_scope_45, json_class_46, prompt_literal_47, agent_decl_48; editor grammars (the words are contextual, so the language server's reserved-word list is unchanged).

## 6. Order, conflicts, open decisions

Order: 1 (needs the `json` modifier decision) then 2, 3 (needs MCP #1819), 4 (needs `using`). Likely
conflict files: `scanner.cpp` (keyword tables), `parser.cpp` (class member loop, statement parser),
`context.cpp` (synthesis hook). Open decisions for the maintainer: call-site type-argument syntax;
`class : json` modifier vs derive-by-use; whether exit-path unwinding exists for scoped limits.

Where sections 2 and 3 touch the compiler, for whoever merges other grammar work: no keyword is added to the
scanner's tables. `prompt` literals are `Parser::ParsePromptLiteral` at the end of `parser.cpp`, one test
(`IsPromptLiteral`) in `ParseSimpleExpression` and one in `ParseStatement`, `ContextAnalyzer::AnalyzePromptHole`
with one test in `AnalyzeExpressionMethodCall`, and one integer in `MethodCall` (`tree.h`).
