## Model Context Protocol (MCP)

`API.MCP` is a pure-Objeck implementation of the [Model Context Protocol](https://modelcontextprotocol.io): a **server** that offers tools, resources and prompts to an MCP client such as Claude Desktop or Claude Code, a **client** that uses another program's tools, and a **bridge** that hands a connected server's tools to the Anthropic, OpenAI and Gemini clients above. Compile with `-lib mcp`, and name the libraries it links as well:

```
obc -src server.obs -lib gen_collect,json,net,net_server,cipher,misc,mcp -dest server.obe
```

**Protocol revision: 2025-11-25.** A client that asks for 2025-06-18, 2025-03-26 or 2024-11-05 is answered with that revision; any other is answered with 2025-11-25, as the lifecycle section of the specification requires. This is the last revision with the `initialize` / `notifications/initialized` handshake. The newest revision on the specification site when this was written, 2026-07-28, is stateless: it drops the handshake for per-request `_meta` fields and `server/discover`. It is **not** implemented. The messages below follow the 2025-11-25 schema.

What is supported, and what is not:

| | |
|---|---|
| Transports | stdio (newline-delimited JSON-RPC 2.0) and streamable HTTP; the client also reaches a server in the same process and a server program started once per request |
| Server methods | `initialize`, `notifications/initialized`, `ping`, `tools/list`, `tools/call`, `resources/list`, `resources/templates/list`, `resources/read`, `prompts/list`, `prompts/get`, `notifications/cancelled`, and `notifications/progress` (sent) |
| Not implemented | `logging/setLevel`, `completion/complete`, `resources/subscribe`, `notifications/*/list_changed`, pagination (`nextCursor`), sampling, roots, elicitation, tasks, OAuth for HTTP, resumable SSE streams, JSON-RPC batches (refused as an invalid request, as 2025-06-18 and later require) |

An MCP server is not the same thing as the `Web.HTTP.Server.MCP` handler in `net_server`, which is unchanged and serves HTTP only. `Data.JSON.RPC` was not reused: it runs one request per TCP connection and has neither stdio nor notifications.

### A server over stdio

A tool is a name, a description, the JSON Schema of its input and a function that takes a `McpCall` and returns a `McpToolResult`. `ServeStdio()` reads one JSON-RPC message per line from standard input and writes one reply per line to standard output until the input ends.

```ruby
use API.MCP;

class Greeter {
  function : Main(args : String[]) ~ Nil {
    server := McpService->New("greeter", "1.0.0");
    server->AddTool("greet", "Greet a person by name",
      "{\"type\":\"object\",\"properties\":{\"name\":{\"type\":\"string\"}},\"required\":[\"name\"]}",
      Func2Ref->New(Greeter->Greet(McpCall) ~ McpToolResult)<McpCall, McpToolResult>);
    server->ServeStdio();
  }

  function : Greet(call : McpCall) ~ McpToolResult {
    name := call->GetString("name");
    return McpToolResult->Text("Hello, {$name}!");
  }
}
```

Fed an `initialize` request, the `initialized` notification and a `tools/call`, it answers (the output is exactly what the program printed; the reply to the notification is nothing):

```
{"id":1,"jsonrpc":"2.0","result":{"capabilities":{"tools":{"listChanged":false}},"protocolVersion":"2025-11-25","serverInfo":{"name":"greeter","version":"1.0.0"}}}
{"id":2,"jsonrpc":"2.0","result":{"content":[{"text":"Hello, Ada!","type":"text"}],"isError":false}}
```

Nothing but protocol may reach standard output while the server runs. Log with `ErrorLine()`, which goes to standard error. A message is read a byte at a time, so it can be as long as the limit (4 MiB; `McpStdio->SetLimit` changes it); a longer line is read to its end and answered with a parse error, so the next message is intact. Replies are plain ASCII (non-ASCII text is written as `\u` escapes), so the console encoding cannot change them.

On Windows the VM's console mode hands standard input over as UTF-16 unless it runs with `OBJECK_STDIO=binary`. The reader tells the two apart from the first two bytes and decodes either, so neither needs to be set.

### Tools, resources and prompts

```ruby
server->AddTool(tool);                                  # McpTool: name, description, schema, handler
server->AddResource(McpResource->New("notes://readme", "readme", "The readme", "text/plain", "fixed text"));
server->AddResource(McpResource->New("notes://{name}", "note", "A note by name", "text/plain", handler));
prompt := McpPrompt->New("review", "Review a file", handler);
prompt->AddArgument("path", "the file", true);
server->AddPrompt(prompt);
```

* A resource whose URI holds a `{` is a template: `resources/templates/list` lists it. `resources/read` goes to the resource registered under exactly that URI if there is one, whatever was registered before it; else to the first template the URI fits from end to end (`McpUriTemplate->Match`); else to the first template whose text before the first `{` the URI starts with, for a handler that reads the URI its own way. A handler takes the URI and returns a `McpContents` (`OfText` or `OfBlob`), or `Nil` for "not there", which is the error `-32002`.
* A prompt handler takes a `McpCall`, whose arguments are the strings the client gave, and returns a `McpPromptResult`; a missing required argument is refused before the handler runs.
* `McpCall` has `GetString`, `GetInt`, `GetFloat`, `GetBool` and `Has` for the arguments, `GetArguments()` for the JSON object, `IsCancelled()` and `ReportProgress(progress, total, message)`.
* `McpToolResult` has `Text`, `Error`, `Json` (a structured result that is also written out as text), and `AddText` and `AddImage` for several content items.

### Failures, errors, cancellation and progress

A tool that fails is **not** a protocol error. A handler that returns `McpToolResult->Error("...")`, raises (a `Nil` dereference, an index error, `Runtime->Raise`) or returns `Nil` produces a normal reply whose `isError` is true and whose content says what went wrong, so the model can read it and try again. Arguments that miss the input schema's `required` names or its `type`s (string, number, integer, boolean, object, array, null) are reported the same way. Only the JSON-RPC errors below are protocol errors:

| Code | When |
|---|---|
| -32700 | the text is not JSON (a bare word, truncated JSON, text after the value); the id is `null` |
| -32600 | JSON that is not a request: not an object, a batch, `jsonrpc` is not `"2.0"`, `method` is not a string, `id` is not a string or number |
| -32601 | a method the server does not have |
| -32602 | `params` that are not an object, a missing or non-string `name` or `uri`, an unknown tool or prompt, a missing required prompt argument, `initialize` without a revision, capabilities or client info |
| -32002 | `resources/read` of a resource that is not there (`data.uri` names it) |

A notification is never answered, whatever it is, and neither is a response from the peer.

On stdio `tools/call` runs off the reading thread, so the server keeps reading while a tool works: up to `SetMaxWorkers(n)` calls at once (16 unless set), the rest wait in the order they arrived, and the threads are joined as they end. `notifications/cancelled` for that request id sets `IsCancelled()`, and a cancelled request is **not answered**, as the cancellation section of the specification says; a call cancelled before it began (the cancellation was on the next line, or the call was still waiting) is not run at all. On every transport the request id is taken as soon as the server has the call, before the tool is looked up or its arguments are checked, so a cancellation that arrives while the arguments are still being checked (over HTTP it is a POST of its own) stops the call before its handler starts. That holds whichever way the call would have ended: a call cancelled during the check is not answered even when the check then rejects its arguments (over HTTP it gets `202` and no body, never the `isError` result), and its id is free at once. A request id names one call at a time on a connection (standard input and output, an HTTP session, an in-process client): a second `tools/call` under an id still at work is refused with `-32600`, as the specification forbids reusing an id, and a cancellation reaches only the call of the connection it arrived on, so two HTTP clients that both number their requests from 1 cannot cancel each other. A transport of your own names its connections with `Handle(text, sink, scope)`. A tool that takes time polls it:

```ruby
function : Slow(call : McpCall) ~ McpToolResult {
  call->ReportProgress(1.0, 10.0, "started");   # notifications/progress, when the request carried a progressToken
  while(<>call->IsCancelled()) {
    System.Concurrency.Thread->Sleep(10);
  };
  return McpToolResult->Text("stopped");
}
```

At the end of standard input the server waits for the calls in progress, writes their replies and returns.

### The streamable HTTP transport

```ruby
server->ServeHttp(8080);          # 127.0.0.1:8080, blocks; 0 takes a free port, GetBoundPort() says which
server->ServeHttp("", 8080);      # every interface, only when you mean it
```

One endpoint answers every path. `POST` carries one JSON-RPC message and is answered with `application/json`, or `202` with no body when the message needs no answer. `GET` is `405`: the server starts no stream of its own. `DELETE` ends a session. An `initialize` that is answered (not one refused for its parameters) hands out an `Mcp-Session-Id`, which later requests must carry (`400` without it, `404` for an unknown one; `SetSessions(false)` turns sessions off, and with them cancellation: see the limits below), a `MCP-Protocol-Version` header that names a revision it does not speak is `400`, and a request with an `Origin` header is refused with `403` unless the origin is `localhost`, `127.0.0.1` or `[::1]` (`AllowOrigin("https://app.example.com")` adds one), so a web page cannot reach the server through the visitor's browser. A malformed body, or one that holds more than one message, is `400` with the JSON-RPC error.

Limits of this transport: replies are JSON, never an event stream, so progress notifications are dropped (cancellation works, as it is a separate POST); cancellation is keyed by the session (`Mcp-Session-Id`), and sessions are on unless you turn them off. With `SetSessions(false)` nothing reliable names the client: a request id is unique within one client only, a cancellation arrives on another connection (another port) than the call it names, and an address is shared by every program on a machine and every client behind a proxy. A sessionless server therefore never guesses: every call is kept by itself, so two clients that use the same request id at the same time each get their own call, and a `notifications/cancelled` is answered `202` and ignored. The server tells the client so in the `initialize` result, under `_meta` as `org.objeck/cancellation` (a text that begins `unsupported`); there is no authorization, so keep the loopback address unless something in front of it authenticates; sessions live in memory and never expire; only one HTTP server runs in a process.

### Pointing Claude Desktop and Claude Code at a server

`programs/examples/mcp_server_demo.obs` is a server with three tools, a resource template and a prompt: `calculate` (an arithmetic expression: `+ - * / % ^`, parentheses, `pi`, `e`, `sqrt abs floor ceil round sin cos tan ln log exp`), `read_file` and `list_directory`, both inside a directory given on the command line. A path that is absolute, names a drive or has a `..` part is refused; a symbolic link inside the directory that points outside it is followed, so keep links out of what you share.

```
obc -src mcp_server_demo.obs -lib gen_collect,json,net,net_server,cipher,misc,mcp -dest mcp_server_demo.obe
```

Claude Desktop reads `claude_desktop_config.json` (`~/Library/Application Support/Claude/` on macOS, `%APPDATA%\Claude\` on Windows); Claude Code reads `.mcp.json` in the project, or `~/.claude.json`. The entry is the same:

```json
{
  "mcpServers": {
    "objeck-demo": {
      "command": "/opt/objeck/bin/obr",
      "args": ["/opt/objeck/examples/mcp_server_demo.obe", "/home/me/shared"],
      "env": { "OBJECK_LIB_PATH": "/opt/objeck/lib" }
    }
  }
}
```

On Windows write the paths with doubled backslashes (`"C:\\Program Files\\Objeck\\bin\\obr.exe"`). Claude Code can also add it from a shell, and can connect to the HTTP form (`obr mcp_server_demo.obe <dir> --http 8080`):

```
claude mcp add objeck-demo -- /opt/objeck/bin/obr /opt/objeck/examples/mcp_server_demo.obe /home/me/shared
claude mcp add --transport http objeck-demo http://127.0.0.1:8080/mcp
```

**Verified with Claude Code, not with Claude Desktop.** On 2026-11-24 Claude Code connected to the demo server over stdio on Windows x64, negotiated revision 2025-11-25, listed the tools and called `calculate`. The Claude Desktop configuration shape comes from its documentation and was not run; the server is also tested against a scripted client that speaks the specification's messages (below).

### A client

`McpClient` initializes a connection, lists and calls tools, reads resources and gets prompts. A request that fails returns `Nil` (or `false`) and `GetError()` and `GetErrorCode()` say why; a tool that ran and failed is a `McpToolResult` whose `IsError()` is true.

```ruby
use API.MCP, Collection, Data.JSON;

class Tour {
  function : Main(args : String[]) ~ Nil {
    server := McpService->New("tour", "1.0.0");
    server->AddTool("add", "Add two integers",
      "{\"type\":\"object\",\"properties\":{\"a\":{\"type\":\"integer\"},\"b\":{\"type\":\"integer\"}},\"required\":[\"a\",\"b\"]}",
      Func2Ref->New(Tour->Add(McpCall) ~ McpToolResult)<McpCall, McpToolResult>);
    server->AddResource(McpResource->New("notes://readme", "readme", "The readme", "text/plain", "Hello from a resource"));

    # a client of a server in the same process
    client := McpClient->InProcess(server);
    if(client->Initialize("tour-client", "1.0")) {
      "server: {$client->GetServerName()} {$client->GetServerVersion()}, revision {$client->GetProtocolVersion()}"->PrintLine();

      tools := client->ListTools();
      each(tool in tools) {
        "tool: {$tool->GetName()} - {$tool->GetDescription()}"->PrintLine();
      };

      result := client->CallTool("add", "{\"a\":2,\"b\":3}");
      "add(2, 3) = {$result->GetText()}"->PrintLine();

      # a request the server refuses is Nil with a reason; a tool that failed is a result
      missing := client->CallTool("add", "{\"a\":2}");
      if(missing->IsError()) {
        "add(2) failed: {$missing->GetText()}"->PrintLine();
      };
      if(client->CallTool("nope", "{}") = Nil) {
        "nope: {$client->GetError()} (code {$client->GetErrorCode()})"->PrintLine();
      };

      contents := client->ReadResource("notes://readme");
      "resource: {$contents->Get(0)->GetText()}"->PrintLine();
    };
  }

  function : Add(call : McpCall) ~ McpToolResult {
    sum := call->GetInt("a", 0) + call->GetInt("b", 0);
    return McpToolResult->Text("{$sum}");
  }
}
```

```
server: tour 1.0.0, revision 2025-11-25
tool: add - Add two integers
add(2, 3) = 5
add(2) failed: Invalid arguments for add: missing required argument 'b'
nope: Unknown tool: nope (code -32602)
resource: Hello from a resource
```

A client reaches a server through a transport:

| Constructor | Reaches |
|---|---|
| `McpClient->Http(url)` | a server over streamable HTTP; sends `Accept`, `MCP-Protocol-Version` and `Mcp-Session-Id`, reads a JSON reply or a server-sent event stream, and ends the session on `Close()` |
| `McpClient->InProcess(service)` | a `McpService` in the same process (tests, embedding); the server's notifications are kept by `McpLoopback->GetNotifications()` |
| `McpClient->Command(command)` | a server program started by the shell **once per request**: the handshake and the request go in on standard input, the reply is read from standard output |
| `McpClient->New(transport)` | any subclass of `McpTransport` |

The runtime has no process with pipes (`Runtime->Command` and `CommandOutput` run to the end), so the `Command` transport cannot keep a conversation open. It suits servers whose tools do not depend on earlier requests, and it cannot carry progress or cancellation; for a long-lived session use HTTP. `Initialize` checks that the revision the server chose is one this library speaks, and `Cancel(request_id, reason)` sends `notifications/cancelled` for a request another thread is waiting on.

### Giving a server's tools to Claude, OpenAI and Gemini

`McpToolBridge` lists a connected server's tools and turns them into what each AI client takes:

```ruby
use API.MCP, Collection, Data.JSON;

class HttpTour {
  @server : static : McpService;
  function : Server() ~ McpService { return @server; }

  function : Main(args : String[]) ~ Nil {
    @server := McpService->New("http-tour", "1.0.0");
    @server->AddTool("add", "Add two integers",
      "{\"type\":\"object\",\"properties\":{\"a\":{\"type\":\"integer\"},\"b\":{\"type\":\"integer\"}},\"required\":[\"a\",\"b\"]}",
      Func2Ref->New(HttpTour->Add(McpCall) ~ McpToolResult)<McpCall, McpToolResult>);

    # serve HTTP on 127.0.0.1, on a port the system chooses
    thread := System.Concurrency.Thread->New() {
      New() { Parent() }
      method : public : Run(arg : System.Base) ~ Nil {
        HttpTour->Server()->ServeHttp(0);
      }
    };
    thread->Execute(Nil);
    while(@server->GetBoundPort() = 0) {
      System.Concurrency.Thread->Sleep(10);
    };
    url := "http://127.0.0.1:{$@server->GetBoundPort()}/mcp";

    client := McpClient->Http(url);
    client->Initialize("http-client", "1.0");

    # the server's tools as the AI clients take them
    bridge := McpToolBridge->New(client);
    "Anthropic: {$bridge->ToAnthropicJson()->ToString()}"->PrintLine();
    "OpenAI:    {$bridge->ToOpenAiJson()->ToString()}"->PrintLine();
    answer := bridge->Run("add", JsonParser->TextToElement("{\"a\":40,\"b\":2}"));
    "a model asked for add(40, 2): {$answer}"->PrintLine();

    client->Close();
    Web.HTTP.Server.WebServer->Stop();
  }

  function : Add(call : McpCall) ~ McpToolResult {
    sum := call->GetInt("a", 0) + call->GetInt("b", 0);
    return McpToolResult->Text("{$sum}");
  }
}
```

```
Anthropic: [{"description":"Add two integers","input_schema":{"properties":{"a":{"type":"integer"},"b":{"type":"integer"}},"required":["a","b"],"type":"object"},"name":"add"}]
OpenAI:    [{"function":{"description":"Add two integers","name":"add","parameters":{"properties":{"a":{"type":"integer"},"b":{"type":"integer"}},"required":["a","b"],"type":"object"}},"type":"function"}]
a model asked for add(40, 2): 42
```

* **Claude.** `API.Anthropic.Tool->New(name, description, schema)` takes the three parts straight from a `McpTool`, and `bridge->Run(name, input)` runs the call the model made and returns the text to give back with `AddToolResult` (`"error: ..."` for a tool that failed, so pass `text->StartsWith("error: ")` as the error flag):

  ```ruby
  use API.MCP, API.Anthropic, Collection, Data.JSON;

  # compile with -lib anthropic,gen_collect,json,net,net_server,cipher,misc,mcp
  client := McpClient->Http("http://127.0.0.1:8080/mcp");
  client->Initialize("agent", "1.0");
  bridge := McpToolBridge->New(client);

  tools := Vector->New()<Tool>;
  each(tool in bridge->GetTools()) {
    tools->AddBack(Tool->New(tool->GetName(), tool->GetDescription(), tool->GetInputSchema()));
  };

  chat := Chat->New("claude-sonnet-5-5", Nil);
  reply := chat->SendReply("What is (2 + 3) * 4? Use the calculator.", tools);
  while(reply <> Nil & reply->HasToolUses()) {
    uses := reply->GetToolUses();
    each(i : uses) {
      call := uses->Get(i);
      text := bridge->Run(call->GetName(), call->GetInput());
      chat->AddToolResult(call->GetId(), text, text->StartsWith("error: "));
    };
    reply := chat->SendReply(Nil, tools);
  };
  reply->GetText()->PrintLine();
  ```

* **Gemini.** `GetFunctions()` returns the map of tool name to a `FunctionType` and the function that calls the tool, which is what `Model->GenerateContent(model, content, funcs, key)` takes. Add `use Data.JSON.Scheme;` for the `FunctionType` name:

  ```ruby
  use API.MCP, API.Google.Gemini, Collection, Data.JSON, Data.JSON.Scheme;

  # compile with -lib gemini,gen_collect,json,net,net_server,cipher,misc,mcp
  bridge := McpToolBridge->New(client);
  funcs := bridge->GetFunctions();
  content := Content->New("user")->AddPart(TextPart->New("What is (2 + 3) * 4? Use the calculator."));
  candidates := Model->GenerateContent("models/gemini-2.0-flash", content, funcs, EndPoint->GetApiKey());
  ```

* **OpenAI.** `ToOpenAiJson()` is the `tools` array of function definitions, `ToFunctionType(tool)` builds the `FunctionType` the OpenAI classes take, and `Run` answers a call the model asked for. For Chat Completions, `Completion->CompleteWithTools(model, messages, bridge->ToOpenAiJson(), max_tokens, token)` sends the tools array and `Choice->GetToolCalls()` returns the calls to pass to `Run` (the loop is in [AI.md](../AI.md#openai)). The functions in `GetFunctions()` are the `Func2Ref<JsonElement, JsonElement>` the OpenAI conversation's `AddFunction` wraps (pass `pair->GetSecond()->Get()`).

A function from `GetFunctions()` returns `{"result": text}`, or `{"error": text}` when the tool failed or the call could not be made. `ToFunctionType` maps `string`, `number`, `integer`, `boolean` and `object` properties and an `array` property to its item type as an array; a property with another or no `type` becomes a string.

On 2026-11-25 the Claude and OpenAI hand-offs were run live. A program connected to the demo server over HTTP, passed the bridge's tools to the model, the model called `calculate` with `17 + 25`, `bridge->Run` returned `42`, and the model answered `42` in two turns. Claude: `claude-haiku-5-5`, `claude-sonnet-5-5`, `claude-opus-5-5` and `claude-fable-5-2` through `Chat->SendReply` (the same 1,404 input and 55 output tokens each time); the MCP `inputSchema` went into `Tool->New` unchanged. OpenAI: `gpt-4.1-mini`, `gpt-5.4-mini` and `gpt-5.5` through `Completion->CompleteWithTools`, `Choice->GetToolCalls`, `Choice->GetMessageJson` and `Completion->ToolResultMessage`. Two OpenAI models could not take part: `gpt-5.3-codex` is served only by the Responses API, and `gpt-5.6-sol` rejects function tools on Chat Completions unless `reasoning_effort` is `none`; this library has no Responses API client. The Gemini hand-off was not run live. The repository's tests never call the live APIs; these runs were manual.

### A function is a tool: `@tool`

Put a line that starts with `@tool` in a function's doc comment and the compiler writes the tool for you: the name, the description, the JSON Schema, the hints and the code that reads the arguments. The class gets a static function `McpTools(service)` that registers all of its tools.

```ruby
use API.MCP;

enum Units { METRIC, IMPERIAL }

class Weather {
  #~
  Forecast for a city.
  @tool
  @readonly
  @param city the city
  @param units units of the temperature
  @param days how many days, 1 to 7
  ~#
  function : Forecast(city : String, units : Units, days : Int := 3) ~ String { ... }

  function : Main(args : String[]) ~ Nil {
    server := McpService->New("weather", "1.0");
    Weather->McpTools(server);
    server->ServeStdio();
  }
}
```

(`days` is optional because it has a default; `units` is an enum, so the model must give one of `METRIC`, `IMPERIAL`.) Compile with `-lib ...,mcp`; the file must `use API.MCP`.

* **The tag.** A `#~ ... ~#` comment with a line that starts with `@tool` (after blanks), directly before a `function`, makes it a tool. `@tool name` sets the tool's name; without one it is the function's name in snake_case (`ReadFile` is `read_file`). A comment that mentions `@tool` mid-line, or `@tools`, or `@tool_name`, is documentation. Only the last comment before the function counts. The same text in a string, in a `//`, `#` or `/* */` comment is nothing.
* **Description.** The comment's text before its first `@` line, on one line. `@title text` sets a title. `@param name text` describes a parameter (a name the function lacks is an error).
* **Hints.** `@readonly`, `@destructive`, `@idempotent`, `@openworld` set the MCP annotations; `@readonly` with `@destructive` is an error.
* **Parameters.** `Int`, `Float`, `Bool`, `Byte`, `Char`, `String`, `JsonElement`, an enum (a string of its item names), a record (an object with its fields, which may themselves be records, enums, arrays), a one-dimensional array, `Vector<T>` or `Map<String,T>` of those. A parameter with a default is optional; the default must be a number, `Bool`, `Char`, `String` or `Nil` literal. A parameter of type `McpCall` is not part of the schema: it gives the function the call (`IsCancelled()`, `ReportProgress`).
* **Result.** `String`, `Int`, `Float`, `Bool`, nothing, `JsonElement` (the structured result, declared as an object) or `McpToolResult` (sent as it is). A raised error, or a `Nil` String, is an error result the model can read.
* **Diagnostics.** A parameter or return type that cannot be sent (a function type, a class that is not a record, an array of two dimensions, a `ref` parameter), a default that is not a literal, a tag before a method, constructor, class, enum or variable, `@param` of nothing, two tools with one name, and a file that does not `use API.MCP` are compile errors that name the tool and point at the function.
* **Limits.** Enums and records must be declared in the sources being compiled, in any file and in any order (the compiler does not read them from a library). An enum cannot be an optional parameter (its default would not be a literal) or an array element. `obr --mcp prog.obe` serves the program's tools, resources and prompts with no further code (see below).

### A function is a resource or a prompt: `@resource`, `@prompt`

The same doc comment makes the other two MCP server primitives. `@resource <uri>` makes a function a resource (read-only data the client or model can fetch by URI); `@prompt [name]` makes it a prompt template (a named, parameterised message the user picks). `McpTools(service)` registers all three kinds, so one registration function serves tools, resources and prompts.

```
use API.MCP;

class Docs {
  #~
  The project readme.
  @resource notes://readme
  @mime text/markdown
  ~#
  function : Readme() ~ String { return "# Readme"; }

  #~
  The forecast for a city.
  @resource weather://{city}/forecast/{days}
  ~#
  function : Forecast(city : String, days : Int) ~ String { return "{$days} days in {$city}"; }

  #~
  Review some code.
  @prompt
  @param language the language of the code
  @param focus what to look for
  ~#
  function : CodeReview(language : String, focus : String := "bugs") ~ String {
    return "Review the {$language} code for {$focus}.";
  }
}
```

* **Resources.** The URI (a fixed one, or a template with `{name}` variables) comes after `@resource`; a fixed URI is listed by `resources/list`, a template by `resources/templates/list`. Every variable is a parameter of the same name (`String`, `Int`, `Float`, `Bool` or `Char`, read from the URI text; `McpUriTemplate->Match` does the matching) and every parameter is a variable; a default is an error. `@mime type` sets the media type (`text/plain` by default). A `String` result is the text of the resource, a `McpContents` is sent as it is (use it for a blob or a different URI); a `Nil` result, or a URI that does not fit the template, reads nothing. The name is the function's name in snake_case. Values are not percent-decoded.
* **Prompts.** The arguments of a prompt are text on the wire, so a parameter is a `String`, `Int`, `Float`, `Bool` or `Char` (converted from the text), and a parameter with a default is optional; `@param` documents it. A `String` result is one user message, a `McpPromptResult` (several messages, roles) is sent as it is; a `McpCall` parameter is not an argument. `@prompt name` names it, otherwise snake_case.
* **Diagnostics.** A function is exactly one of `@tool`, `@resource`, `@prompt`; the hints (`@readonly`...) belong to tools and `@mime` to resources; a URI variable with no parameter, a parameter that is not a variable, an unsupported parameter or return type, and two resources with one URI are compile errors with the function's position.

### Serving a program: `obr --mcp`, `obr --mcp-http`

```
obr --mcp tools.obe                       # standard input and output: Claude Desktop, Claude Code, `mcp_stubs --cmd`
obr --mcp-http=8080 tools.obe             # streamable HTTP on 127.0.0.1:8080
obr --mcp-http=0 --mcp-http-host=0.0.0.0 tools.obe     # any free port on every interface; the port goes to standard error
```

The flags come before the program, like every VM option. The VM does not run `Main`: it starts the program at `McpMain(args : String[])`, which the compiler writes into a class with a tagged function (`McpService->New(<class>, "1.0")`, `Class->McpTools(server)`, `McpServe->Run(server, args)`), after adding the transport (`stdio`, or `http`, the address and the port) to the front of the program's arguments. A program that wants one server for several classes writes its own `McpMain` (it wins over the generated one) that calls each `McpTools` and then `McpServe->Run(server, args)`; two classes with a generated `McpMain` and none of the program's own is an error that says so, and so is a program with nothing to serve.

* **Standard output is the protocol** under `--mcp`: a tool that prints there corrupts it. Log with `ErrorLine` (standard error); the HTTP port announcement goes there too.
* **End.** stdio ends when standard input ends (exit 0, after the running calls); HTTP stops when the process is terminated.
* **Progress and cancellation** are the `McpCall` of the tool: `call->ReportProgress(done, total)` becomes `notifications/progress`, and a client's `notifications/cancelled` sets `call->IsCancelled()`.
* **Nothing changes without the flags**: the loader reads one environment variable the flags set, and a run without them takes no other path (no JIT, GC or interpreter change; `run_vm_flag_tests.py` and the regression suites pass unchanged).

### Declaring a tool from its types (the library form)

`@tool` (above) is the way to write a tool. `McpToolSpec` is the same thing as a library: it needs no compiler support, and it is what the compiler's output is checked against. A function's name and parameter types already say what its JSON Schema says; `McpToolSpec` writes them once, as text, and derives the schema:

```ruby
McpToolSpec->New("forecast", "Forecast for a city.")
  ->Add("city", "String", "the city")                    # required
  ->AddOptional("days", "Int", "how many days, 1 to 7")  # optional: not in "required"
  ->AddOptional("units", "enum(metric,imperial)", Nil)   # a string with a list of values
  ->ReadOnly()                                           # readOnlyHint: the tool changes nothing
  ->AddTo(server, Func2Ref->New(Weather->Forecast(McpCall) ~ McpToolResult)<McpCall, McpToolResult>);
```

| Objeck type | JSON Schema |
|---|---|
| `Int` / `Float` / `Bool` / `String` | `integer` / `number` / `boolean` / `string` |
| `Char` | a string of exactly one character |
| `Byte` | an integer 0 to 255 (the server does not check the bounds) |
| `T[]`, `T[,]`, `Vector<T>` | an array of `T`, nested once per dimension (a collection of numbers holds `IntRef` or `FloatRef`; both spellings are read) |
| `Map<String,T>` | an object whose values are `T` |
| `enum(a,b,c)` | a string, one of `a`, `b`, `c` (checked on every call) |
| `Json`, `JsonElement` | any value |
| any other class or record | a bare `{"type":"object"}`; `McpToolSpec->Known(type)` is false, because the mapping cannot see the fields |

The derived schema equals, as JSON (`McpToolSpec->Same`), the hand-written schema of every tool in the repository's own servers and examples; `mcp_declare_test` re-declares fifteen of them and fails on the first difference. With `McpToolSpec` the function still reads its arguments with `McpCall`; with `@tool` the compiler writes that code. `AddSchema(name, json, description, required)` adds a parameter whose schema is given as JSON text, which is how the compiler's output states a record or an enum.

* **Effects.** `ReadOnly()`, `Destructive()`, `Idempotent()` and `OpenWorld()` set the MCP `annotations` (`readOnlyHint`, `destructiveHint`, `idempotentHint`, `openWorldHint`), which `tools/list` carries and `McpClient->ListTools` reads back as `McpTool->GetReadOnlyHint()` and so on (`HasReadOnlyHint()` tells "false" from "not stated"). A tool with no hints lists exactly as before. `Returns(schema)` declares the shape of a structured result (`outputSchema`).
* **Checks.** Besides `required` and `type`, a call is checked against a property's `enum` and an array's `items` type; the reason names the allowed values.
* **Counters.** `server->EnableStats(true)`, then `server->GetStats()`: calls, errors, rejected arguments, cancellations and handler milliseconds per tool. Off by default; while off a call pays one Bool test.

### A typed client: mcp_stubs

`tools/mcp/mcp_stubs.obs` connects to a server, reads `tools/list` and writes an Objeck class with one typed method per tool:

```
obc -src mcp_stubs.obs -lib gen_collect,json,net,net_server,cipher,misc,mcp -dest mcp_stubs.obe
obr mcp_stubs.obe --cmd "obr weather_server.obe --stdio" --class WeatherClient --out WeatherClient.obs
obr mcp_stubs.obe --http http://127.0.0.1:8080/mcp --class WeatherClient
```

```ruby
weather := WeatherClient->New(McpClient->Command("obr weather_server.obe --stdio"));
text := weather->Forecast("Denver");                 # the optional parameters left out
text := weather->Forecast("Denver", 5, "imperial");  # a wrong type here is a compile error
if(text = Nil) { weather->GetError()->ErrorLine(); }; # isError, or the server could not be reached
hits := weather->LookupJson("Denver");               # a tool with an outputSchema: its structuredContent
```

Build-time drift check: `--save-manifest m.json` keeps the server's `tools/list`; `--manifest m.json` regenerates the same client with no server; `--check WeatherClient.obs` writes nothing and exits 1, naming the methods removed (`-`), added or changed (`+`), when the checked-in client no longer matches. Run it before `obc` so a changed server is a failed build, not a failed call.

`integer` becomes `Int`, `number` `Float`, `boolean` `Bool`, `string` `String` (a string enum lists its values in the doc comment), an array of those `Int[]`, `Float[]`, `Bool[]` or `String[]`, and anything else (an object, a nested array, a schema with no type) `JsonElement`. A JSON object does not keep the order its names were written in, so required parameters come first, in the order of the `required` array, then the optional ones by name. A tool whose name or an argument's name has characters a method cannot carry is skipped with a comment. The output has no timestamp, so it can be checked in and compared; `programs/regression/mcp_stubs/WeatherClient.golden` is that file for the test server.

### Giving functions and servers to an agent

The library `mcp_agent` (`-lib ...,mcp,agent,mcp_agent`) makes `API.Agent` tools without re-describing anything:

```ruby
registry := ToolRegistry->New();
registry->Add(McpAgentTools->FromSpec(spec, handler));    # a declared function, run in this process
McpAgentTools->AddAll(registry, McpClient->Http(url));    # every tool of a server, with its hints
agent := Agent->New(backend, registry);
agent->SetApprover(approver)->SetApproveUnlessReadOnly(true);
```

The agent reads the hints. A tool whose `destructiveHint` is true needs the approver, like one marked `SetDangerous`, and with no approver the call is refused. `SetApproveUnlessReadOnly(true)` asks about every tool not known to be read-only: the safe rule for tools from a server you do not control, because a missing hint says nothing. A tool with no hints behaves as before. The agent's cancellation token (and its deadline) reaches a function run by `FromSpec` as `McpCall->IsCancelled()`, polled every 2 ms, so a handler that checks it stops promptly; one that does not runs to its end. See `programs/deploy/mcp_declare_40.obs`, `mcp_stub_41.obs` and `mcp_agent_42.obs`, and the walkthrough `docs/MCP_WALKTHROUGH.md`.

### API

| Class | Use |
|---|---|
| `McpService` | the server: `AddTool`, `AddResource`, `AddPrompt`, `SetInstructions`, `SetTitle`, `ServeStdio`, `ServeHttp`, `GetBoundPort`, `SetSessions`, `AllowOrigin`, `SetMaxWorkers`, and `Handle(text, sink)` or `Handle(text, sink, scope)` to answer one message for any other transport |
| `McpTool`, `McpResource`, `McpPrompt` | what a server offers (and, without a handler, what a client reads back from a list) |
| `McpCall` | what a handler receives: the arguments, `IsCancelled`, `ReportProgress` |
| `McpToolResult`, `McpContents`, `McpPromptResult` | what a handler returns |
| `McpClient` | `Initialize`, `Ping`, `ListTools`, `CallTool`, `Cancel`, `ListResources`, `ListResourceTemplates`, `ReadResource`, `ListPrompts`, `GetPrompt`, `Close`, `GetError`, `GetErrorCode` |
| `McpToolBridge` | `GetTools`, `ToAnthropicJson`, `ToOpenAiJson`, `ToFunctionType`, `GetFunctions`, `Run` |
| `McpToolSpec` | declare a tool from its types: `Add`, `AddOptional`, `ReadOnly`, `Destructive`, `Idempotent`, `OpenWorld`, `Returns`, `GetSchema`, `ToTool`, `AddTo`; `SchemaFor`, `Known`, `Same` |
| `McpStubGenerator` | `Generate(server, version, tools, class)`: the source of a typed client (the `mcp_stubs` tool is a command line around it) |
| `McpAgentTools` (library `mcp_agent`) | `FromSpec`, `FromTool`, `FromClient`, `AddAll`: agent tools from a declared function or a server |
| `McpTransport`, `McpLoopback`, `McpHttpTransport`, `McpCommandTransport` | how a client reaches a server |
| `McpSink`, `McpCollector`, `McpStdoutSink`, `McpStdio` | where a server's own messages go, and how standard input is read |
| `McpProtocol` | `Version()`, `IsSupported(version)` and the error codes |

### Limits and caveats

* The JSON parser is lenient: it reads a bare word as a string and ignores what follows the first value. The server checks that a message that opens with `{` or `[` ends with the matching closer, that the opening object, array or string closes on the last character (two messages on one line, or one with something after it, are a parse error, not one answer and one silence) and that a root string is quoted, which catches the common damage (truncation, trailing text), but a malformed message inside well-placed braces can be accepted.
* Tool input is validated for `required` names, the `type` of each property, a property's `enum`, a number's `minimum` and `maximum`, a string's `minLength` and `maxLength`, and an array's `items` (their type and the same limits, through nested arrays), which is everything the schemas the library derives advertise: a `Byte` given 256 or a `Char` given two characters is an `isError` result and the handler does not run. `exclusiveMinimum`, `multipleOf`, patterns, array lengths, nested objects and `$ref` are not checked.
* On stdio at most `SetMaxWorkers(n)` tool calls run at once (16 unless set, 1 to 256); the rest queue, without a cap on the queue. Over HTTP a call runs on the web server's thread for its request.
* `McpClient` refuses a reply that is not JSON-RPC 2.0 or whose id is not the request's (`GetErrorCode()` is `-32600`); an error with a null id is taken as the server's answer.
* Standard input is read a byte at a time (a megabyte takes roughly half a second on Linux), which is what makes a line of any length work: the VM's line read keeps 8,192 characters.
* A server over stdio ends when its input does; there is no `shutdown`, as the specification has none.
* Tested without Claude Desktop, Claude Code or the network: see Tests.

### Tests

* `mcp_core_test` (regression suite): every method; version negotiation; the JSON-RPC errors; a tool that fails, raises, returns nothing, or gets bad arguments; resources, templates and prompts; cancellation and progress; a message of a megabyte; the bridge; and the same client over the HTTP transport on `127.0.0.1` port 0, including the session, `Origin`, protocol-version and method checks.
* `mcp_stdio_test.py` (its own CI step; `python mcp_stdio_test.py <bin_dir>`): a scripted Python client drives a server process over stdio: the handshake, every method, malformed JSON, an unknown method, bad parameters, a failing and a raising tool, blank lines, CRLF and non-ASCII text, progress and a request cancelled in flight (no reply), megabyte messages in both directions and a line over the limit, that standard output holds nothing but JSON, the end of input, and the Objeck client against the same server; then two messages on one line, an id reused while its call is at work, a hundred calls each cancelled on the next line, a call cancelled while its arguments are checked and then rejected by the check (no reply), more calls at once than workers, 1,500 calls without a handle (Windows) or address space (Linux) kept per call, a UTF-16 line cut inside a character (POSIX), and over HTTP two sessions with the same request id and a refused `initialize`. It also passes on Windows.
* Both fail when the code is broken: a server that answers a cancelled request, or uses the wrong code for an unknown method, fails both.
* `mcp_declare_test` (regression suite): every row of the type mapping table, the oracle (the derived schema equals the hand-written schema of fifteen tools from the repository's servers and examples), the annotations on the wire and at the client, calls with an optional argument, an array and a structured result, the error cases (missing required argument, wrong type, a fraction for an integer, a value outside an enum, a wrong array item, an unknown tool, a failing tool), the counters, and the generator's names and types.
* `mcp_agent_test` (regression suite): a declared function and a server's tools as agent tools against a scripted model; the approval rules for destructive, read-only and unmarked tools (and that a refused call never runs its handler); failures; the cancel token reaching the handler.
* `mcp_stubs_test.py` (its own CI step; `python mcp_stubs_test.py <bin_dir>`): compiles a typed server and the generator, generates a stub over stdio, compares it with the golden file, compiles it with a program that calls it, runs that in two modes, checks that a wrong argument type is a compile error, and the command line's errors.
* `mcp_tool_tags_test` (regression suite): `@tool` functions and nothing else: the schema the compiler derives equals the hand-written schema of 15 tools from the repository's servers and examples, every row of the type mapping table, descriptions, names, hints, `@param` text, calls with defaults, every return type, a `McpCall` parameter, and the checks the server makes from the derived schema.
* `mcp_tool_tags_diag_test.py` (its own CI step): each compile error of the tag (with its line), what must be left alone (the tag in a string or another comment, mid-line, `@tools`, `@tool_name`), CRLF, a megabyte of comment, non-ASCII text.
* Not covered by tests: a live model through the typed stub (run by hand: `tools/mcp/mcp_live_stub.obs`).
