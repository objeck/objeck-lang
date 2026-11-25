# MCP canvas: two painters, one canvas

`mcp_canvas.obs` is a visual demo of Objeck's MCP support (`-lib mcp`) on SDL2.

The program hosts an MCP server **in the same process**. Its tools *are* a 40x24 tile
canvas: `look`, `set_color`, `move`, `plot`, `line`, `fill_rect`, `circle`, `stamp`.
Two painters connect with `McpClient->InProcess` and paint **at the same time**, each on
its own thread, on the one shared canvas (every tool call runs under a lock).

The window shows the canvas, each painter's cursor, a panel of the `tools/call`
requests as they happen (tool, arguments, result), and sparks that fly from a
painter's card to the tiles its call just changed. SDL stays on the main thread; the
painters only touch the canvas through the MCP tools.

## Run it

Offline, no keys and no network (two scripted painters through the same MCP client):

    obc -src mcp_canvas.obs -lib gen_collect,json,net,net_server,cipher,misc,mcp,anthropic,openai,sdl2 -dest mcp_canvas.obe
    obr mcp_canvas.obe --demo

Live: Claude (`claude-sonnet-5-5`, `API.Anthropic`) paints a sunset while an OpenAI
model (`gpt-5.4-mini`, `API.OpenAI.Chat.Completion->CompleteWithTools`) adds a boat,
birds and an island. Both get the tools from `McpToolBridge` (`ToAnthropicJson` /
`ToOpenAiJson`, `bridge->Run`):

    obr mcp_canvas.obe                       # keys: ANTHROPIC_API_KEY / OPENAI_API_KEY, or
                                             # anthropic_api_key.dat / openai_api_key.dat here
    obr mcp_canvas.obe --claude claude-opus-5-5 --openai gpt-5.5 --turns 8

A painter without a key falls back to the scripted painter. A live run costs a few cents
(about 20k Claude tokens). `run_mcp_canvas.cmd` builds and runs it on Windows.

Headless (no window, software renderer), for screenshots and smoke tests:

    obr mcp_canvas.obe --demo --shot out.png      # writes out_mid.png and out.png
    obr mcp_canvas.obe --selftest                 # 18 checks of the tools, no SDL, no network

Other options: `--prompt-a TEXT`, `--prompt-b TEXT`, `--shot-after N`, `--seconds N`, `--verbose`
(print every call). ESC or closing the window quits.

## Notes

* The second live painter waits until the first has made 12 calls, so the two meet on a
  half-painted canvas. They do not always cooperate: a model may put its boat above the
  horizon, or paint a reflection through the other's sprite. That is the demo.
* Strings compare with `->Equals`; `=` on two strings compares references.
* Both LLM client libraries have a `Tool` class, so `Tool` is used only for Anthropic here;
  OpenAI takes the bridge's JSON directly. OpenAI's `Completion` exposes no usage counts, so
  only Claude's tokens are reported.
