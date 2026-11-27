# Agents

Objeck is an agentic language and runtime. A function is a tool, a program is an MCP server, and an agent is a loop with limits. Each program below was compiled and run with v{{version}}; the output shown is what it printed. None needs a key or a network.

## Declare a tool

A doc comment with `@tool` is the whole declaration. The description is the comment, the JSON Schema comes from the parameter types, and `@readonly` becomes the MCP `readOnlyHint`. A call with a bad argument is refused before the function runs.

{{example:tool}}

## Serve it

A program with `@tool`, `@resource` or `@prompt` functions needs no server code. `obr --mcp` starts it as an MCP server on standard input and output, which is what Claude Code and Claude Desktop launch; `obr --mcp-http=8080` serves the same program over HTTP.

{{example:serve}}

Here is a session with it. Each `>` is what a client sent and each `<` the reply, played against the real server by the same check that keeps this page honest:

{{mcp:serve}}

To use it from Claude Code, give the client the command:

```json
{"mcpServers":{"notes":{"command":"obr","args":["--mcp","/path/to/serve.obe"]}}}
```

Standard output carries the protocol, so a tool logs with `ErrorLine()`, which goes to standard error.

## Call a server through a typed client

`mcp_stubs` asks a server for its tools and writes a class with one typed method per tool. A wrong argument is a compile error, and `--check` fails the build when the server's tools have changed since the client was generated. The generated class (about 100 lines) is in the sample's source; the page shows the part you write.

{{example:client}}

## Run an agent

`McpAgentTools` hands a server's tools to an `Agent`. The effect hints decide who approves: the read-only `add` ran without a question, the destructive `delete_file` asked, and the approver said no, so its function never ran. A budget ends a run that would not stop. The model here is a `ScriptedBackend`, which replays fixed replies; swap in `AnthropicBackend`, `OpenAIBackend`, `GeminiBackend` or `OllamaBackend` and nothing else changes.

{{example:agent}}

Every run says why it ended in one word: `completed`, `cancelled`, `deadline`, `budget_steps`, `budget_tool_calls`, `budget_tokens` and so on. An `AgentCancel` token that any thread can trip stops a run between steps, and a tool in a bounded mode is abandoned at its time limit.

## Limits for a block

`AgentScope` puts one set of limits, one cancel switch and one trace around every agent run inside a block, so a program does not hand a budget to each agent it makes. The limits are totals: the second question gets only what the first one left, and a run with nothing left is not started, so the model is not called at all.

{{example:scope}}

## Local models, no key

`OllamaBackend` talks to a model on your own machine at `localhost:11434`. There is no key and nothing leaves the box. The program below runs the scripted model by default and a local one with `obr local.obe ollama`; the agent, the tool and the budget are the same code.

{{example:local}}

The Ollama branch needs a running Ollama and a model that supports tools. The default run, shown above, needs neither.

## Built to run agents

- **Collector-cooperative inference.** An ONNX session running on a thread does not hold up the garbage collector, and a session can be shared by threads. [Architecture](../architecture.md) and [performance](../performance.md) have the measurements.
- **Actors with supervision.** `System.Actor` gives cooperating agents a mailbox each, links and monitors, and a `Supervisor` that restarts the ones that fail.
- **Telemetry.** `System.Telemetry` records inference latency and predicts it from past runs, so a run can be accounted for.
- **Generational collector and a JIT.** The runtime under the agents is the same one that runs the [benchmarks](../performance.md).

## Reference

- [MCP walkthrough](../MCP_WALKTHROUGH.md): declare, serve, consume, hand to an agent, with a real model
- [MCP reference](../ai_sections/mcp.md): what the server and client support, and what they do not
- [Agent reference](../ai_sections/agent.md) and [run control](../ai_sections/agent_control.md): the loop, the gates, budgets, cancel, scope
- [Libraries](../web/libraries.html): `agent`, `mcp`, `mcp_agent`, `actor`, `telemetry`, `llm_util`, `rag`
- [AI and ML guide](../web/ai_guide.html)
