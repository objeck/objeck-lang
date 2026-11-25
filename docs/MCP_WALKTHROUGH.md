# MCP walkthrough: declare, serve, consume, hand to an agent

The Model Context Protocol (MCP) is how an AI model finds and calls a program's functions. This walkthrough
builds one small tool four ways. Every output below is from a real run on the build that wrote this page;
the programs are in `programs/deploy/` (`mcp_declare_32`, `mcp_stub_33`, `mcp_agent_34`) and compile with the
libraries on their first line. The reference is [ai_sections/mcp.md](ai_sections/mcp.md); the design and what
is not possible yet is [design/MCP_FIRST_CLASS.md](design/MCP_FIRST_CLASS.md).

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
`Tools->McpTools(server)` registers every tool of the class. `obr mcp_declare_32.obe` lists what the server
offers and calls it:

```
greet (read-only): Greet a person.
  schema: {"properties":{"name":{"description":"who to greet","type":"string"},"style":{"description":"how to greet","enum":["FORMAL","CASUAL"],"type":"string"}},"required":["name","style"],"type":"object"}
greet("Ada", FORMAL)         -> Good day, Ada
greet("Ada", "RUDE")         -> refused: Invalid arguments for greet: argument 'style' must be one of: FORMAL, CASUAL
add(1) with no b             -> refused: Invalid arguments for add: missing required argument 'b'
```

Bad arguments never reach the function: the server answers with a result the model can read and correct. A
function type, a class that is not a record, a wrong tag or a default that is not a literal is a compile error
at the function. The library form of the same declaration, for code that cannot use the tag, is `McpToolSpec`
(`->Add("name", "String", "who to greet")->ReadOnly()->AddTo(server, handler)`); the compiler's output is
checked against it.

## 2. Serve

The same server speaks the protocol on standard input and output with `obr mcp_declare_32.obe --stdio`
(or `server->ServeHttp(8080)`), which is what Claude Code and Claude Desktop launch: put
`{"mcpServers":{"demo":{"command":"obr","args":["mcp_declare_32.obe","--stdio"]}}}` in `.mcp.json`. The
protocol path is tested against a scripted client, and the MCP reference records the one real-client check
(Claude Code with `mcp_server_demo`). This example's `--stdio` mode was driven by hand with an initialize and a
`tools/call` and answered correctly; it has not been tried with Claude Code. A one-line `obr prog.obe --mcp`
that serves a program's tools with no code at all is not built yet.

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

`mcp_stub_33` runs this end to end in one process and prints those lines.

## 4. Hand functions to an agent, and let the effect decide who approves

`McpAgentTools->AddAll(registry, client)` makes `API.Agent` tools from a server's tools (the server here is the
`@tool` class), and the hints drive approval; `FromSpec(spec, handler)` does the same for an `McpToolSpec`. `mcp_agent_34` gives a scripted model three tools (one read-only, one unmarked, one
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

`obr prog.obe --mcp` and `--mcp-http <port>` (serve a program's tools with no code) are the next stage; today a
program calls `Class->McpTools(server)` and `ServeStdio()` itself. Enums and records must be declared in the
sources being compiled, before the class that uses them; an enum cannot be an optional parameter (a default must
be a literal). A tool's result is text, a `JsonElement` (structured) or an `McpToolResult`; a record result and a
derived `outputSchema` beyond "an object" are not derived. The design document has the reasons and the plan.
