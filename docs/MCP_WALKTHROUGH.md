# MCP walkthrough: declare, serve, consume, hand to an agent

The Model Context Protocol (MCP) is how an AI model finds and calls a program's functions. This walkthrough
builds one small tool four ways. Every output below is from a real run on the build that wrote this page;
the programs are in `programs/deploy/` (`mcp_declare_40`, `mcp_stub_41`, `mcp_agent_42`, `mcp_resources_43`, `mcp_serve_44`) and compile with the
libraries on their first line. The reference is [ai_sections/mcp.md](ai_sections/mcp.md); the design and what
is not possible yet is [design/MCP_FIRST_CLASS.md](design/MCP_FIRST_CLASS.md). A shorter tour of the whole release is
[What is new in v2026.11.4](guides/whats_new_11_4.md).

## 1. Declare: a function is a tool

A tool is a function plus a name, a description and a JSON Schema that tells a model its arguments. You write
the function and a doc comment; a line that says `@tool` in the comment is the whole declaration, and the
compiler writes the rest from what is already there:

```ruby
enum Style { FORMAL, CASUAL }

class Tools {
  #~
  Greet a person.
  @tool
  @readonly
  @param name who to greet
  @param style how to greet
  ~#
  function : Greet(name : String, style : Style) ~ String {
    if(style = Style->CASUAL) { return "hi {$name}"; };
    return "Good day, {$name}";
  }
}
```

The description is the comment's text, the schema comes from the parameter types (the enum becomes a list of
allowed values, a default would make a parameter optional), `@readonly` is the MCP `readOnlyHint`, and
`Tools->McpTools(server)` registers every tool of the class. `obr mcp_declare_40.obe` lists what the server
offers and calls it (`remove_file` is the destructive tool the third section uses):

```
The server lists 3 tools:
  add (read-only): Add two integers.
    schema: {"properties":{"a":{"description":"the first number","type":"integer"},"b":{"description":"the second number","type":"integer"}},"required":["a","b"],"type":"object"}
  greet (read-only): Greet a person.
    schema: {"properties":{"name":{"description":"who to greet","type":"string"},"style":{"description":"how to greet","enum":["FORMAL","CASUAL"],"type":"string"}},"required":["name","style"],"type":"object"}
  remove_file (destructive): Delete a file.
    schema: {"properties":{"path":{"description":"the file to delete","type":"string"}},"required":["path"],"type":"object"}
add(17, 25)                  -> 42
greet("Ada", FORMAL)         -> Good day, Ada
greet("Ada", CASUAL)         -> hi Ada
add(1, "two")                -> refused: Invalid arguments for add: argument 'b' must be of type integer
greet("Ada", "RUDE")         -> refused: Invalid arguments for greet: argument 'style' must be one of: FORMAL, CASUAL
add(1) with no b             -> refused: Invalid arguments for add: missing required argument 'b'
```

Bad arguments never reach the function: the server answers with a result the model can read and correct. A
function type, a class that is not a record, a wrong tag or a default that is not a literal is a compile error
at the function. The library form of the same declaration, for code that cannot use the tag, is `McpToolSpec`
(`->Add("name", "String", "who to greet")->ReadOnly()->AddTo(server, handler)`); the compiler's output is
checked against it.

## 2. Serve

A program with `@tool`, `@resource` or `@prompt` functions needs no server code: `obr --mcp prog.obe` serves them on
standard input and output, which is what Claude Code and Claude Desktop launch (put
`{"mcpServers":{"demo":{"command":"obr","args":["--mcp","mcp_declare_40.obe"]}}}` in `.mcp.json`), and
`obr --mcp-http=8080 prog.obe` serves the same over streamable HTTP (`--mcp-http-host=0.0.0.0` to listen beyond
the loopback address; `--mcp-http=0` picks a free port and prints it on standard error). The VM starts the
program at the `McpMain` the compiler wrote for the class instead of at `Main`; a program that has several
tagged classes writes its own `McpMain(args : String[])` that registers them all and calls
`McpServe->Run(server, args)`. On standard input and output the protocol owns standard output: anything the
program prints there corrupts it, so log with `ErrorLine` (standard error). The program must still have a
`Main` (it is what runs without the flag). Tested by `mcp_launch_test.py` (stdio in three execution modes,
hostile input, HTTP, the refusals); the same client library that tests it is what the stub generator uses.
An older `--stdio` flag in `mcp_declare_40.obs` is the program's own and still works.

Resources and prompts come from the same comment: `@resource weather://{city}/forecast/{days}` serves data and
`@prompt` a prompt template (`mcp_resources_43` shows all three kinds, `mcp_serve_44` is the smallest server).
A start of a session by hand (`mcp_serve_44` offers one tool, `count_words`; the tool call is in [What is new](guides/whats_new_11_4.md)):

```
$ printf '%s\n' '{"jsonrpc":"2.0","id":1,"method":"initialize","params":{"protocolVersion":"2025-11-25","capabilities":{},"clientInfo":{"name":"t","version":"1"}}}' '{"jsonrpc":"2.0","method":"notifications/initialized"}' '{"jsonrpc":"2.0","id":2,"method":"tools/list"}' | obr --mcp mcp_serve_44.obe
{"id":1,"jsonrpc":"2.0","result":{"capabilities":{"prompts":{"listChanged":false},"resources":{"listChanged":false,"subscribe":false},"tools":{"listChanged":false}},"protocolVersion":"2025-11-25","serverInfo":{"name":"Notes","version":"1.0"}}}
{"id":2,"jsonrpc":"2.0","result":{"tools":[{"annotations":{"readOnlyHint":true},"description":"Count the words in a text.","inputSchema":{"properties":{"text":{"description":"the text to count","type":"string"}},"required":["text"],"type":"object"},"name":"count_words"}]}}
```

## 3. Consume through a typed client

Calling a server with `CallTool("price", "{\"item\":\"tea\"}")` builds JSON by hand. `mcp_stubs` asks a server
for its tools and writes a class with one typed method per tool:

```
obr mcp_stubs.obe --cmd "obr shop_server.obe --stdio" --class ShopClient --out ShopClient.obs
```

```ruby
shop := ShopClient->New(client);
shop->Price("tea");                 # 4.50
shop->Restock("tea", 12, true);     # 12 x tea, rush delivery   (the optional "rush" is a second overload)
shop->SearchJson(terms);            # the structured result, because the tool declares an outputSchema
shop->Price("coffee");              # Nil, and shop->GetError() is "no such item: coffee"
shop->Price("tea", 3);              # does not compile
```

`mcp_stub_41` runs this end to end in one process and prints those lines.

**Keep the client honest at build time.** Save what the server said next to the generated client
(`--save-manifest shop.manifest.json`) and run `obr mcp_stubs.obe --manifest shop.manifest.json --class ShopClient --check ShopClient.obs`
before `obc`: it needs no server, and when the server's tools have changed it fails the build, naming each method
that was removed, added or changed. Regenerate the client and every call written for the old signature is a compile
error. This is a build step rather than an `obc` flag: the compiler already type-checks the calls, and does not start servers.

## 4. Hand functions to an agent, and let the effect decide who approves

`McpAgentTools->AddAll(registry, client)` makes `API.Agent` tools from a server's tools (the server here is the
`@tool` class), and the hints drive approval; `FromSpec(spec, handler)` does the same for an `McpToolSpec`. `mcp_agent_42` gives a scripted model three tools (one read-only, one unmarked, one
destructive) and runs the agent with `SetApproveUnlessReadOnly(true)` and an approver that refuses deletions:

```
  [approval asked for note: yes]
  [approval asked for delete_file: no]
add: ok - 42
note: ok - noted: the sum is 42
delete_file: refused - Error: the user declined to run 'delete_file'
```

The read-only `add` ran without a question, the unmarked `note` asked, and the destructive `delete_file`
asked and was refused before its function ran.

## 5. With a local model

The agent loop of section 4 does not care which model sits behind it. `OllamaBackend` runs it on a model served
by [Ollama](https://ollama.com) on your own machine, with no key. **Requires a local Ollama server**;
`programs/examples/guides/agent_local.obs` hands an `@tool` function to `qwen3:8b`:

```
$ obr agent_local.obe
answer: The sum of 17 and 25 is **42**.
tool: add({"a":17,"b":25}) -> 42
stop reason: completed, model calls: 2
```

## 6. With a hosted model

`tools/mcp/mcp_live_stub.obs` hands the shop server's tool definitions to a model through `McpToolBridge` and
runs every call the model makes through `ShopClient`. Run it from the directory that holds your key files (the
libraries read them; the program does not):

```
obr mcp_live_stub.obe claude           claude-sonnet-5-5
  call 1: price({"item":"tea"}) -> 4.50
  call 2: restock({"count":12,"item":"tea","rush":true}) -> 12 x tea, rush delivery
  call 3: search({"max_results":5,"terms":["green","tea"]}) -> {"exact":true,"matches":2}
  calls=3 turns=2 input_tokens=1711 output_tokens=323

obr mcp_live_stub.obe openai           gpt-5.4-mini (Completion->CompleteWithTools; it reports no token counts)
  the same three calls, calls=3 turns=4
```

The visual version is `programs/examples/mcp_canvas.obs`: two painters, Claude and an OpenAI model, paint one
canvas through MCP tools that are declared with `McpToolSpec`; the scripted painters call the same server through
a generated typed client (`obr mcp_canvas.obe --demo`, or `--stub` to print that client).

## What this does not do yet

Enums and records must be declared in the
sources being compiled, before the class that uses them; an enum cannot be an optional parameter (a default must
be a literal). A tool's result is text, a `JsonElement` (structured) or an `McpToolResult`; a record result and a
derived `outputSchema` beyond "an object" are not derived. The design document has the reasons and the plan.
