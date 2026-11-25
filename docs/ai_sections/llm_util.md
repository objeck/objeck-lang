## LLM Utilities (API.LlmUtil)

Four helpers for the work around a chat model, written in Objeck with no network code of their own: **prompt templates** and a message builder, **token estimation** with a context window that keeps a conversation inside a budget, **JSON Schema validation** that reports every problem with a JSON pointer, and **structured calls** that ask any chat backend for JSON matching a schema and retry with the validation errors until the reply is right. They work with any chat backend through the `ChatFn` interface, a two-method interface you implement for the client you use.

The library is `llm_util`, compiled with `-lib llm_util,regex,json,gen_collect`. It links no client, makes no network call and needs no key. `-lib @ml` lists it and what it needs.

### Prompt templates

A `PromptTemplate` is text with `{{placeholders}}`. `PromptVars` holds the values: text, numbers (rendered as text) and lists.

```ruby
use API.LlmUtil;
use Collection;

class Example1 {
  function : Main(args : String[]) ~ Nil {
    template := PromptTemplate->New("Summarize for {{audience|a general reader}}.\n{{#if style}}Style: {{style:lower}}\n{{/if}}Sources:\n{{#each sources}}{{@number}}. {{this}}\n{{/each}}Text: {{text:json}}");
    vars := PromptVars->New()
      ->Set("text", "She said \"hello\".")
      ->Set("style", "TERSE")
      ->SetArray("sources", ["Wikipedia", "RFC 8259"]);
    template->Render(vars)->PrintLine();

    "---"->PrintLine();
    broken := PromptVars->New()->Set("style", "x");
    if(template->Render(broken) = Nil) {
      template->GetError()->PrintLine();
    };
  }
}
```

```
Summarize for a general reader.
Style: terse
Sources:
1. Wikipedia
2. RFC 8259
Text: She said \"hello\".
---
missing required variables: sources, text
```

| Syntax | Meaning |
|---|---|
| `{{name}}` | a required variable |
| `{{name?}}` | optional: empty when it is not set |
| `{{name\|fallback}}` | a default, the text after the bar, used verbatim (a filter applies to it too) |
| `{{name:json}}` | a filter: `json` escapes for the inside of a JSON string, `upper`, `lower`, `trim`; combine as `{{name:json\|fallback}}` |
| `{{#each items}} ... {{/each}}` | repeat for each item of a list; inside, `{{this}}` is the item, `{{@index}}` counts from 0 and `{{@number}}` from 1; `{{#each items?}}` allows a missing list; loops nest and `this` is the innermost |
| `{{#if name}} ... {{else}} ... {{/if}}` | the first part when the variable is set and not empty (a list: not empty), else the second; `{{else}}` is optional |
| `\{{` | a literal `{{` |

- **A clear error.** `Render` returns `Nil` and `GetError()` names every missing required variable at once (`missing required variables: sources, text`), not the first one. `GetMissing()` lists them, `Check(vars)` asks without keeping the text, and `GetRequired()` and `GetOptional()` list what a template needs, in order of first use.
- **Escaping.** Values are never scanned for placeholders, so text a user supplies cannot add a `{{variable}}` or a loop to your prompt; a list item that reads `{{secret}}` renders as `{{secret}}`. Use the `json` filter when the value goes inside a JSON string in the prompt.
- **A bad template is a value, not a crash.** `IsOk()` is false and `GetError()` says what and where: `unclosed '{{' at offset 3`, `the {{#each xs}} block is never closed`, `'{{/each}}' at offset 2 closes nothing`, `unknown filter 'rot13' ...`, `'{{this}}' at offset 0 is only valid inside {{#each}}`. A list used where text is wanted (and the other way round) is a render error too.

`PromptBuilder` collects a conversation: a system prompt (`System`) and user and assistant turns (`User`, `Assistant`), each from text or from a template and values. It is provider-neutral; two calls give the common shapes:

| Call | Gives | For |
|---|---|---|
| `builder->ToPairs()` | `Vector<Pair<String,String>>`, system first | `API.OpenAI.Chat.Completion->Complete` |
| `builder->ToJsonMessages(with_system)` | a JSON array of `role` and `content` objects | OpenAI-style APIs and Ollama (`false` leaves the system prompt out, as Anthropic wants it apart) |

A failed template inside the builder adds no turn and is kept in `builder->GetError()`.

### Token estimation

`TokenEstimator` guesses how many tokens a text costs without a tokenizer, a model file or a network call. It counts the pieces a byte-pair tokenizer usually makes: a word costs one token per 6 letters and a new piece at each lower-to-upper case change (so `getHttpResponse` splits), digits cost one token per 3, a run of punctuation one token per 2 characters, a line break one token per 2 (a blank line is one), an indent of two or more spaces one, and non-Latin characters one and a half.

```ruby
use API.LlmUtil;

estimator := TokenEstimator->New();
estimator->Estimate("Hello, world!")->PrintLine();     # 4
claude := TokenEstimator->New()->SetScale(1.15)->SetMargin(0.1);
estimate := claude->Estimate("The quick brown fox jumps over the lazy dog.");   # 12
claude->Safe(estimate)->PrintLine();                    # 14, with the 10 percent margin
```

**Error bounds.** The estimator was calibrated by hand against the cl100k tokenizer, whose counts for two standard sentences it matches exactly (`Hello, world!` is 4 and `The quick brown fox jumps over the lazy dog.` is 10). On longer text, expect:

| Text | Characters per token | Estimate against the true count |
|---|---|---|
| English prose | about 4.3 (a real tokenizer gives about 4.0 to 4.7) | within about 15 percent, a little high |
| Source code and JSON | about 3.1 for Python, 2.8 for JSON (a real tokenizer gives roughly 3 to 3.8 for code) | within about 30 percent |
| Non-Latin text (CJK) | about 0.7 | within about 50 percent; the weakest case |

A single short string can be off by two tokens either way. The regression test holds the estimator to those characters-per-token ranges on fixed samples; it has **not** been compared with a live tokenizer in CI, so treat the table as the design target, not a measured guarantee. Claude's tokenizer usually produces 10 to 20 percent more tokens than cl100k for the same text, so `SetScale(1.15)` is a good setting for Anthropic models, and `SetMargin` plus `Safe()` keeps a budget from being the exact limit. Where a hard limit matters, use the provider's own count.

**Exact counts.** Where a hard limit matters, use the provider's own count. `TokenCounter` is a one-method interface (`Count(text)`), so `TokenEstimator` and a class of your own that asks a provider both fit it.

### Context windows

`ContextWindow` keeps a conversation inside a token budget. `Fit` drops the oldest turns first, always keeps the system prompt and the newest turn, never leaves a conversation starting on an assistant turn, and leaves its input unchanged. An omission note can be added to the system prompt when something was dropped, so the model knows the history is partial.

```ruby
use API.LlmUtil;

conversation := PromptBuilder->New()->System("You are a careful assistant.");
for(i := 0; i < 20; i += 1;) {
  if(i % 2 = 0) {
    conversation->User("question {$i} about the topic");
  }
  else {
    conversation->Assistant("answer {$i} about the topic");
  };
};

window := ContextWindow->New(120, TokenEstimator->New())->SetOmissionNote("[earlier messages omitted]");
fitted := window->Fit(conversation);
"kept {$fitted->Size()} of {$conversation->Size()} turns, dropped {$window->GetDropped()}, {$window->GetTokens()} tokens"->PrintLine();
```

```
kept 10 of 20 turns, dropped 10, 119 tokens
```

The kept turns are 10 to 19 (the first is `question 10`, a user turn), and the system prompt now ends with `[earlier messages omitted]`. Every turn costs its text plus 4 tokens of framing (`SetTurnOverhead` changes that). If the system prompt and the newest turn alone exceed the budget they are still returned and `IsOverBudget()` is true. `Measure(builder)` counts a conversation, and `Truncate(text, max_tokens, marker)` shortens one long text at a word boundary (`...` after a cut). This drops turns; it does not summarize them. To summarize, ask a model to, then put the summary in the system prompt.

### JSON Schema validation

`JsonSchema` checks a JSON value against a schema and returns every problem, each as a `SchemaError` with a JSON pointer (RFC 6901) to the offending value, the keyword that objected, and a message.

```ruby
use API.LlmUtil;
use Data.JSON;
use Collection;

schema := JsonParser->TextToElement("{\"type\":\"object\",\"required\":[\"name\",\"age\",\"tags\"],\"properties\":{\"name\":{\"type\":\"string\",\"minLength\":1},\"age\":{\"type\":\"integer\",\"minimum\":0,\"maximum\":150},\"email\":{\"type\":\"string\",\"pattern\":\"^[^@ ]+@[^@ ]+$\"},\"tags\":{\"type\":\"array\",\"items\":{\"enum\":[\"a\",\"b\",\"c\"]}}},\"additionalProperties\":false}");
value := JsonParser->TextToElement("{\"name\":\"\",\"age\":200,\"email\":\"nobody\",\"tags\":[\"a\",\"z\"],\"extra\":1}");

errors := JsonSchema->Check(schema, value);
each(i : errors) {
  error := errors->Get(i);
  "  {$error->ToString()}   [{$error->GetKeyword()}]"->PrintLine();
};
```

```
  /age: 200 is above the maximum 150   [maximum]
  /email: "nobody" does not match the pattern '^[^@ ]+@[^@ ]+$'   [pattern]
  /name: is 0 characters, shorter than the minimum 1   [minLength]
  /tags/1: "z" is not one of: "a", "b", "c"   [enum]
  /extra: the property 'extra' is not allowed   [additionalProperties]
```

`JsonSchema->New(schema)` makes a reusable validator (`Validate(value)` returns the errors, `IsValid(value)` a Bool); `JsonSchema->Check(schema, value)` does both in one call, and `JsonSchema->Describe(errors)` joins the list into lines.

Supported keywords:

| Applies to | Keywords |
|---|---|
| any value | `type` (a name or a list of names: `string`, `number`, `integer`, `boolean`, `object`, `array`, `null`), `enum`, `const`, `allOf`, `anyOf`, `oneOf`, `not`, `$ref` |
| strings | `minLength`, `maxLength` (in characters), `pattern` (an unanchored search, with the `RegEx` dialect) |
| numbers | `minimum`, `maximum`, `exclusiveMinimum`, `exclusiveMaximum`, `multipleOf` (`1.0` and `1e2` are integers) |
| objects | `properties`, `required`, `additionalProperties` (`false` or a schema), `minProperties`, `maxProperties` |
| arrays | `items` (a schema, or a list for a tuple), `minItems`, `maxItems`, `uniqueItems` |

- **Pointers.** The root is the empty pointer, shown as `(root)`; `/users/1/address/zip` is the zip of the second user. A `/` in a property name is written `~1` and a `~` is `~0`. A missing required property is reported at that property's own pointer (`/age`), so the path says what to add.
- **Order.** Errors come in a fixed order: `type`, `enum` and `const`, then the keywords for the value's kind (an object's `required` before its `properties`, with properties in alphabetical order), then `allOf`, `anyOf`, `oneOf` and `not`. A value of the wrong type stops further checks on it, so one mistake is one error.
- **Combinators.** `anyOf` and `oneOf` say which alternative came closest (`matches none of the 2 allowed shapes: /id: the required property 'id' is missing`); `oneOf` also reports a value that matches more than one.
- **`$ref`.** `#`, `#/definitions/x` and `#/$defs/x` are resolved inside the same schema, recursive ones included, and a loop is stopped at a depth of 64. A remote reference is reported, not fetched.
- **Ignored.** `format`, `title`, `description`, `default`, `examples`, `patternProperties`, `dependencies`, `if`/`then`/`else` and every unknown keyword. A boolean schema is accepted (`false` rejects everything). A malformed schema is reported as a `schema` error, not a crash, and an invalid `pattern` as a `pattern` error. `SetMaxErrors(n)` caps the list (default 100).

### Structured calls with retry

`StructuredCall` asks a chat backend for JSON that matches a schema. It adds the schema to the system prompt, pulls the JSON out of the reply (a fenced block, a sentence around it, or the whole text), validates it, and when it is wrong sends the model its own reply together with the exact errors and asks again, up to a retry limit. The outcome is the typed JSON value or the final error list.

The backend is a `ChatFn`, two methods: `Complete(turns)` takes a conversation (`PromptTurn` objects, a `system` turn first, ending with a `user` turn) and returns the reply text, or `Nil` with `GetError()` saying why. This program runs with no key: its `ChatFn` plays a model that answers wrongly first and then in a fence.

```ruby
use API.LlmUtil;
use Data.JSON;
use Collection;

class ScriptedModel implements ChatFn {
  @replies : Vector<String>;
  @asked : Int;

  New() {
    @replies := Vector->New()<String>;
    @replies->AddBack("{'name': 'Ada Lovelace', 'age': '36',}");
    @replies->AddBack("Sure, here it is:\n```json\n{\"name\": \"Ada Lovelace\", \"age\": 36}\n```");
  }

  method : public : Complete(turns : Vector<PromptTurn>) ~ String {
    number := @asked + 1;
    "request {$number}: {$turns->Size()} turns, last turn:"->PrintLine();
    turns->Last()->GetText()->PrintLine();
    reply := @replies->Get(@asked);
    @asked += 1;
    return reply;
  }

  method : public : GetError() ~ String {
    return Nil;
  }
}

class Example4 {
  function : Main(args : String[]) ~ Nil {
    schema := JsonParser->TextToElement("{\"type\":\"object\",\"required\":[\"name\",\"age\"],\"properties\":{\"name\":{\"type\":\"string\"},\"age\":{\"type\":\"integer\"}}}");

    call := StructuredCall->New(ScriptedModel->New(), schema)->SetMaxRetries(2)->SetRepair(true);
    outcome := call->Ask("Extract the person: Ada Lovelace is 36.");
    "---"->PrintLine();
    if(outcome->IsOk()) {
      "ok after {$outcome->GetAttempts()} attempts: {$outcome->GetValue()->ToString()}"->PrintLine();
    }
    else {
      outcome->GetError()->PrintLine();
    };
  }
}
```

```
request 1: 2 turns, last turn:
Extract the person: Ada Lovelace is 36.
request 2: 4 turns, last turn:
Your reply did not match the schema. Problems:
- /age: expected integer, got string
Reply again with only the corrected JSON value.
---
ok after 2 attempts: {"age":36,"name":"Ada Lovelace"}
```

The first reply used single quotes and a trailing comma, so with `SetRepair(true)` it was repaired and parsed, then failed validation (`age` is a string) and the model was shown the exact error. Without repair the first reply would have failed to parse and the feedback would have said so.

With a real model, implement `ChatFn` over the client you use (build the client's conversation from the turns, send it, return the reply text, or `Nil` with `GetError()` set) and pass it to `StructuredCall->New(chat, schema)`. A transport failure is the backend's error and is not retried.

- **What counts as success.** Every JSON value found in the reply is tried, fenced blocks first, then each balanced `{...}` or `[...]` span, then the whole text for a bare scalar; the first that matches the schema wins, so a reply with an aside (`see [1]`) before the answer still works. A bare scalar reply works when the schema is a scalar schema (`4` for an integer range).
- **Retries.** `SetMaxRetries(n)` (default 2, so up to three requests; 0 asks once). The feedback lists up to 8 errors with their pointers and counts the rest. A reply with no JSON gets `Your reply could not be used: the reply holds no JSON object or array.` A transport failure (`Complete` returned `Nil`) is not retried: the outcome's error is `the chat backend failed: <reason>`.
- **The outcome.** `IsOk()`, `GetValue()` (a `JsonElement`), `GetErrors()` (the last reply's `SchemaError` list), `GetError()` (one line), `GetAttempts()`, `GetReplies()` (every raw reply) and `WasRepaired()`.
- **Other inputs.** `Ask(builder)` takes a whole conversation (its system prompt is kept and the schema instruction added after it; the builder is not changed) and `Ask(template, vars)` a template. `SetInstruction(text)` replaces the sentence that asks for JSON only; the schema is always appended.
- **Provider structured modes.** The loop is a plain chat, so it works the same everywhere. A provider's native mode (a forced tool, a response schema, an Ollama `format`) can be used by wrapping it in your own `ChatFn`; the validation and retry then still apply.

#### Finding and repairing JSON

`JsonExtractor` is what `StructuredCall` uses to find the JSON: `Candidates(text)` lists the places in a reply that may hold JSON, `ParseAll(text)` and `Parse(text)` parse them. Parsing is strict (`JsonStrict`, an RFC 8259 checker: the Objeck `JsonParser` alone accepts trailing commas and bare words, which would hide the damage a repair exists to fix).

`SetRepair(true)` turns on a lenient repair, **off by default and used only after the strict parse failed**. `JsonRepair->Repair(text)` is one pass that fixes:

| Damage | Becomes |
|---|---|
| trailing commas: `{"a":1,}` | `{"a":1}` |
| single quotes: `{'a':'b'}` | `{"a":"b"}` (inner double quotes escaped) |
| unquoted keys: `{a:1}` | `{"a":1}` |
| a reply cut off: `{"a":[1,2` or `{"a":"hel` | closed: `{"a":[1,2]}`, `{"a":"hel"}`; a dangling `"a":` or key gets `null` |
| raw line breaks and tabs inside strings | `\n` and `\t` |
| Python and JavaScript literals: `True`, `None`, `undefined`, `NaN` | `true`, `null` |
| comments and a mismatched closer | removed, closer corrected |
| text after the value | dropped |

It does not guess further: a missing value (`{"a":,"b":1}`), a missing colon or a missing comma between items stays an error. A repaired value is still validated against the schema, and `WasRepaired()` says it was repaired.

### API summary

| Class | Main members |
|---|---|
| `PromptTemplate` | `New(text)`, `IsOk()`, `Render(vars)`, `GetError()`, `GetMissing()`, `Check(vars)`, `GetRequired()`, `GetOptional()` |
| `PromptVars` | `Set`, `SetInt`, `SetFloat`, `SetBool`, `SetList`, `SetArray`, `Get`, `GetList`, `Has`, `HasList` |
| `PromptBuilder`, `PromptTurn` | `System`, `User`, `Assistant`, `Add`, `GetSystem`, `GetTurns`, `GetAll`, `Size`, `Clone`, `ToPairs`, `ToJsonMessages`, `GetError` |
| `TokenCounter` (interface), `TokenEstimator` | `Count(text)`; `Estimate`, `EstimateTurns`, `Safe`, `SetScale`, `SetMargin`, `SetTurnOverhead` |
| `ContextWindow` | `New(budget, counter)`, `Fit`, `Measure`, `Truncate`, `GetDropped`, `GetTokens`, `IsOverBudget`, `SetOmissionNote`, `SetTurnOverhead` |
| `JsonSchema`, `SchemaError` | `New(schema)`, `Validate`, `IsValid`, `Check`, `Describe`, `Same`, `Join`, `SetMaxErrors`; `GetPath`, `GetKeyword`, `GetMessage` |
| `JsonStrict`, `JsonRepair`, `JsonExtractor` | `Validate`, `GetError`; `Repair`; `Candidates`, `ParseAll`, `Parse`, `SetRepair`, `IsRepaired`, `GetError` |
| `ChatFn` (interface), `StructuredCall`, `StructuredOutcome` | `Complete`, `GetError`; `Ask` (text, builder or template), `SetMaxRetries`, `SetRepair`, `SetInstruction`; `IsOk`, `GetValue`, `GetErrors`, `GetError`, `GetAttempts`, `GetReplies`, `WasRepaired` |

### Limits and caveats

- **The token estimate is an estimate.** See the bounds above; it is the right tool for a budget with a margin, not for a hard limit.
- **A `String` is a sequence of characters.** `minLength` and the estimator count characters, not UTF-8 bytes.
- **Not thread-safe.** A `PromptTemplate` keeps its last error and missing list, and a `JsonSchema` its error list, so give each thread its own. A `RegEx` compiled for `pattern` is cached in the validator.
- **The schema subset above is all there is.** `patternProperties`, `dependencies`, conditional schemas and `format` are ignored without a message, so a schema that relies on them validates more loosely than a full implementation would.
- **Class names.** The library avoids `Tool`, `ToolCall`, `Message`, `Reply`, `Response` and `Chat` on purpose: generic arguments from another library resolve by short name, and a class with one of those names would hijack the clients' own signatures.

It is tested by `llm_util_template_test.obs`, `llm_util_tokens_test.obs`, `llm_util_schema_test.obs` (a table of about 170 schema, value and expected-error cases), `llm_util_extract_test.obs` (extraction, the repair table and the strict checker), `llm_util_structured_test.obs` (a scripted `ChatFn`: valid first time, fixed on a retry, never valid, chatty and fenced replies, repair, a failing backend) and `llm_util_clients_test.obs`. None uses the network or a key.
