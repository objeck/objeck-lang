# What is new in v2026.11.4

The headline of v2026.11.4 is AI engineering: a function becomes an MCP tool with one doc-comment line,
an agent can run on a model on your own machine, and a run can be stopped by a budget or a cancel. The
[CHANGELOG](../../CHANGELOG.md) is the complete list. Every program below is in
[`programs/examples/guides/`](../../programs/examples/guides/) or [`programs/deploy/`](../../programs/deploy/README.md),
was compiled and run with a toolchain built from this source, and the output shown is what it printed.

## A function is an MCP tool

MCP (the Model Context Protocol) is how an AI model finds and calls a program's functions. Put `@tool`
in the function's doc comment and the compiler writes the name, the JSON Schema and the argument checks.

```ruby
use API.MCP;

class Notes {
  #~
  Count the words in a text.
  @tool
  @readonly
  @param text the text to count
  ~#
  function : CountWords(text : String) ~ Int { ... }
}
```

`obr --mcp` serves the program's tools on standard input and output (`--mcp-http=8080` serves them over
HTTP). Compile `mcp_tool.obs` with `-lib gen_collect,json,net,net_server,cipher,misc,mcp`, then send it
one JSON message per line:

```text
$ obr --mcp mcp_tool.obe
> {"jsonrpc":"2.0","id":2,"method":"tools/call","params":{"name":"count_words","arguments":{"text":"one two  three"}}}
{"id":2,"jsonrpc":"2.0","result":{"content":[{"text":"3","type":"text"}],"isError":false}}
```

(The protocol also wants an `initialize` message first; the walkthrough shows a whole session.) To give the
server to Claude Code or Claude Desktop, put `{"mcpServers":{"notes":{"command":"obr","args":["--mcp","/full/path/mcp_tool.obe"]}}}`
in `.mcp.json`. Under `--mcp` standard output carries the protocol, so a tool logs with `ErrorLine`.

## Resources and prompts

`@resource <uri>` serves data (a URI template such as `weather://{city}/forecast/{days}` reads its
variables into same-named parameters) and `@prompt` serves a prompt template. `mcp_resources_43` runs all
three kinds in one process:

```text
resources: notes://readme (text/markdown)
templates: weather://{city}/forecast/{days} (text/plain)
prompts:   code_review(language, focus?)
tools:     word_count
read notes://readme               -> # Notes: write them down.
read weather://Denver/forecast/3  -> 3 days in Denver: sunny
prompt code_review(Objeck)        -> Review the Objeck code for bugs.
prompt code_review(C, style)      -> Review the C code for style.
tool word_count(a b  c)           -> 3
```

## A typed client, checked at build time

`mcp_stubs` asks a server for its tools and writes a class with one typed method per tool:

```text
$ obr mcp_stubs.obe --cmd "obr --mcp mcp_tool.obe" --class NotesClient --out NotesClient.obs --save-manifest notes.manifest.json
mcp_stubs: wrote 1 tool(s) to NotesClient.obs
$ obr mcp_stubs.obe --manifest notes.manifest.json --class NotesClient --check NotesClient.obs
mcp_stubs: NotesClient.obs matches the server
```

`notes->CountWords("a b c")` is now checked by `obc`. When the server's tool changes (here a second
parameter, `min_length`, was added), `--check` fails the build and names the method:

```text
$ obr mcp_stubs.obe --cmd "obr --mcp mcp_tool2.obe" --class NotesClient --check NotesClient.obs
mcp_stubs: NotesClient.obs is out of date: the server's tools are not the ones it was generated from
  + method : public : CountWords(text : String, min_length : Int) ~ String {
  regenerate it (obr mcp_stubs.obe ... --out NotesClient.obs) and review the calls that no longer compile
```

The exit code is 1. `mcp_stub_41` runs a client end to end in one process:

```text
price("tea")                 -> 4.50
restock("tea", 12)           -> 12 x tea, standard delivery
restock("tea", 12, true)     -> 12 x tea, rush delivery
searchJson([green, tea])     -> matches=2 exact=true
price("coffee")              -> Nil, GetError() = Tool price failed: no such item: coffee
```

## Effect hints decide who approves

`@readonly`, `@destructive`, `@idempotent` and `@openworld` become MCP annotations. An agent can use them:
`Agent->SetApproveUnlessReadOnly(true)` asks about every tool not known to be read-only, and a destructive
tool always asks. `mcp_agent_42` hands three declared tools to a scripted model (one read-only, one
unmarked, one destructive) and an approver that refuses deletions:

```text
  [approval asked for note: yes]
  [approval asked for delete_file: no]
add: ok - 42
note: ok - noted: the sum is 42
delete_file: refused - Error: the user declined to run 'delete_file'
model says: Added the numbers and made a note.
```

The read-only `add` ran without a question; the destructive `delete_file` was refused before its function ran.
The full walkthrough is the [MCP walkthrough](../MCP_WALKTHROUGH.md).

## An agent on a local model

`OllamaBackend` runs the agent loop on a model served by [Ollama](https://ollama.com): no API key, and no
data leaves the machine. **Requires a local Ollama server** (`ollama pull qwen3:8b`) listening on
`http://localhost:11434`; `SetHost` points it elsewhere. `agent_local.obs` hands an `@tool` function to the
model:

```ruby
agent := Agent->New(OllamaBackend->New("qwen3:8b"), registry);
agent->SetBudget(AgentBudget->New()->SetMaxSteps(6)->SetMaxWallMs(120000));
result := agent->Run("What is 17 plus 25? Use the add tool.");
```

```text
$ obr agent_local.obe
answer: The sum of 17 and 25 is **42**.
tool: add({"a":17,"b":25}) -> 42
stop reason: completed, model calls: 2
```

The model's wording differs from run to run; the tool call and the stop reason do not. The model must
support tool calls.

## Stop an agent: budgets, cancel, a hung tool

A run ends with a stop reason: `completed`, `cancelled`, `deadline` or a budget (`budget_steps`,
`budget_model_calls`, `budget_tool_calls`, `budget_tokens`, `budget_cost`). A tool in `BOUNDED_THREAD` mode
is abandoned at its time limit and the model still gets an answer. `agent_control_34` stops a model that
asks for a tool forever:

```ruby
agent->SetBudget(AgentBudget->New()->SetMaxToolCalls(2));
result := agent->Run("keep working");
```

```text
budget:  budget_tool_calls after 2 tool calls ran
cancel:  cancelled after 1 tool calls ran
hung:    completed, the slow tool was a timeout
```

`AgentScope` puts one set of limits, one cancel switch and one trace around every agent run in a block, so
you do not hand a budget to each agent. The limits are totals: `agent_scope_45` gives three questions a scope of
two steps, and the second and third are not started:

```ruby
scope := AgentScope->New()->SetMaxSteps(2)->SetTrace(true);
AgentScope->Run(scope, Questions() ~ Nil);
```

```text
scope: 2 steps allowed
question 1: completed (2 steps)
question 2: budget_steps (0 steps)
question 3: budget_steps (0 steps)
agent scope: 3 runs, 2 steps, 30 tokens, 1 tool calls, halt=budget_steps, events=10
outside the block: no scope (true)
```

Reference: [Agent run control](../ai_sections/agent_control.md).

## Smaller changes you can see

`new_in_11_4.obs` shows four: an out-of-bounds index reports the same message compiled and interpreted, a
local declared without a value starts empty on every pass of a loop, a file records a failed open or write,
and a read into a `Char[]` lands at its offset. Compile it with no `-lib`:

```text
Index out of bounds: 5,2
n=0 x=5
n=1 x=0
n=2 x=0
open ok? false, errno 2
read 4: ...abcd
```

`Console->ReadLine` returns `Nil` at the end of input; a blank line is still `""`. `eof_lines.obs`
fed `a`, a blank line and `b` (no line ending):

```text
$ printf 'a\n\nb' | obr eof_lines.obe
3 lines, 1 blank
```

The other items (HTTP timeouts, thread-fault isolation, AES-256-GCM, ONNX on the GPU, gradient boosting,
forecasting) are in the [CHANGELOG](../../CHANGELOG.md) and the [AI guide](../AI.md).
