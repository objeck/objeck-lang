## Agents (tool use across providers)

`API.Agent` is a provider-neutral tool-use loop. You hand a chat model a set of Objeck functions as tools and get the final answer back: the library sends the conversation, runs the tool calls the model asks for, sends the results, and repeats until the model answers in plain text. The same program runs against Anthropic, OpenAI, Gemini or a local Ollama by changing one line, and against a scripted fake model with no key and no network. Compile with `-lib agent,net,json,cipher,misc`.

The library speaks to the providers directly over HTTPS (HTTP for a local Ollama), so it does not need the `anthropic`, `openai`, `gemini` or `ollama` libraries and can be linked next to any of them. Those clients each name their tool class `Tool`, and the compiler resolves another library's `Vector<Tool>` by its short name, so a library that linked two of them would break; every class here has a distinctive name (`AgentTool`, `AgentToolCall`, `AgentStep`, ...) for the same reason.

### The smallest agent

A tool is a name, a description the model reads, a JSON Schema for its arguments and a handler. A handler is a function `(JsonElement) ~ String` wrapped in a `Func2Ref`, or an object that implements `AgentToolHandler`.

```ruby
use API.Agent;
use Collection;
use Data.JSON;

class Weather {
  function : Get(arguments : JsonElement) ~ String {
    city := arguments->Get("city")->GetString();
    return "18C and sunny in {$city}";
  }

  function : Main(args : String[]) ~ Nil {
    schema := JsonParser->TextToElement("{\"type\":\"object\",\"properties\":{\"city\":{\"type\":\"string\"}},\"required\":[\"city\"]}");
    registry := ToolRegistry->New();
    registry->Add(AgentTool->New("get_weather", "Get the weather for a city", schema, Func2Ref->New(Weather->Get(JsonElement) ~ String)<JsonElement, String>));

    # a scripted model: it asks for the tool, then answers
    script := Vector->New()<AgentReply>;
    script->AddBack(AgentReply->Ask("call_1", "get_weather", "{\"city\":\"Paris\"}"));
    script->AddBack(AgentReply->Say("It is 18C and sunny in Paris."));

    agent := Agent->New(ScriptedBackend->New(script), registry);
    result := agent->Run("What is the weather in Paris?");
    result->GetText()->PrintLine();
    result->ToString()->PrintLine();
  }
}
```

Output:

```
It is 18C and sunny in Paris.
[1] call get_weather {"city":"Paris"} -> ok: 18C and sunny in Paris
[2] model: It is 18C and sunny in Paris.
result: completed
answer: It is 18C and sunny in Paris.
```

`ScriptedBackend` replays a list of replies, so tools and guards can be tested without a model. `AgentReply->Say(text)` is a final answer, `AgentReply->Ask(id, name, json_arguments)` a request for one tool and `AgentReply->AskAll(calls)` a request for several in one turn.

### A real model

Swap the backend. Each one looks for its key in this order: the argument, an environment variable, then the first line of a key file in the working directory.

| Backend | Constructor | Key | Wire format |
|---|---|---|---|
| `AnthropicBackend` | `New(model, key)` | `ANTHROPIC_API_KEY`, `anthropic_api_key.dat` | Messages API: `tool_use` blocks, results as `tool_result` blocks in one user message |
| `OpenAIBackend` | `New(model, key)` | `OPENAI_API_KEY`, `openai_api_key.dat` | Chat Completions (`/chat/completions`): `tool_calls` with string arguments, one `tool` message per result. `SetBaseUrl` points it at a compatible server |
| `GeminiBackend` | `New(model, key)` | `GOOGLE_API_KEY` or `GEMINI_API_KEY`, `gemini_api_key.dat` | `generateContent`: `functionCall` and `functionResponse` parts; the key goes in the `x-goog-api-key` header, not the URL |
| `OllamaBackend` | `New(model)` | none | `/api/chat` on `http://localhost:11434` (`SetHost` to change); the model must support tools |
| `ScriptedBackend` | `New(script)` | none | a fake for tests and offline demos |

```ruby
use API.Agent;

class Live {
  function : Main(args : String[]) ~ Nil {
    registry := ToolRegistry->New();
    registry->Add(AgentTools->Calculator());
    registry->Add(AgentTools->CurrentTime());

    # pick one; each looks for its key in the environment or a key file
    backend : ChatBackend;
    backend := AnthropicBackend->New("claude-sonnet-5-5", Nil);
    # backend := OpenAIBackend->New("gpt-5", Nil);
    # backend := GeminiBackend->New("gemini-2.5-flash", Nil);
    # backend := OllamaBackend->New("llama3.2");

    agent := Agent->New(backend, registry);
    agent->SetSystem("Use the tools for any arithmetic or date question.");
    result := agent->Run("What is 17 * 23, and what is today's date in UTC?");
    if(result->IsCompleted()) {
      result->GetText()->PrintLine();
    }
    else {
      "run failed ({$(result->GetStatus())}): {$(result->GetError())}"->ErrorLine();
    };
  }
}
```

Without a key this prints `run failed (error): no API key: pass one, set ANTHROPIC_API_KEY, or create anthropic_api_key.dat`. The backends also take `SetBaseUrl`, `SetCertificate` (pin a gateway's certificate), `SetMaxTokens` and `SetTemperature`.

The demo program takes a provider, a model and a question, and runs offline when it gets none:

```
obc -src programs/examples/agent_demo.obs -lib agent,net,json,cipher,misc -dest agent_demo.obe
obr agent_demo.obe                                              # scripted model, no key
export ANTHROPIC_API_KEY=...
obr agent_demo.obe anthropic claude-sonnet-5-5 "What is 17 * 23, and what is the weather in Paris?"
obr agent_demo.obe openai gpt-5 "..."
obr agent_demo.obe gemini gemini-2.5-flash "..."
obr agent_demo.obe ollama llama3.2 "..."
```

### What the loop does with a call

`Agent->Run(prompt)` sends the conversation, and for every tool call in the reply it applies these gates in order. Whatever fails becomes an error result the model can read, and the loop carries on, so one bad call does not end the run.

| Status | Meaning |
|---|---|
| `unknown` | no tool by that name; the message lists the tools that exist |
| `denied` | the registry's allow or deny list excludes it |
| `invalid` | the arguments broke the tool's JSON Schema, or were not JSON; the handler did not run |
| `refused` | the tool is dangerous (or the agent asks about every call) and the approver said no, or there is no approver |
| `error` | the handler raised an error, hit a runtime fault such as a divide by zero, or returned `Nil` |
| `timeout` | the handler took longer than its limit; its result is discarded |
| `skipped` | the step limit was reached before the call could run |
| `ok` | the handler's result went back to the model |

`Run` returns an `AgentResult`: `IsCompleted()`, `GetText()` (the final answer), `GetStatus()` (`completed`, `max_steps` or `error`), `GetError()`, `GetSteps()` (each `AgentStep` holds the model's `AgentReply` and its `AgentToolResult`s), `GetToolCallCount()`, token totals, `ToString()` (a readable transcript) and `ToJson()`. The conversation is kept, so a second `Run` carries on; `Reset()` forgets it.

A handler fails by raising an error, which the loop catches and shows to the model:

```ruby
Runtime->Raise(Error->New("negative steps are not allowed"));
```

### Tools, allow and deny lists, approval, and watching a run

This program runs all of them against a scripted model that makes seven calls in one turn.

```ruby
use API.Agent;
use Collection;
use Data.JSON;

# A tool with state: it counts how many times it was asked
class Counter implements AgentToolHandler {
  @calls : Int;

  New() {
  }

  method : public : Run(arguments : JsonElement) ~ String {
    @calls += 1;
    step := arguments->Get("by")->GetInt();
    if(step < 0) {
      # a handler fails by raising; the model gets the message as an error result
      Runtime->Raise(Error->New("negative steps are not allowed"));
    };
    total := @calls * step;
    return "{$total}";
  }
}

# Says yes only to small deletions
class Careful implements AgentApprover {
  New() {
  }

  method : public : Approve(call : AgentToolCall, tool : AgentTool) ~ Bool {
    return call->GetArgument("count")->ToInt() <= 3;
  }
}

class Remover implements AgentToolHandler {
  New() {
  }

  method : public : Run(arguments : JsonElement) ~ String {
    return "removed";
  }
}

class Watcher implements AgentObserver {
  New() {
  }

  method : public : OnReply(step_index : Int, reply : AgentReply) ~ Nil {
    "step {$step_index}: {$(reply->GetToolCalls()->Size())} tool call(s)"->PrintLine();
  }

  method : public : OnToolResult(step_index : Int, result : AgentToolResult) ~ Nil {
    "  {$(result->GetToolName())}: {$(result->GetStatus())}"->PrintLine();
  }
}

class Guarded {
  function : Main(args : String[]) ~ Nil {
    registry := ToolRegistry->New();
    registry->Add(AgentTool->New("count", "Counts by a step", JsonParser->TextToElement("{\"type\":\"object\",\"properties\":{\"by\":{\"type\":\"integer\",\"minimum\":-10,\"maximum\":10}},\"required\":[\"by\"]}"), Counter->New()));
    registry->Add(AgentTool->New("remove_files", "Deletes files", JsonParser->TextToElement("{\"type\":\"object\",\"properties\":{\"count\":{\"type\":\"integer\"}},\"required\":[\"count\"]}"), Remover->New())->SetDangerous(true));
    registry->Add(AgentTool->New("format_disk", "Wipes the disk", Nil, Remover->New()));
    registry->Deny("format_disk");

    script := Vector->New()<AgentReply>;
    calls := Vector->New()<AgentToolCall>;
    calls->AddBack(AgentToolCall->New("a", "count", JsonParser->TextToElement("{\"by\":2}")));
    calls->AddBack(AgentToolCall->New("b", "count", JsonParser->TextToElement("{\"by\":-1}")));
    calls->AddBack(AgentToolCall->New("c", "count", JsonParser->TextToElement("{\"by\":99}")));
    calls->AddBack(AgentToolCall->New("d", "remove_files", JsonParser->TextToElement("{\"count\":2}")));
    calls->AddBack(AgentToolCall->New("e", "remove_files", JsonParser->TextToElement("{\"count\":500}")));
    calls->AddBack(AgentToolCall->New("f", "format_disk", JsonParser->TextToElement("{}")));
    calls->AddBack(AgentToolCall->New("g", "teleport", JsonParser->TextToElement("{}")));
    script->AddBack(AgentReply->AskAll(calls));
    script->AddBack(AgentReply->Say("Done, with some refusals."));

    agent := Agent->New(ScriptedBackend->New(script), registry);
    agent->SetApprover(Careful->New());
    agent->SetObserver(Watcher->New());
    agent->SetMaxSteps(5);
    result := agent->Run("Do these things.");

    results := result->GetSteps()->Get(0)->GetResults();
    i := 0;
    while(i < results->Size()) {
      "{$(results->Get(i)->GetToolName())} -> {$(results->Get(i)->GetStatus())}: {$(results->Get(i)->GetContent())}"->PrintLine();
      i += 1;
    };
    "final: {$(result->GetStatus())}, {$(result->GetText())}"->PrintLine();

    problem := AgentSchema->Check(JsonParser->TextToElement("{\"type\":\"object\",\"properties\":{\"by\":{\"type\":\"integer\"}},\"required\":[\"by\"]}"), JsonParser->TextToElement("{\"by\":\"two\"}"));
    problem->PrintLine();
  }
}
```

Output:

```
step 1: 7 tool call(s)
  count: ok
  count: error
  count: invalid
  remove_files: ok
  remove_files: refused
  format_disk: denied
  teleport: unknown
step 2: 0 tool call(s)
count -> ok: 2
count -> error: Error: the tool 'count' failed: negative steps are not allowed
count -> invalid: Error: invalid arguments for 'count': arguments.by: must be at most 10
remove_files -> ok: removed
remove_files -> refused: Error: the user declined to run 'remove_files'
format_disk -> denied: Error: the tool 'format_disk' is not permitted here
teleport -> unknown: Error: there is no tool named 'teleport'. Available tools: count, remove_files
final: completed, Done, with some refusals.
arguments.by: expected integer, got string
```

The observer's `OnReply` and `OnToolResult` run as each step happens, so a program can show progress while a long run is going. This streams steps, not the model's tokens.

### Declaring an agent

An agent declaration states the model, the instructions, the tools and the limits in one place. It stands where a class does:

```ruby
use API.MCP, API.Agent, Collection;

class Calendar {
  #~
  The weekday of a date.
  @tool
  @readonly
  @param date a date as YYYY-MM-DD
  ~#
  function : Weekday(date : String) ~ String {
    return "{$date} is a Friday";
  }

  #~
  Book a meeting.
  @tool
  @param date a date as YYYY-MM-DD
  @param title what the meeting is called
  ~#
  function : Book(date : String, title : String) ~ String {
    return "booked '{$title}' on {$date}";
  }
}

agent Scheduler(script : Vector<AgentReply>, owner : String) {
  backend : ScriptedBackend->New(script);
  instructions : "You keep the calendar of {$owner}. Answer in one sentence.";
  tools : Calendar, AgentTools->Calculator();
  budget { steps : 6; tokens : 4000; }
  trace : AgentTraceBuffer->New();
}

class Example {
  function : Main(args : String[]) ~ Nil {
    script := Vector->New()<AgentReply>;
    script->AddBack(AgentReply->Ask("1", "weekday", "{\"date\":\"2026-11-27\"}"));
    script->AddBack(AgentReply->Ask("2", "book", "{\"date\":\"2026-11-27\",\"title\":\"Review\"}"));
    script->AddBack(AgentReply->Say("Booked the review for Friday."));

    scheduler := Scheduler->Build(script, "Ada");
    result := scheduler->Run("Book a review on 2026-11-27 if it is a weekday.");
    each(step in result->GetSteps()) {
      each(r in step->GetResults()) {
        "{$r->GetToolName()}: {$r->GetContent()}"->PrintLine();
      };
    };
    result->GetText()->PrintLine();
    "{$result->GetStopReason()}, {$result->GetSteps()->Size()} of {$scheduler->GetBudget()->GetMaxSteps()} steps"->PrintLine();
    scheduler->GetConversation()->GetSystem()->PrintLine();
  }
}
```

```
weekday: 2026-11-27 is a Friday
book: booked 'Review' on 2026-11-27
Booked the review for Friday.
completed, 3 of 6 steps
You keep the calendar of Ada. Answer in one sentence.
```

Compile with `-lib gen_collect,json,net,net_server,cipher,misc,mcp,agent,mcp_agent` (without `mcp,net_server,mcp_agent` when no class is named in `tools` and `mcp` is not used). The model here is a `ScriptedBackend`; for a real one change one line, for example `backend : AnthropicBackend->New("claude-sonnet-5-5", Nil);`.

`agent Scheduler(...) { ... }` declares a class `Scheduler` with one function, `Build`, which takes the declaration's parameters and returns a new `Agent` each time it is called. The parameter list may be left out (`agent Plain { backend : ...; }` gives `Plain->Build()`). Nothing is evaluated until `Build` is called; the settings are then evaluated in the order they are written, once per call.

| Setting | Value | Does |
|---|---|---|
| `backend : value;` | a `ChatBackend`; required | the model the agent talks to |
| `instructions : value;` | a `String` | the system prompt (`SetSystem`); a prompt literal's `ToString()` fits here |
| `tools : entry, entry, ...;` | a class name, or an `AgentTool` | a class gives every `@tool` function of it, with the schema the compiler derives (see [MCP](mcp.md)); a value is added as it is |
| `mcp : value, ...;` | an initialized `McpClient` | every tool of that server |
| `allow : name, ...;` `deny : name, ...;` | tool names (`String`) | `ToolRegistry->Allow` and `Deny`: with `allow`, only the listed tools are offered; `deny` always wins |
| `budget { steps : n; tokens : n; tool_calls : n; model_calls : n; wall_ms : n; model_call_ms : n; cost : x; }` | `Int` each, `cost` a `Float` | an `AgentBudget` with those limits |
| `trace : value;` | an `AgentTraceSink` | where the run's events go; read it back with `GetTrace()` |

- **Every value is an ordinary expression** and may use the parameters. It is checked by the compiler where you wrote it: `tools : Calendar, script;` in the program above is reported at `script`, and a misspelled limit at the limit:

  ```
  guide.obs:(28,21): Invalid cast between classes: 'API.Agent.AgentTool' and 'Collection.Vector'
  guide.obs:(29,23): Unknown limit 'turns' in the budget of the agent 'Scheduler': a budget has steps, tokens, tool_calls, model_calls, wall_ms, model_call_ms and cost
  ```

- **Also refused when the program is compiled:** an unknown setting, a setting written twice, a declaration with no `backend`, an empty list, a class in `tools` that has no `@tool` function, and `Calendar->Book` in `tools` (a tool entry is a whole class; name the class and pick with `allow` or `deny`).
- **How an entry of `tools` is read.** A name that is not one of the declaration's parameters is a class; anything else is an expression that must be an `AgentTool`. The tools are offered to the model in the order written, a class's functions in the order of the class.
- **What `Build` returns is a plain `Agent`**, so everything in this guide applies to it: `SetApprover`, `SetCancel`, `SetObserver`, a second `Run` that carries the conversation on, and [`AgentScope`](agent_control.md), which limits a declared agent like any other (the smaller limit wins).
- **At run time** `Build` raises an error when a tool or an MCP client is `Nil`, or when a server's tools cannot be listed.
- **`agent` is still a name.** It is a keyword only at the start of a declaration; `backend`, `budget`, `tools`, `steps` and the other words are compared inside the declaration only. Variables, parameters, methods and classes with those names compile as before.

Tests: `agent_decl_test` (what a declared agent offers, runs, denies and is stopped by, the order of evaluation, an MCP server's tools, a prompt literal and a lambda in a setting, a scope around a declared agent), the `bad_agent_*` compile-error tests and `agent_decl_diag_test.py` (each error with its line and column, in LF and CRLF files, bundles, several files). The program above was compiled and run for this guide, as were the two errors shown. Example: `programs/deploy/agent_decl_48.obs`; design: `docs/design/AGENT_SYNTAX.md`.

### Safety notes

- **Offer the least.** `ToolRegistry->Allow(name)` restricts the model to the listed tools (once the list has a name, only listed tools are offered or run); `Deny(name)` always wins over `Allow`. A tool the lists exclude is not shown to the model, and a call to it anyway is answered `denied` and the handler never runs.
- **Dangerous tools need a human or a policy.** `AgentTool->SetDangerous(true)` makes every call to the tool go through the `AgentApprover` after the arguments pass validation (an invalid call is not worth asking about). With no approver the call is refused, so forgetting to configure one fails closed. `Agent->SetApproveAll(true)` asks about every call. The approver sees the `AgentToolCall` (name and arguments), so it can approve by content, as `Careful` does above.
- **Arguments are checked before the handler runs.** The model's arguments are untrusted input. `AgentSchema` checks them against the tool's schema; write schemas with `required`, `enum`, bounds and `"additionalProperties": false` so a handler can rely on them. A handler should still treat a string as a string: a path, a command line or a SQL fragment is checked by the handler, not by the schema.
- **Model output is untrusted, tool output is too.** A file the model reads may contain instructions aimed at the model. Keep tools narrow, keep dangerous ones behind the approver, and do not give an agent both a tool that reads untrusted content and a tool that can send or delete without approval.
- **API keys are masked.** Every error message, `AgentResult->ToString()` and `ToJson()` pass through `Redactor`, which removes every key a backend has used and anything shaped like one (`sk-...`, `AIza...`, the token after `Bearer`). A provider that echoes a bad key in its error message does not leak it into the transcript. The backends refuse to send a key while `OBJECK_TLS_INSECURE_SKIP_VERIFY` has certificate checking off, as the other clients do. Call `Redactor->Scrub(text)` on anything else you log.
- **The built-in file reader is lexical.** `AgentTools->ReadFile(dir)` refuses an absolute path, a drive letter and any `..` segment, so the model cannot name a file outside `dir`; a symbolic link inside `dir` that points elsewhere is not detected. Results over 64000 characters are cut.

### Built-in tools

`AgentTools` has three ready-made, non-dangerous tools: `Calculator()` (`+ - * / % ^`, parentheses, `pi`, `e`, `sqrt abs sin cos tan ln log10 exp floor ceil round`; division by zero and malformed input come back as error results), `CurrentTime()` (UTC, optionally shifted by `offset_hours`) and `ReadFile(directory)` (a text file under one directory). `AgentTools->AddAll(registry, directory)` registers all three. Calculator results print up to six decimal places.

### Limits and caveats

- **A step is one model call.** `SetMaxSteps(n)` (default 10) allows n model calls in one `Run`. If the model is still asking for tools on call n, its calls are answered `skipped` (so the conversation stays valid and the agent can be `Run` again) and the result is `max_steps`.
- **Timeouts are not interruptions.** `SetToolTimeout(ms)` and `AgentTool->SetTimeout(ms)` measure how long a handler took; a handler that overruns is reported as `timeout` and its result is discarded, but Objeck cannot stop a running handler, so one that never returns still hangs the run. Set the tool's mode to `BOUNDED_THREAD` to stop waiting at the limit; see `agent_control.md`.
- **Calls run one after another**, in the order the model gave them, even when the model asks for several at once. Tool results over 20000 characters are cut (`SetMaxResultChars`).
- **The schema checker covers what tool schemas use**: `type` (one name or a list), `properties`, `required`, `additionalProperties`, `enum`, `const`, `minimum`, `maximum`, `minLength`, `maxLength`, `minItems`, `maxItems`, `items`, `anyOf` and `oneOf`. `pattern`, `format` and `$ref` are not checked. Gemini's function schema is a smaller language than JSON Schema, so `GeminiBackend` trims a schema to the keywords it accepts (it drops `additionalProperties`; the loop still enforces it) and sends a tool with no properties without parameters.
- **OpenAI uses Chat Completions, not the Responses API**, so servers that copy the older interface work; reasoning items from the Responses API are not replayed. Gemini's `thoughtSignature` parts are kept and sent back unchanged. Ollama has no tool call ids, so the library makes one up for each call and matches the result by tool name.
- **No token streaming.** Steps stream through the observer; the model's text arrives whole when a reply completes.
- **Tested without the live services.** `agent_loop_test` runs the loop against a scripted model (no call, one call, sequential calls, parallel calls, a tool that fails three ways, invalid arguments, an unknown tool, the step limit, allow and deny, approval, timeouts, key masking). `agent_backend_test` checks each adapter against recorded response bodies in the providers' documented shapes and against the requests it builds, and runs the whole loop through each adapter. `agent_http_test` runs the Anthropic, OpenAI and Gemini adapters against a local HTTPS server and checks the key headers. Nothing was run against api.anthropic.com, api.openai.com, Google's API or a real Ollama; if a provider sends a field this library does not expect, add a recorded case to `agent_backend_test`.

### API summary

| Class | Purpose |
|---|---|
| `Agent` | the loop: `New(backend, registry)`, `Run(prompt)`, `SetSystem`, `SetMaxSteps`, `SetToolTimeout`, `SetMaxResultChars`, `SetApprover`, `SetApproveAll`, `SetObserver`, `GetConversation`, `Reset` |
| `AgentTool` | one tool: `New(name, description, schema, handler)` with an `AgentToolHandler`, a `Func2Ref<JsonElement, String>` or a `Func2Ref<JsonElement, JsonElement>`; `SetDangerous`, `SetTimeout` |
| `ToolRegistry` | `Add`, `Find`, `GetAll`, `GetPermitted`, `Allow`, `Deny`, `Permits`, `Names` |
| `AgentToolHandler` | interface: `Run(arguments : JsonElement) ~ String` (return `Nil` or raise an error to fail) |
| `AgentApprover` | interface: `Approve(call, tool) ~ Bool` |
| `AgentObserver` | interface: `OnReply(step_index, reply)`, `OnToolResult(step_index, result)` |
| `ChatBackend` | interface a model sits behind: `GetName()`, `Chat(conversation, tools) ~ AgentReply`, `GetLastError()` |
| `AnthropicBackend`, `OpenAIBackend`, `GeminiBackend`, `OllamaBackend` | the adapters; each also has `BuildRequest(conversation, tools)` and `ParseBody(text)` so requests and recorded responses can be inspected without a network |
| `ScriptedBackend` | a fake model that replays a list of `AgentReply` values |
| `AgentReply`, `AgentToolCall` | what a model sent back: text, tool calls (id, name, arguments), stop reason (`end`, `tool_calls`, `length`, `filtered`, `other`), token counts |
| `AgentConversation`, `AgentEntry` | the neutral conversation a backend turns into a request |
| `AgentResult`, `AgentStep`, `AgentToolResult` | the transcript |
| `AgentSchema` | `Check(schema, value) ~ String`: `Nil` when the value fits, else a message naming the first problem |
| `AgentTools` | `Calculator()`, `CurrentTime()`, `ReadFile(directory)`, `AddAll(registry, directory)`, `SafePath(directory, path)` |
| `Redactor` | `Scrub(text)` and `Add(secret)`: mask API keys in text |

---
