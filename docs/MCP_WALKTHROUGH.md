# MCP walkthrough: declare, serve, consume, hand to an agent

The Model Context Protocol (MCP) is how an AI model finds and calls a program's functions. This walkthrough
builds one small tool four ways. Every output below is from a real run on the build that wrote this page;
the programs are in `programs/deploy/` (`mcp_declare_40`, `mcp_stub_41`, `mcp_agent_42`) and compile with the
libraries on their first line. The reference is [ai_sections/mcp.md](ai_sections/mcp.md); the design and what
is not possible yet is [design/MCP_FIRST_CLASS.md](design/MCP_FIRST_CLASS.md).

## 1. Declare

A tool is a function plus a JSON Schema that tells a model its arguments. Writing the schema by hand next to
the function is where mistakes creep in, so `McpToolSpec` derives it from the Objeck types you name:

```ruby
McpToolSpec->New("greet", "Greet a person")
  ->Add("name", "String", "who to greet")
  ->AddOptional("style", "enum(formal,casual)", "how to greet")
  ->ReadOnly()
  ->AddTo(server, Func2Ref->New(Tools->Greet(McpCall) ~ McpToolResult)<McpCall, McpToolResult>);
```

`obr mcp_declare_40.obe` lists what the server offers, with the derived schema and the hint, and calls it:

```
greet (read-only)
  schema: {"properties":{"name":{"description":"who to greet","type":"string"},"style":{"description":"how to greet","enum":["formal","casual"],"type":"string"}},"required":["name"],"type":"object"}
greet("Ada")                 -> Good day, Ada
greet("Ada", "rude")         -> refused: Invalid arguments for greet: argument 'style' must be one of: formal, casual
add(1) with no b             -> refused: Invalid arguments for add: missing required argument 'b'
```

Bad arguments never reach the function: the server answers with a result the model can read and correct.

## 2. Serve

The same server speaks the protocol on standard input and output with `obr mcp_declare_40.obe --stdio`
(or `server->ServeHttp(8080)`), which is what Claude Code and Claude Desktop launch: put
`{"mcpServers":{"demo":{"command":"obr","args":["mcp_declare_40.obe","--stdio"]}}}` in `.mcp.json`. The
protocol path is tested against a scripted client, and the MCP reference records the one real-client check
(Claude Code with `mcp_server_demo`). This example's `--stdio` mode was driven by hand with an initialize and a
`tools/call` and answered correctly; it has not been tried with Claude Code.

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

## 4. Hand functions to an agent, and let the effect decide who approves

`McpAgentTools->FromSpec(spec, handler)` makes an `API.Agent` tool from the same declaration, and the hints
drive approval. `mcp_agent_42` gives a scripted model three tools (one read-only, one unmarked, one
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

## 5. With a real model

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

The types are still written twice, once in the function and once in the spec, and the function still reads its
arguments with `McpCall`. Deriving the spec and the argument reading from the function itself, with its doc
comment as the description, and `obr prog.obe --mcp` to serve a program with no code, need compiler support;
reflection cannot give parameter names or call a method. That is the next stage in the design document.
