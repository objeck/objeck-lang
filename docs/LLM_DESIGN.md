# API.LLM design note

`API.LLM` (`core/compiler/lib_src/llm.obs`) is one chat interface over Anthropic, OpenAI, Gemini and Ollama, so an agent loop is written once. The user guide is [AI.md](AI.md#one-chat-interface-apillm); this note records why it is built the way it is.

## The surface

```
interface Session    SetSystem, SetMaxTokens, SetTemperature,
                     Send(message, tools) -> Turn, AddToolResult(call id, tool name, content),
                     Stream(message, tools) -> TurnStream, SendJson(question, name, schema),
                     GetLastError, GetProvider, GetModel
interface TurnStream HasNext, Next, GetTurn, Close
class Turn           text, tool calls, stop reason (normalized + raw), token counts, model
class ToolSpec       name, description, JSON schema
class ToolInvocation id, name, arguments (JsonElement)
interface ToolHandler / class Agent   the loop: send, run tools, send results, until an answer
class LLM            Open("provider:model"), Anthropic/OpenAI/Gemini/Ollama(model)
```

One implementation per provider: `AnthropicSession`, `OpenAISession`, `GeminiSession`, `OllamaSession`. `Send(Nil, tools)` continues after tool results; a failed call returns `Nil` with the last error set and does not keep the message, the repository convention for the AI clients.

## Decisions

**The tool types are `ToolSpec` and `ToolInvocation`, not `Tool` and `ToolCall`.** A library's generic arguments are stored by short name and resolved at the use site. `API.Anthropic.Chat->SendReply` takes a `Vector<Tool>` and `API.Ollama.Reply->GetToolCalls` returns a `Vector<ToolCall>`; a class named `Tool` or `ToolCall` in `API.LLM` hijacks those and the call no longer type-checks. The same collision makes `Tool` ambiguous between Anthropic and Ollama in one program, which is the next decision.

**Anthropic is wrapped; the other three are spoken to directly.** `AnthropicSession` wraps `API.Anthropic.Chat`, which already has the round trip, the stream assembler and the forced-tool JSON. `API.Ollama.Chat` cannot be used alongside it: both take a `Vector<Tool>` and the compiler cannot tell the two `Tool`s apart when both libraries are loaded. `OllamaSession` therefore keeps the messages itself and posts `/api/chat` (the request is a dozen lines; `API.Ollama.EndPoint->GetHost()` is reused). OpenAI and Gemini cannot be wrapped at all: `API.OpenAI.Responses` does not parse `function_call` output items or send `function_call_output` input items (only the Assistants `Run` class round-trips tools), and Gemini's tool support dispatches callbacks itself (`BuildContentResponse`) rather than returning the call. Both sessions are written against the documented wire formats over `HttpsClient`.

**The session owns the conversation and sends it whole.** OpenAI Responses and Gemini are stateless as used here. The model's own output items (OpenAI) or content (Gemini) are replayed exactly as they came back, not rebuilt from the parsed text, so reasoning items, `call_id`s and Gemini's thought signatures survive. A failed `Send` pops the message it added; a failed continuation (`Send(Nil, ...)`) leaves the tool results in place so the caller can retry.

**Tool results are matched the way each provider matches them, from one call.** `AddToolResult(call id, tool name, content)` carries both because Anthropic and OpenAI match by id and Gemini and Ollama by name. Ollama has no ids and older Gemini replies have none, so one is made up (`ollama_call_N`, `gemini_call_N`); a made-up Gemini id is not echoed back, one the API sent is. Anthropic results for one turn share one user message and Gemini `functionResponse` parts for one turn share one content, both as those APIs require. A Gemini result that is not a JSON object is wrapped as `{"result": text}` because `functionResponse.response` must be an object.

**Stop reasons are normalized** to `end`, `tool_calls`, `length`, `filtered`, `other`, with the provider's word kept in `GetRawStopReason()`. Tool calls win over the raw reason: Gemini reports `STOP` for a turn that is a function call, and Ollama `stop`. A blocked Gemini prompt (no candidates, `promptFeedback.blockReason`) is an error rather than an empty turn.

**JSON output uses each provider's own mechanism** and is one-shot (history is context, the question and answer are not kept): a forced tool (Anthropic), `text.format` of type `json_schema` (OpenAI), `responseMimeType` plus `responseSchema` (Gemini), `format` as a schema (Ollama). OpenAI is sent `strict: false`: strict mode rejects any schema that does not list every property as required and forbid extras, which a neutral interface cannot assume. Gemini's schema dialect is a subset of JSON Schema; the schema is passed through unchanged.

**Tools are adapted per provider, from one `ToolSpec`.** Gemini rejects an object schema with empty `properties` (a tool with no arguments) and any keyword outside its OpenAPI subset, so its `parameters` is omitted when empty and the schema, for tools and for `responseSchema`, goes through `GeminiSchema` first: unsupported keywords are dropped, `oneOf` becomes `anyOf`, `const` a one-value `enum`, a type list a type plus `nullable`; property names are never mistaken for keywords. OpenAI tools are sent with `"strict": false` always, because strict mode wants `additionalProperties: false` and every property required. `AddToolError` marks a failed call where the provider can say so (Anthropic `is_error`, Gemini an `error` field) and elsewhere prefixes `Error: `. `Agent` answers an unknown tool or a `Nil` result with `AddToolError`, and, when it stops at its round limit, answers the calls that are still pending the same way, so the session is left in a state the next `Send` can use.

**A stream settles itself.** Whether the stream is read to its end, abandoned, closed early, or broken, the session ends up with either the whole exchange or neither message: the end of the stream (or `GetTurn()`) keeps it, `Close()` before the end and any failure take the user message back, as a failed `Send` does. A refused stream shows only its status line, so a 400, 401, 403 or 404 is repeated once without streaming to read the server's message.

**Streaming is SSE through `HttpsClient->StreamPost`, the only stream the library has.** OpenAI text arrives as `response.output_text.delta` events and the whole turn is read from the final `response.completed` object, so the streamed and non-streamed paths share one parser. Gemini chunks are merged into one content and parsed the same way; Gemini sends no end-of-stream event and a keep-alive connection would block the line reader forever, so the reader stops at the chunk that carries a `finishReason` or a blocked-prompt `promptFeedback`. The request also sends `Connection: close`, but that did not make the local mock close the connection, so it is not relied on. A part that carries a `thoughtSignature` is kept whole when the content is rebuilt. Ollama cannot stream here (plain HTTP, `HttpClient` has no stream), so `WholeStream` returns its finished reply as one piece, which the docs say.

**The Gemini key is sent in `x-goog-api-key`, not the URL** the Gemini client uses (`?key=`), so it does not appear in a URL or an error message. This is the documented alternative; it is the one deliberate departure from the existing client's convention.

**Configuration follows the clients.** Keys come from each client's `EndPoint->GetApiKey()` (Anthropic: env var or `anthropic_api_key.dat`; OpenAI and Gemini: `*_api_key.dat`; those two print a note on stderr when the file is missing, as before). `OpenAISession` falls back to `API.OpenAI.EndPoint->GetBaseUrl()`. `SetBaseUrl` and `SetCertificate` exist per session on OpenAI and Gemini so a mock or gateway can be used; Anthropic and Ollama keep their static endpoints.

## Tests

Nothing needs a key or a network beyond loopback.

- `llm_parse_test` (recorded bodies): every provider's body into a `Turn` (text, calls, stop reasons, tokens, error objects, malformed bodies), the request each session builds, what joins the conversation, `LLM->Open`, and `Agent` against a scripted session.
- `llm_http_test`: a local HTTPS server (Anthropic, OpenAI, Gemini) and a plain HTTP one (Ollama). Per provider: headers and body, plain reply, error object, tool round trip, JSON output, streaming (and a streamed tool call for OpenAI and Gemini), refused stream, refused connection, missing key. The mock parses the request JSON and answers a tool result only when it is in the provider's shape with the right id or name; a substring check would pass on the replayed call and miss a wrongly named field.
- `llm_agent_test`: one two-step errand (weather, then time, then the answer) through `Agent->Run` and through a hand-written loop that holds only a `Session`, against all four mocks; each mock moves a step only when the earlier result arrived exactly.

Beyond the shapes above, the mocks enforce the rules the real APIs enforce on a request (Gemini: no empty `properties`, no keywords outside its subset; OpenAI: `strict` stated on every tool) and answer a 400 otherwise, so a regression there fails a test rather than shipping. Each test was checked able to fail by breaking the library (wrong field name for a tool result, a wrong tool name, a dropped result, a changed header, a changed stop mapping, an ignored stream event) and confirming a check fails; see the pull request for the list.

## Not verified

None of this has run against the live services. The wire formats are written from the providers' public documentation; a field the mocks do not exercise (for example a newer reasoning-item shape, Gemini's behaviour at the end of a real stream, OpenAI's treatment of `strict: false`) may differ and would show up as an error from the API.
