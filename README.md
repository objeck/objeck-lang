<p align="center">
  <img src='https://github.com/objeck/objeck-lang/blob/master/core/lib/code_doc/templates/resources/objeck-logo-alt.png' height="125px"/>
</p>

<p align="center">
<strong>Object-oriented • JIT-compiled • AI-native • Robust APIs
</p>

<hr/>

<p align="center">
  <a href="https://github.com/objeck/objeck-lang/actions/workflows/codeql.yml"><img src="https://github.com/objeck/objeck-lang/actions/workflows/codeql.yml/badge.svg" alt="GitHub CodeQL"></a>
  <a href="https://scan.coverity.com/projects/objeck"><img src="https://scan.coverity.com/projects/10314/badge.svg" alt="Coverity Scan Build Status"></a>
  <a href="https://github.com/objeck/objeck-lang/actions/workflows/ci-build.yml"><img src="https://github.com/objeck/objeck-lang/actions/workflows/ci-build.yml/badge.svg" alt="CI Build"></a>
  <a href="https://github.com/objeck/objeck-lang/actions/workflows/release-build.yml"><img src="https://github.com/objeck/objeck-lang/actions/workflows/release-build.yml/badge.svg" alt="Release Build"></a>
  <a href="https://github.com/objeck/objeck-lang/releases"><img src="https://img.shields.io/badge/release-v2026.11.3-blue" alt="Latest Release"></a>
</p>

<p align="center">
  <a href="https://playground.objeck.org">Try it online</a> &middot;
  <a href="#downloads">Download</a> &middot;
  <a href="#quick-start">Quick start</a> &middot;
  <a href="#libraries">Libraries</a> &middot;
  <a href="https://www.objeck.org">Documentation</a> &middot;
  <a href="https://github.com/objeck/objeck-lang/discussions">Discussions</a>
</p>

## Why Objeck?

**Built for modern development:**
- 🚀 **JIT-compiled** for performance (ARM64/AMD64)
- 🤖 **AI-native**: OpenAI, Anthropic, Gemini, Ollama, ONNX, OpenCV — no third-party packages
- 🌐 **Network-complete**: HTTP/1.1 · HTTP/2 · HTTP/3/QUIC · WebSocket · DTLS — all standard library
- 💻 **Developer-friendly**: REPL shell, LSP plugins for VSCode/Sublime/Kate, DAP debugger
- 🌍 **Cross-platform**: Linux, macOS, Windows (x64 + ARM64, including 64-bit Raspberry Pi)
- 🔧 **Full-featured**: Threads, generics, closures, reflection, serialization

**Perfect for:**
AI/ML prototyping • Computer vision • Web services • Real-time applications • Game development

## Try It Online

👉🏽 [Playground](https://playground.objeck.org) — 42 demos across 10 categories, Monaco editor, no install required.

## Quick Start

```bash
# Ubuntu 24.04: one package, system libraries included (-arm64 for ARM)
curl -LO https://github.com/objeck/objeck-lang/releases/download/v2026.11.3/objeck-ubuntu24.04-x64_2026.11.3.deb
sudo apt install ./objeck-ubuntu24.04-x64_2026.11.3.deb

# Other Linux (see Platform support below), or macOS: the archive
curl -LO https://github.com/objeck/objeck-lang/releases/download/v2026.11.3/objeck-linux-x64_2026.11.3.tgz
tar xzf objeck-linux-x64_2026.11.3.tgz
# Linux only: install the system libraries the toolchain links against
# (mbedTLS, readline, SDL2/GL, OpenCV, unixODBC, LAME) --
# obr does not start without them. --check reports without installing.
./objeck-lang/install_deps.sh
# Sets PATH and OBJECK_LIB_PATH for this shell. Source it, do not run it.
source ./objeck-lang/env.sh

# Hello World
echo 'class Hello {
  function : Main(args : String[]) ~ Nil {
    "Hello World"->PrintLine();
  }
}' > hello.obs

# Compile and run (modern syntax)
obc hello && obr hello
```

📖 **Full docs**: [objeck.org](https://www.objeck.org) &nbsp;·&nbsp; 💡 **Examples**: [github.com/objeck/objeck-lang/programs](https://github.com/objeck/objeck-lang/tree/master/programs)

### Platform support

| Platform | Install with | Notes |
|---|---|---|
| Windows (x64, ARM64) | `.msi` or `.zip` | Nothing else needed |
| macOS (Apple silicon) | `.pkg`, `.tgz` or `.zip` | Nothing else needed |
| Ubuntu 24.04 (x64, ARM64) | `.deb` | Installs its own dependencies |
| Other Linux (x64, ARM64) | `.tgz` | glibc 2.38 or newer, then `install_deps.sh` |

<details>
<summary>On an older Linux, such as Debian 12 or Ubuntu 22.04?</summary>

The Linux release is built on Ubuntu 24.04 and needs glibc 2.38 or newer, which Debian 12 (and antiX 23, MX 23 and other distributions based on it) and Ubuntu 22.04 do not have, so `dpkg` refuses the `.deb` there. Two ways to run Objeck on them:

- **A container:** run the Ubuntu 24.04 release in Docker, Podman or distrobox.
- **From source:** build with `core/release/deploy_posix.sh`, the path `install_deps.sh` itself points to (not checked on every distribution).

A Linux build that runs on older distributions is planned.

The toolchain also links the system libraries `install_deps.sh` lists, such as the mbedTLS 2.28 (`.so.14`) and OpenCV 4.6 (`.so.406`) sonames. Some newer distributions do not package those versions either (Debian 13, Fedora, Arch and openSUSE Leap, checked in September for v2026.9.2). `./objeck-lang/install_deps.sh --check` reports what is missing without installing anything.

</details>

## Examples

Every program below was compiled and run; the output is what it printed. They are in
[`programs/examples/guides`](programs/examples/guides/), with longer ones and a [guide](docs/guides/README.md)
for each topic. Libraries are named with `-lib`; `obc` does not pull in a library's own dependencies.

### The language

Classes, virtual methods, a generic `Vector`, a closure and string formatting.

<details>
<summary>Program and output (<code>readme_tour.obs</code>)</summary>

```ruby
use Collection;

class Shape {
  @name : String;

  New(name : String) {
    @name := name;
  }

  method : virtual : public : Area() ~ Float;

  method : public : Describe() ~ String {
    return "{$@name}: area {$(Area()):.2}";
  }
}

class Circle from Shape {
  @radius : Float;

  New(radius : Float) {
    Parent("circle");
    @radius := radius;
  }

  method : public : Area() ~ Float {
    return Float->Pi() * @radius * @radius;
  }
}

class Square from Shape {
  @side : Float;

  New(side : Float) {
    Parent("square");
    @side := side;
  }

  method : public : Area() ~ Float {
    return @side * @side;
  }
}

class Tour {
  function : Main(args : String[]) ~ Nil {
    shapes := Vector->New()<Shape>;
    shapes->AddBack(Circle->New(1.5));
    shapes->AddBack(Square->New(2.0));
    each(i : shapes) {
      shapes->Get(i)->Describe()->PrintLine();
    };

    # a function that returns a function (the closure remembers `factor`)
    triple := Scaler(3);
    "triple(14) = {$(triple(14))}"->PrintLine();
  }

  function : Scaler(factor : Int) ~ (Int) ~ Int {
    return \(Int) ~ Int : (x) => x * factor;
  }
}
```

```text
$ obc -src readme_tour.obs
$ obr readme_tour.obe
circle: area 7.07
square: area 4.00
triple(14) = 42
```

</details>

### A database in your program

`Query.Structured` runs SQL over typed in-memory tables, files and archives. A bad query is a message, not a crash. [Guide](docs/guides/query.md)

<details open>
<summary>Program and output (<code>readme_query.obs</code>)</summary>

```ruby
use Query.Structured;

class Sales {
  function : Main(args : String[]) ~ Nil {
    sales := Table->New("sales", ["region", "item", "qty", "price"]);
    Add(sales, "east", "widget", 10, 2.5);
    Add(sales, "west", "gadget", 4, 10.0);
    Add(sales, "east", "gadget", 3, 10.0);
    Add(sales, "west", "widget", 7, 2.5);

    result := sales->Query("select region, sum(qty * price) as revenue from sales group by region order by revenue desc");
    result->ToCsv()->Print();

    if(sales->Query("select nothing from sales") = Nil) {
      sales->GetError()->PrintLine();
    };
  }

  function : Add(table : Table, region : String, item : String, qty : Int, price : Float) ~ Nil {
    row := table->Insert();
    row->Set("region", region);
    row->Set("item", item);
    row->Set("qty", IntRef->New(qty));
    row->Set("price", FloatRef->New(price));
  }
}
```

```text
$ obc -src readme_query.obs -lib query,misc,net,json,regex,xml,csv,cipher
$ obr readme_query.obe
region,revenue
west,57.5
east,55.0
unknown column 'nothing'
```

</details>

### JSON

Parse, read typed values, build, and tell when the text is not JSON. [Guide](docs/guides/data_and_text.md)

<details>
<summary>Program and output (<code>readme_json.obs</code>)</summary>

```ruby
use Data.JSON;

class Json {
  function : Main(args : String[]) ~ Nil {
    root := JsonParser->TextToElement("{\"name\": \"Ada\", \"langs\": [\"en\", \"fr\"], \"age\": 36}");
    name := root->Get("name")->GetString();
    age := root->Get("age")->GetInt();
    langs := root->Get("langs")->Size();
    "{$name} is {$age} and speaks {$langs} languages"->PrintLine();

    doc := JsonElement->New(JsonElement->JsonType->OBJECT);
    doc->Insert("ok", true);
    doc->Insert("count", 3);
    doc->ToString()->PrintLine();

    broken := JsonParser->TextToElement("{\"name\": ");
    "broken text parses to Nil: {$(broken = Nil)}"->PrintLine();
  }
}
```

```text
$ obc -src readme_json.obs -lib json
$ obr readme_json.obe
Ada is 36 and speaks 2 languages
{"count":3,"ok":true}
broken text parses to Nil: true
```

</details>

### Regular expressions

Find, capture, replace. A pattern the engine cannot accept is not OK and matches nothing. [Guide](docs/guides/data_and_text.md)

<details>
<summary>Program and output (<code>readme_regex.obs</code>)</summary>

```ruby
use Query.RegEx;

class Dates {
  function : Main(args : String[]) ~ Nil {
    text := "shipped 2026-10-31, patched 2026-11-02";
    found := RegEx->New("(\\d{4})-(\\d{2})-(\\d{2})")->Find(text);
    each(i : found) {
      date := found->Get(i);
      "{$(date->GetValue())} is month {$(date->GetGroup(2))}"->PrintLine();
    };

    hidden := RegEx->New("\\d")->ReplaceAll("call 555-0199", "#");
    hidden->PrintLine();

    bad := RegEx->New("a{2,1}");
    "a{2,1} is a valid pattern: {$(bad->IsOk())}"->PrintLine();
  }
}
```

```text
$ obc -src readme_regex.obs -lib regex
$ obr readme_regex.obe
2026-10-31 is month 10
2026-11-02 is month 11
call ###-####
a{2,1} is a valid pattern: false
```

</details>

### Collections

A `Hash` to count, a `Map` to report in key order. [Guide](docs/guides/collections.md) and [choosing a collection](docs/COLLECTIONS_GUIDE.md)

<details>
<summary>Program and output (<code>readme_collections.obs</code>)</summary>

```ruby
use Collection;

class Words {
  function : Main(args : String[]) ~ Nil {
    counts := Hash->New()<String, IntRef>;
    words := "the cat and the hat and the bat"->Split(" ");
    each(i : words) {
      seen := counts->Find(words[i]);
      if(seen = Nil) {
        counts->Insert(words[i], IntRef->New(1));
      }
      else {
        seen->Set(seen->Get() + 1);
      };
    };

    ordered := Map->New()<String, IntRef>;
    pairs := counts->GetKeyValues();
    each(i : pairs) {
      ordered->Insert(pairs->Get(i)->GetFirst(), pairs->Get(i)->GetSecond());
    };
    entries := ordered->GetKeyValues();
    each(i : entries) {
      "{$(entries->Get(i)->GetFirst())}: {$(entries->Get(i)->GetSecond()->Get())}"->PrintLine();
    };
  }
}
```

```text
$ obc -src readme_collections.obs
$ obr readme_collections.obe
and: 2
bat: 1
cat: 1
hat: 1
the: 3
```

</details>

### A web server and a client

A one-route server, a request to it, and a clean stop. [Guide](docs/guides/http.md)

<details>
<summary>Program and output (<code>readme_http.obs</code>)</summary>

```ruby
use System.Concurrency;
use Web.HTTP.Server;

class Greeter from HttpRequestHandler {
  New() {
    Parent();
  }

  method : ProcessGet(request : Request, response : Response) ~ Bool {
    name := request->GetParam("name");
    if(name = Nil) {
      name := "world";
    };
    response->SetCodeTypeContent(200, "text/plain", "Hello, {$name}!");
    return false;
  }

  method : ProcessPost(request : Request, response : Response) ~ Bool {
    response->SetCodeContent(405, "Method not allowed");
    return false;
  }
}

class HelloWeb {
  function : Main(args : String[]) ~ Nil {
    # Serve() blocks, so it runs on a thread; port 0 means any free port
    server := Thread->New() {
      New() {
        Parent();
      }

      method : public : Run(arg : System.Base) ~ Nil {
        WebServer->Serve(Greeter->New()->GetClass(), "127.0.0.1", 0);
      }
    };
    server->Execute(Nil);

    while(WebServer->GetBoundPort() = 0) {
      Thread->Sleep(25);
    };
    port := WebServer->GetBoundPort();

    client := Web.HTTP.HttpClient->New();
    response := client->Get(Web.HTTP.Url->New("http://127.0.0.1:{$port}/hello?name=Ada"));
    "{$(response->GetCode())} {$(response->GetContent()->ToString())}"->PrintLine();

    WebServer->Stop();
  }
}
```

```text
$ obc -src readme_http.obs -lib net,net_server,json,cipher
$ obr readme_http.obe
200 Hello, Ada!
```

</details>

### Game search

Describe a game once; `Minimax` and `MonteCarloTreeSearch` choose moves. [Guide](docs/guides/ai.md)

<details>
<summary>Program and output (<code>readme_ai.obs</code>)</summary>

```ruby
use System.AI;

class Nim implements GameState {
  @stones : Int;
  @a_to_move : Bool;

  New(stones : Int, a_to_move : Bool) {
    @stones := stones;
    @a_to_move := a_to_move;
  }

  method : public : GetMoves() ~ Int[] {
    count := @stones < 3 ? @stones : 3;
    moves := Int->New[count];
    each(i : count) {
      moves[i] := i + 1;
    };
    return moves;
  }

  method : public : Apply(move : Int) ~ GameState {
    return Nim->New(@stones - move, <>@a_to_move);
  }

  method : public : IsTerminal() ~ Bool {
    return @stones = 0;
  }

  # +1 when player A took the last stone, -1 when B did
  method : public : Evaluate() ~ Float {
    if(@stones > 0) {
      return 0.0;
    };
    return @a_to_move ? -1.0 : 1.0;
  }

  method : public : IsMaximizing() ~ Bool {
    return @a_to_move;
  }
}

class Play {
  function : Main(args : String[]) ~ Nil {
    state := Nim->New(10, true)->As(GameState);
    "Minimax takes {$(Minimax->New(12)->FindBestMove(state))} of 10 stones"->PrintLine();
    "Monte Carlo takes {$(MonteCarloTreeSearch->New(2000, 1.414, 7)->FindBestMove(state))} of 10 stones"->PrintLine();
  }
}
```

```text
$ obc -src readme_ai.obs -lib ai
$ obr readme_ai.obe
Minimax takes 2 of 10 stones
Monte Carlo takes 2 of 10 stones
```

</details>

More: [all the guides](docs/guides/README.md), the 32 numbered programs in [`programs/deploy`](programs/deploy/README.md) that ship with every release, and [what is new in v2026.11.3](#whats-new).

## What's New

Full details for every release are in the [CHANGELOG](CHANGELOG.md).

### v2026.11.4
  * **A toolkit for AI agents** &mdash; `API.Agent` is a provider-neutral tool-use loop for Claude, OpenAI, Gemini and Ollama with cancellation, budgets, bounded tool calls and a trace; `mcp` is a Model Context Protocol server and client over stdio and streamable HTTP; `System.Actor` adds actors with supervision; and `llm_util`, `rag` and `System.Telemetry` add prompt templates and JSON Schema checks, a retrieval-augmented generation kit and inference timing, all in Objeck.
  * **Functions become MCP tools** &mdash; a `@tool`, `@resource` or `@prompt` line in a function's doc comment is the whole declaration: the compiler writes the name, the JSON Schema and the argument checks. `obr --mcp prog.obe` serves it on standard input and output and `obr --mcp-http=8080 prog.obe` over HTTP. `mcp_stubs` writes a typed client and `--check` fails the build when a server's tools change, and the `@readonly` and `@destructive` hints tell an agent which calls to ask about. See the [MCP walkthrough](docs/MCP_WALKTHROUGH.md).
  * **Scoped agent limits** &mdash; `AgentScope->Run(scope, Work() ~ Nil)` applies one step, token, tool-call and time budget, a cancel switch and a trace to every agent run in the block; a run that finds nothing left is not started.
  * **Agents on a local model** &mdash; `OllamaBackend` runs the same agent loop on a model served by Ollama (tried with `qwen3:8b`): no API key, and no data leaves the machine. Requires a local Ollama server. See [Local models with Ollama](docs/AI.md#local-models-with-ollama).
  * **OpenAI tool calls** &mdash; `Completion->CompleteWithTools` sends a `tools` array to Chat Completions and `Choice->GetToolCalls()` returns the calls the model asked for, so the OpenAI client takes part in the same tool loop as the other providers.
  * **Boosting and forecasting** &mdash; `System.ML.Boost` is gradient boosting (regressor and classifier, early stopping, feature importances) and `System.ML.TimeSeries` is forecasting (imputation, differencing, ACF, an ADF test, exponential smoothing), both in pure Objeck.
  * **ONNX on the GPU** &mdash; `DeviceTensor` and `BoundSession` keep tensors in the provider's memory (ResNet-34, batch 32, RTX 4090: 48.9 ms per `Session->Run`, 6.1 ms with the data resident), `ep=tensorrt` adds an engine cache and CUDA graphs, `Half`, `BFloat16` and `Quantization` convert exactly for FP16 and INT8 models, and an inference no longer holds up the garbage collector or a session shared by threads.
  * **Editor support** &mdash; the language server offers inline completion (off by default, local-first), `inlineValue`, `linkedEditingRange`, `onTypeFormatting`, `willRenameFiles` and variable semantic tokens, and `obd --dap` adds `stepInTargets`, `goto`, `cancel`, `disassemble` and the `invalidated` and `progress` events, halts a running program on `disconnect`, and verifies breakpoints against the program.
  * **Time limits for HTTP** &mdash; `HttpClient` and `HttpsClient` take `SetConnectTimeout`, `SetReadTimeout` and `SetDeadline` (connect, handshake, reads, redirects and retries), where before a server that dribbled one byte at a time could keep a request alive indefinitely.
  * **A thread's error need not end the process** &mdash; `--thread-faults=isolate` (or `OBJECK_THREAD_FAULTS=isolate`) ends only the thread that raised an uncaught error, prints its stack, releases its locks and lets the others run.
  * **Safer secrets** &mdash; the OpenAI, Gemini and Anthropic clients refuse the API key while `OBJECK_TLS_INSECURE_SKIP_VERIFY` is set, and `Cipher.Encrypt->AES256` writes AES-256-GCM with a PBKDF2 key and a random salt and nonce (the old format is still read, and is no longer produced).
  * **Input and files** &mdash; `Console->ReadLine` returns `Nil` at the end of input, and `File` records I/O errors (`IsOK()`, `GetLastError()`, `ClearError()`) instead of ignoring them.
  * **Faster generated code** &mdash; the Linux x64 JIT keeps `Float` loop locals in registers, the ARM64 JIT pins loop locals in callee-saved registers, more methods are JIT-compiled instead of interpreted, the optimizer folds integer identities and uses a control-flow graph for loop-invariant code motion and dead stores, and `obc` resolves names about 27% faster.
  * **Compiler and runtime fixes** &mdash; `String->New(Byte[])` no longer reads past the array, a method chain on a parenthesised calculation keeps every call, a local declared without a value in a loop starts empty each iteration, dead-store elimination keeps a store a handler reads, loop-invariant code motion no longer hoists an observable store, function references to method ids of 32768 or more work, threads making the first call of one long-running method no longer run it 20x slower, and an ARM64 hang on the minimum `Int` is gone.
  * **Library fixes** &mdash; `KNearestNeighbors->Query`, bounded `LevenshteinDistance` and the threaded `VectorIndex` search are corrected at their edges, `SentimentAnalyzer`, `View->ToJson`, `Table->Save` and the Gemini and JSON-schema parsers no longer carry a previous value into the next, and a `Nil` collection lookup reports not-found.
  * **Build and release** &mdash; `core/lib/*.obl` are rebuilt from source and compared in CI, `docs/api.zip` and the Windows `objeck.rc` files are generated instead of committed, and `tools/hpc/alderaan` runs Objeck on a Slurm cluster without root.
  * **Fourteen AI examples** &mdash; `mcp_32.obs` to `agent_scope_45.obs` ship with every release and run offline with no key or model: an MCP server and client, an agent loop, agent run control, a supervised actor, retrieval with citations, structured output with retry, telemetry, a forecast, and the `@tool`, `@resource`, typed-client, approval and `obr --mcp` examples.
  * **A new objeck.org** &mdash; a dark-first design with new navigation, Learn, Start, Libraries and Community pages, and code samples that a CI check compiles and runs.
  * **Steadier tests and CI** &mdash; the nightly differential run no longer sees timings on stdout, the CodeQL steps move to 4.39.4 together, and the Windows build legs have a 90-minute cap.

A function is an MCP tool, and `obr --mcp` serves it (`programs/examples/guides/mcp_tool.obs`; [more](docs/guides/whats_new_11_4.md)):

```ruby
#~
Count the words in a text.
@tool
@readonly
~#
function : CountWords(text : String) ~ Int { ... }
```

```text
$ obr --mcp mcp_tool.obe        # one JSON-RPC message per line on standard input
{"id":2,"jsonrpc":"2.0","result":{"content":[{"text":"3","type":"text"}],"isError":false}}
```

`AgentScope` limits every agent run in a block (`agent_scope_45.obs`): `scope := AgentScope->New()->SetMaxSteps(2); AgentScope->Run(scope, Questions() ~ Nil);` ran question 1 to `completed` and left questions 2 and 3 with `budget_steps` and no model call.

The same agent loop on a model on your own machine (requires a local Ollama server; `agent_local.obs`):

```ruby
agent := Agent->New(OllamaBackend->New("qwen3:8b"), registry);
agent->SetBudget(AgentBudget->New()->SetMaxSteps(6)->SetMaxWallMs(120000));
result := agent->Run("What is 17 plus 25? Use the add tool.");
# answer: The sum of 17 and 25 is **42**.
```

### v2026.11.3 ✅
  * **Regular expressions are 4.5 to 27 times faster** &mdash; `RegEx` finds where the longest match ends with a lazily built DFA, one table lookup per character, and skips start positions that cannot begin a match. On 680 KB of text a class-first search went from 0.55 s to 0.043 s and an alternation with a group from 0.99 s to 0.09 s. Patterns with `\b` or `\B`, or that need more than 1,024 states, run on the NFA as before; capture groups are unchanged. A `RegEx` can now be shared by threads (matching on one instance is serialized).
  * **RegEx bugs fixed** &mdash; `{n}` was read as `{n,}`, so `a{2}` matched `aaa`; it now means exactly n, and `{n,}` is accepted. `ReplaceAll` matched against text it had already rewritten (`\bx` on `xx` with `-` gave `--`); it now works in one pass over the original. `^a`, `^a+`, `^a$` and `^$` were rejected; they compile. Unknown letter or digit escapes, including the backreference `\1`, were silently read as the literal character; they and a trailing backslash are now an error.
  * **Parallel vector search** &mdash; `VectorIndex->SetThreads(n)` splits a search over a large index into n parts on their own threads and merges them in the same order a single scan uses, so ids, scores and ties are identical. 100,000 vectors of 128 values, L2, top 10: 66 queries/s on one thread, 315 on sixteen.
  * **Nearest neighbors and edit distance** &mdash; `KNearestNeighbors->Query` keeps the best k in a heap in one pass instead of building and sorting an object per row: 200 queries over 50,000 rows of 16 values took 15.1 s and now take 0.057 s (13 to 3,506 queries/s). A k below 1 or a Nil query returns no neighbors instead of ending with an allocation error. `TextSimilarity->LevenshteinDistance` keeps two rows instead of the full matrix (20,000 pairs: 0.68 s to 0.12 s), and a new `LevenshteinDistance(a, b, max_distance)` stops at a ceiling; `SpellChecker->Suggest` uses it.
  * **Nine new collection types and sturdier collections** &mdash; `Deque`, `MultiSet`, `UnionFind`, `Trie`, `OrderedHash`, `BiMap`, `PriorityMap` (with decrease-key), `FenwickTree` and `BloomFilter` join the collections library, with one shared merge sort for `Vector` and `CompareVector`, `Vector` insert, slice and add-all, doubling growth, and a `Stack` that no longer crashes near 500,000 entries. A `Nil` key or value lookup now reports not-found instead of ending the program. Thirteen new regression tests cover them.
  * **Library workloads are measured** &mdash; `docs/performance.md` has a new Library workloads section with the regex, vector search, nearest-neighbor, edit-distance, string and JSON timings behind these numbers, and what it did not measure.

### v2026.11.2
  * **A long linked chain no longer crashes the VM** &mdash; a `List`, `Queue`, `CompareList` or linked `Stack` of about 500,000 or more nodes exited with code 127, because marking recursed once per node and overflowed the collector thread's native stack; marking now uses a bounded per-thread work stack, and a 2-million-node chain passes with and without the JIT (`binarytrees` is unchanged)
  * **Sorting and strings are faster** &mdash; the `Vector` and `CompareVector` merge sorts skip a merge when the halves are already in order (500,000 sorted elements: about 500 ms to 80 ms, random input unchanged, still stable), and `String->Append(String)` copies strings of 32 or more characters in one block (1,000-character pieces: 1,660 ms to 292 ms)
  * **Faster image loading for convolutional networks** &mdash; `System.ML.Cnn` decodes a batch of images on several native threads and prefetches the next one, so training no longer waits on the JPEG decoder (decoding 10,000 face images: 1,107 images/s on one thread, 5,236 on eight; a 20,000-image epoch on an RTX 4090: about 30 to 34 s, now 9.1 s), with bit-identical results
  * **Result tables for a study that arrives over time** &mdash; `ResultTable` has scientific, p-value (`<0.0001`) and integer number formats, and `System.ML.Report` has a safe CSV loader, CSV tables, placeholders that read `not run yet`, figure slots and a run-status table
  * **OpenAI output cap** &mdash; the Responses API now gets `max_output_tokens` and Chat Completions `max_completion_tokens`; before, the cap was sent under a name the endpoint ignored
  * **One command finishes a release** &mdash; `tools/cicd/release_finish.sh` waits for the build, signs the installers (unattended from a DPAPI-protected password file, or at one prompt), deploys the playground and the site, and runs the release gates; the site deploy script targets the new server
  * **Topic stability is a distribution** &mdash; `docs/AI.md` explains how to read `TopicMetrics->Stability` (about 1.0 when two seeds find the same optimum, 0.67 to 0.8 when one merged two topics), measured against Gensim over 15 seed pairs

## Downloads

**Latest Release:** [v2026.11.3](https://github.com/objeck/objeck-lang/releases/latest)

| Platform | Architecture | Download |
|----------|--------------|----------|
| **Windows** | x64 | [MSI Installer](https://github.com/objeck/objeck-lang/releases/latest) / [ZIP](https://github.com/objeck/objeck-lang/releases/latest) |
| **Windows** | ARM64 | [MSI Installer](https://github.com/objeck/objeck-lang/releases/latest) / [ZIP](https://github.com/objeck/objeck-lang/releases/latest) |
| **Linux** | x64 | [DEB (Ubuntu 24.04)](https://github.com/objeck/objeck-lang/releases/latest) / [TGZ Archive](https://github.com/objeck/objeck-lang/releases/latest) |
| **Linux** | ARM64 | [DEB (Ubuntu 24.04)](https://github.com/objeck/objeck-lang/releases/latest) / [TGZ Archive](https://github.com/objeck/objeck-lang/releases/latest) |
| **macOS** | ARM64 | [TGZ Archive](https://github.com/objeck/objeck-lang/releases/latest) |
| **LSP** | All platforms | [ZIP Archive](https://github.com/objeck/objeck-lang/releases/latest) |

📦 **Alternative:** [Sourceforge](https://sourceforge.net/projects/objeck-lang/files/) • 📚 **API Docs:** [objeck.org/api/latest](https://www.objeck.org/api/latest/)

> **Note:** Windows installers are signed and timestamped (`CN=Randy Hollines`, Sectigo); the macOS `.pkg` is signed and notarized. Signing uses a hardware token and therefore happens locally after publication, so verify rather than assume — `Get-AuthenticodeSignature <file>.msi` reports `Valid` only when it really is signed. Check any download against the release's `SHA256SUMS`, which is regenerated after signing. Builds are automated on GitHub Actions runners.

### Verifying a download

Every release ships `SHA256SUMS` and an Ed25519 signature over it,
`SHA256SUMS.minisig`. Download both beside the archive:

```bash
obu verify objeck-linux-x64_<VERSION>.tgz SHA256SUMS
```

`obu` checks the signature before it reads a line of the manifest, and refuses a
manifest it cannot verify. Exit codes are distinct on purpose: `0` matches, `1`
does not match, `2` could not be checked.

The manifest matters because it is served from the same place as the assets it
describes — anyone able to replace an archive could replace the manifest to match.
The Linux and macOS archives carry no platform signature at all, so for those it
is the only integrity claim there is. The signature is what an attacker cannot
reproduce.

**The release public key**, for checking out of band — with `minisign`, or against
what `obu` reports:

```
untrusted comment: minisign public key 8237C6B17C7EA6B9
RWS5pn58scY3gv7X3EL/fJaOQli1V0HDwwFVt+2IWqPXBxvE4U1UhxHC
```

Key ID `8237C6B17C7EA6B9`. The same key is committed at
[`core/release/objeck-release.pub`](core/release/objeck-release.pub) and served,
independently of this repository, from
**<https://www.objeck.org/objeck-release.pub>**. A key you can only obtain from
the thing you are trying to verify proves nothing, which is why it is in more
than one place. Check that they agree:

```bash
curl -sS https://www.objeck.org/objeck-release.pub | sha256sum
# 864c6a9951a539ec356520dbeca4f97ec7a5fc9f4fd31d881f659f59bf7a6c04
```

That digest is of the whole 113-byte file, both lines, newline included. The
site serves `.pub` as `application/x-mspublisher`, so a browser offers to
download it rather than showing it; for reading in a browser there is an
identical copy at
[`objeck-release.pub.txt`](https://www.objeck.org/objeck-release.pub.txt),
served as text and with the same digest. Design and
key-rotation procedure: [`docs/release_integrity.md`](docs/release_integrity.md).

## See It In Action

### HTTP/2 Client
```ruby
use Web.HTTP;

# Persistent connection — multiple requests share one TLS session
client := Http2Client->New("httpbin.org");
resp := client->Get("/get");
"Status: {$resp->GetCode()}"->PrintLine();    # Status: 200

body := "{\"lang\":\"objeck\"}"->ToByteArray();
resp2 := client->Post("/post", body, "application/json");
client->Close();

# One-liner for quick requests
resp := Http2Client->QuickGet(Url->New("https://httpbin.org/get"));
```

### HTTP/3 / QUIC Client
```ruby
use Web.HTTP;

# QUIC over UDP — zero round-trip connection on repeat visits
client := Http3Client->New("quic.nginx.org");
resp := client->Get("/");
"Status: {$resp->GetCode()}"->PrintLine();    # Status: 200
client->Close();

# One-liner
resp := Http3Client->QuickGet(Url->New("https://quic.nginx.org/"));
```

### AI Integration
```ruby
# OpenAI Realtime API - get text AND audio
response := Realtime->Respond("How many James Bond movies?",
                              "gpt-4o-realtime-preview", token);
text := response->GetFirst();
audio := response->GetSecond();
Mixer->PlayPcm(audio->Get(), 22050, AudioFormat->SDL_AUDIO_S16LSB, 1);
```

### Face Recognition
```ruby
# SCRFD detector + ArcFace R50 embeddings (InsightFace buffalo_l)
session := FaceSession->New("det_10g.onnx", "w600k_r50.onnx");
r1 := session->Recognize(img1_bytes, 0.5);
r2 := session->Recognize(img2_bytes, 0.5);
faces1 := r1->GetResults(); faces2 := r2->GetResults();
sim := FaceSession->Compare(faces1[0]->GetEmbedding(), faces2[0]->GetEmbedding());
"Same person: {$(sim > 0.35)}"->PrintLine();
```

### Computer Vision
```ruby
# OpenCV: grayscale, blur and edge detection
image := Image->Load("photo.jpg");
gray := image->CvtColor(ColorConversionCodes->BGR2GRAY);
edges := gray->GaussianBlur(5, 5, 0.0)->Canny(50, 150);
edges->Save("edges.jpg");
```

### Natural Language Processing
```ruby
# Sentiment analysis and TF-IDF
text := "This product is absolutely wonderful!";
sentiment := SentimentAnalyzer->Classify(text);  # "positive"

# Train TF-IDF on documents
docs := ["cats are pets", "dogs are pets", "birds can fly"];
tfidf := TF_IDF->New();
tfidf->Fit(docs);
vector := tfidf->Transform("cats and dogs");  # [0.47, 0.0, 0.47, ...]
```

[🎯 More examples](https://github.com/objeck/objeck-lang/tree/master/programs/examples)

## Language Features

**Object-Oriented**
- Inheritance, interfaces, generics
- Type inference and boxing
- Reflection and dependency injection
- [See OOP examples →](docs/FEATURES.md#oop)

**Functional**
- Closures and lambda expressions
- First-class functions
- [See functional examples →](docs/FEATURES.md#functional)

**Strings & Formatting**
- Interpolation with expressions: `"{$i + 1}"`, `"{$obj->M()}"`
- Format specifiers: `"{$pi:.2}"`, `"{$n:05}"`, `"{$v:x}"`
- Positional templates: `String->Format("{0} = {1}", a, b)`
- [See string features →](docs/FEATURES.md#strings)

**Platform Support**
- Unicode, file I/O, sockets, named pipes
- Threading with mutexes
- [See platform features →](docs/FEATURES.md#platform)

## Libraries

**AI & Machine Learning** — [📖 AI Developer Guide](https://www.objeck.org/ai_guide.html) · [GitHub source](docs/AI.md) · [🤖 Getting Models](docs/MODELS.md)
- [OpenAI](https://github.com/objeck/objeck-lang/blob/master/core/compiler/lib_src/openai.obs) — chat, vision, realtime audio, image generation, embeddings, moderation, batch
- [Gemini](https://github.com/objeck/objeck-lang/blob/master/core/compiler/lib_src/gemini.obs) — chat, vision, search grounding, files, context caching, batch embeddings
- [Anthropic](https://github.com/objeck/objeck-lang/blob/master/core/compiler/lib_src/anthropic.obs) — Claude chat with system prompts, tool use, streaming and structured output; tested against recorded responses and a local mock server, not the live API
- [LLM utilities](https://github.com/objeck/objeck-lang/blob/master/core/compiler/lib_src/llm_util.obs) — prompt templates, offline token estimates, a context window, JSON Schema validation, and structured calls that retry with the validation errors; works with every client above, tested with no network or key
- [Agent](https://github.com/objeck/objeck-lang/blob/master/core/compiler/lib_src/agent.obs) — provider-neutral tool-use loop: hand a chat model Objeck functions as tools (schema-checked, with allow/deny lists and an approval callback) and get the final answer; adapters for Anthropic, OpenAI, Gemini and Ollama; tested offline against a scripted model and recorded bodies
- [Actor](https://github.com/objeck/objeck-lang/blob/master/core/compiler/lib_src/actor.obs) — actors with supervision for multi-agent programs: mailboxes with backpressure, monitors, links, `Call` with a timeout, supervisors (one-for-one, one-for-all, rest-for-one, restart intensity); supervises `API.Agent` agents through `actor_agents`; recovers from catchable errors only, not from VM faults
- [MCP](https://github.com/objeck/objeck-lang/blob/master/core/compiler/lib_src/mcp.obs) — Model Context Protocol server (stdio and streamable HTTP) and client: tools, resources, prompts, cancellation and progress, and a bridge that hands a server's tools to the Claude, OpenAI and Gemini clients; tested against a scripted Python client, not against Claude Desktop
- [RAG kit](https://github.com/objeck/objeck-lang/blob/master/core/compiler/lib_src/rag.obs) — retrieval-augmented generation: chunking, a local vector index, grounded prompts with [n] citations and a pipeline; runs offline, with adapters for BERT, Ollama, OpenAI, Gemini and Anthropic
- [Ollama](https://github.com/objeck/objeck-lang/blob/master/core/compiler/lib_src/ollama.obs) — local LLM chat, vision, and embeddings; recommended models: `llama3.2`, `phi3`, `llava` ([get models →](docs/MODELS.md#ollama-models))
- [LLM evaluation](https://github.com/objeck/objeck-lang/blob/master/tools/llm_eval) — measure how well any model writes Objeck (or answers any task with a checkable result): a 40-task suite with verified reference solutions, N attempts per task compiled and run by the real compiler, compile rate, pass@k, error categories and a paired-bootstrap comparison of two models; the harness itself is tested against scripted models, no key needed
- [NLP](https://github.com/objeck/objeck-lang/blob/master/core/compiler/lib_src/nlp.obs) — tokenization, TF-IDF, text similarity, sentiment analysis
- [OpenCV](https://github.com/objeck/objeck-lang/blob/master/core/compiler/lib_src/opencv.obs) — computer vision: detection, transforms, video
- [ONNX Runtime](https://github.com/objeck/objeck-lang/blob/master/core/compiler/lib_src/onnx.obs) — local ML inference: YOLO, ResNet, DeepLab, OpenPose, Phi-3, face recognition ([get models →](docs/MODELS.md#onnx-models), [model reference](core/lib/onnx/MODELS.md))
- [Telemetry](https://github.com/objeck/objeck-lang/blob/master/core/compiler/lib_src/telemetry.obs) — per-model, per-device inference timing on a monotonic clock, bounded-memory latency percentiles, host, collector and GPU samplers, an energy estimate, saved profiles and a contention helper (`-lib telemetry,gen_collect,json`)
- [Face Recognition](https://github.com/objeck/objeck-lang/blob/master/core/lib/onnx/README.md) — SCRFD detector + ArcFace R50 (InsightFace buffalo_l)
- [Phi-3 / Phi-3 Vision](https://github.com/objeck/objeck-lang/tree/master/programs/frameworks/opencv_onnx) — local SLM text and multimodal inference

**Web & Networking**
- HTTP/1.1 [server](https://github.com/objeck/objeck-lang/blob/master/core/compiler/lib_src/net_secure.obs)/[client](https://github.com/objeck/objeck-lang/blob/master/core/compiler/lib_src/net_secure.obs), [OAuth](https://github.com/objeck/objeck-lang/blob/master/core/compiler/lib_src/net_common.obs)
- [HTTP/2](https://github.com/objeck/objeck-lang/blob/master/core/compiler/lib_src/net_h2.obs) — multiplexed TLS client via nghttp2
- [HTTP/3 / QUIC](https://github.com/objeck/objeck-lang/blob/master/core/compiler/lib_src/net_quic.obs) — UDP-based client; ngtcp2 + nghttp3 on Linux/macOS, WinHTTP over MsQuic on Windows 11+
- [RSS](https://github.com/objeck/objeck-lang/blob/master/core/compiler/lib_src/rss.obs)

**Data**
- [JSON](https://github.com/objeck/objeck-lang/blob/master/core/compiler/lib_src/json.obs) (hierarchical + [streaming](https://github.com/objeck/objeck-lang/blob/master/core/compiler/lib_src/json_stream.obs)), [XML](https://github.com/objeck/objeck-lang/blob/master/core/compiler/lib_src/xml.obs), [CSV](https://github.com/objeck/objeck-lang/blob/master/core/compiler/lib_src/csv.obs)
- [SQL/ODBC](https://github.com/objeck/objeck-lang/blob/master/core/compiler/lib_src/odbc.obs), [In-memory queries](https://github.com/objeck/objeck-lang/blob/master/core/compiler/lib_src/query.obs)
- [Collections](https://github.com/objeck/objeck-lang/blob/master/core/compiler/lib_src/gen_collect.obs)

**Graphics & Gaming**
- [3D Graphics (OpenGL)](https://github.com/objeck/objeck-lang/blob/master/core/compiler/lib_src/sdl_gl.obs) — OpenGL 3.3 core: shaders, meshes, textures, cameras, lighting, shadows ([setup & examples →](docs/opengl.md))
- [2D Gaming (SDL)](https://github.com/objeck/objeck-lang/blob/master/core/compiler/lib_src/sdl_game.obs)

**Other**
- [Encryption](https://github.com/objeck/objeck-lang/blob/master/core/compiler/lib_src/cipher.obs), [Regex](https://github.com/objeck/objeck-lang/blob/master/core/compiler/lib_src/regex.obs)

## Architecture

[![Objeck toolchain architecture](docs/images/objeck-architecture.svg)](docs/architecture.md)

One compiler front end serves `obc` and the language server; the VM interprets, then JIT-compiles hot methods for AMD64 and ARM64; libraries reach SDL2, ONNX Runtime, OpenCV, mbedTLS and more through native modules. [More diagrams](docs/architecture.md).

## Development

**Modern tooling and practices:**
- 🤖 **Claude Code** for pair programming, debugging, and refactoring
- 🔄 **CI/CD**: Fully automated build, test, sign, and release pipeline (GitHub Actions)
  - ✅ Every push triggers multi-platform builds (Windows, Linux, macOS)
  - ✅ macOS installers signed and notarized in CI (Windows signing is a local step — hardware token)
  - ✅ One-tag releases: `git tag v2026.2.1` → automated distribution in 60 minutes
  - ✅ Parallel builds across 6 platforms (x64/ARM64)
  - 📖 [Release Process Documentation](docs/release_process.md) • [CI/CD Architecture](docs/CI_CD.md) • [System Architecture](docs/architecture.md)
- 🔍 **Quality**: CodeQL security scanning + gitleaks secret scanning
- 🧪 **Testing**: 350+ tests across 3 suites (regression, comprehensive, deploy)
  - **Regression suite**: 10 focused tests for critical functionality
  - **Comprehensive suite**: 323+ tests for full language validation
  - **Deploy suite**: 17 real-world usage examples
  - Full cross-platform coverage (Windows/Linux/macOS, x64/ARM64)

**Editor Support:**
- LSP plugins for [VSCode, Sublime, Kate, Neovim, Emacs, Helix, and more](tools/lsp/)
- REPL for interactive development
- API docs at [objeck.org](https://www.objeck.org)

**📚 [Testing Documentation](programs/TESTING.md)** • **🧪 [Regression Tests](programs/regression/)** • **📊 [Performance & Benchmarks](docs/performance.md)**

## Resources

- 📖 [Documentation](https://www.objeck.org)
- 🏗️ [Architecture](docs/architecture.md) — a one-page toolchain diagram, plus Mermaid diagrams of the compiler, VM, JIT, libraries and CI/CD
- 🎯 [Examples](https://github.com/objeck/objeck-lang/tree/master/programs)
- 💬 [Discussions](https://github.com/objeck/objeck-lang/discussions)
- 🐛 [Issues](https://github.com/objeck/objeck-lang/issues)













