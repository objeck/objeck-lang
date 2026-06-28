<p align="center">
  <img src='https://github.com/objeck/objeck-lang/blob/master/core/lib/code_doc/templates/resources/objeck-logo-alt.png' height="125px"/>
</p>

<p align="center">
<strong>Object-oriented • JIT-compiled • AI-native • Robust APIs
</p>

<hr/>

<p align="center">
  <a href="https://github.com/objeck/objeck-lang/actions/workflows/codeql.yml"><img src="https://github.com/objeck/objeck-lang/actions/workflows/codeql.yml/badge.svg" alt="GitHub CodeQL"></a>
  <a href="https://github.com/objeck/objeck-lang/actions/workflows/ci-build.yml"><img src="https://github.com/objeck/objeck-lang/actions/workflows/ci-build.yml/badge.svg" alt="CI Build"></a>
  <a href="https://github.com/objeck/objeck-lang/actions/workflows/release-build.yml"><img src="https://github.com/objeck/objeck-lang/actions/workflows/release-build.yml/badge.svg" alt="Release Build"></a>
  <a href="https://github.com/objeck/objeck-lang/releases"><img src="https://img.shields.io/badge/release-v2026.6.4-blue" alt="Latest Release"></a>
</p>

## Why Objeck?

**Built for modern development:**
- 🚀 **JIT-compiled** for performance (ARM64/AMD64)
- 🤖 **AI-native**: OpenAI, Gemini, Ollama, ONNX, OpenCV — no third-party packages
- 🌐 **Network-complete**: HTTP/1.1 · HTTP/2 · HTTP/3/QUIC · WebSocket · DTLS — all standard library
- 💻 **Developer-friendly**: REPL shell, LSP plugins for VSCode/Sublime/Kate, DAP debugger
- 🌍 **Cross-platform**: Linux, macOS, Windows (x64 + ARM64/RPI)
- 🔧 **Full-featured**: Threads, generics, closures, reflection, serialization

**Perfect for:**
AI/ML prototyping • Computer vision • Web services • Real-time applications • Game development

## Try It Online

👉🏽 [Playground](https://playground.objeck.org) — 33 demos across 7 categories, Monaco editor, no install required.

## Quick Start

```bash
# Install (example for macOS/Linux)
curl -LO https://github.com/objeck/objeck-lang/releases/download/v2026.6.3/objeck-linux-x64_2026.6.3.tgz
tar xzf objeck-linux-x64_2026.6.3.tgz
export PATH=$PATH:./objeck-lang/bin
export OBJECK_LIB_PATH=./objeck-lang/lib

# Hello World
echo 'class Hello {
  function : Main(args : String[]) ~ Nil {
    "Hello World"->PrintLine();
  }
}' > hello.obs

# Compile and run (modern syntax)
obc hello && obr hello
```

📖 **Full docs**: [objeck.org](https://www.objeck.org)
💡 **Examples**: [github.com/objeck/objeck-lang/programs](https://github.com/objeck/objeck-lang/tree/master/programs)

## What's New

### v2026.6.3 ✅
  * **Generational minor garbage collection** — minor (nursery) collection is now enabled: a nursery-full collection scans only the remembered set plus roots and recycles the young generation without sweeping the old generation, falling back to a full major GC under old-gen pressure. JIT and interpreter reference stores emit the write barrier on AMD64 and ARM64, and the nursery is now zeroed at allocation time instead of inside the stop-the-world pause. See [performance →](docs/performance.md)
  * **Closure ergonomics** — three quality-of-life additions for function references: call a `FuncRef` directly with `v()` (no explicit `->Call()`); write bare lambdas with an inferred return type — `\(x) => x * 2` — that auto-wrap into `FuncRef<R>` when assigned, returned, passed as a method argument, or stored as a collection element; and give a lambda a block body (`\(x) => { ... }`). A multi-capture closure heap-corruption bug is fixed — captures now use closure-local ids
  * **New `System.Concurrency` library** — structured concurrency with `TaskScope`, `Task`, and `Monitor`, plus `runtime.*` process/GC/CPU diagnostics (GC pause, promotion, allocation rate, lock contention, thread/STW/nursery counters) read through `Runtime->GetProperty("runtime.…")`

### v2026.6.2
  * **Major JIT & GC performance work** — the cooperative stop-the-world GC safepoint poll (new in v2026.6.1) is now nearly free in JIT'd code: an inline flag test that only calls the collector when a collection is active, reading `&stw_active` from a register cached at the prologue (R12/X19) and emitted only at loop back-edges. `fannkuchredux` roughly halved (~59s → ~31s), recovering the full regression on AMD64 and ARM64. Closure / function-reference calls (`DYN_MTHD_CALL`) now **auto-JIT** on both architectures — `spectralnorm` reaches `native`-level speed once warm (43s interpreted → 0.46s at n=2000, matching the hand-`native` kernel). Nursery allocation for `NEW_OBJ_INST` is inlined on AMD64, and the interpreter gains a float fast-path. See [performance →](docs/performance.md)
  * **JIT correctness hardening** — a sweep of float-codegen and tail-call bugs surfaced by forcing JIT (`OBJECK_JIT_THRESHOLD=1`): AMD64 `Floor`/`Ceil`/`ArcTan` codegen and two latent `DYN_MTHD_CALL` miscompiles; ARM64 transcendental/round cached-local operands, dropped libc float result/argument, working-stack registers clobbered across inlined float calls, and an `imm19` backpatch SIGILL (`ml_gbt`); and a TCO deferred-load corruption (e.g. `return Gcd(b, a%b)`) on both architectures; and an ARM64 negative-offset load bug that crashed when a JIT-compiled closure captured in a collection (`Vector<FuncRef>`) was invoked — its memory encoders couldn't represent a negative displacement and read the wrong stack slot, now routed through a signed-offset `LDUR`/`STUR` helper (x64 was never affected). The full ARM64 suite is now green at `OBJECK_JIT_THRESHOLD=1`
  * **UTF-8 in any locale** — `obc` reading UTF-8 source and `obr` loading/printing UTF-8 strings no longer break under a `C`/non-UTF-8 process locale; `sys.h` now uses systemic locale-independent UTF-8 codecs instead of `mbstowcs`/`wcstombs`
  * **VM shutdown race fixed** — worker threads are quiesced before program teardown, removing a JIT-shutdown thread race
  * **`Int->MinSize()`** now returns `INT64_MIN` (was `INT64_MAX` — the float `2->Pow(63)` path saturated on conversion)
  * **Native cross-language perf gate (CI)** — a non-Docker harness measures Objeck against Python/Ruby/LuaJIT/Java with committed baseline ratios, so performance regressions are caught automatically

### v2026.6.1
  * **String interpolation — expressions, format specifiers, and `String->Format`** — `"{$...}"` now accepts arbitrary expressions (`"{$i + 1}"`, `"{$a * b - c}"`, `"{$x > y}"`), not just variables and method calls. Inline format specifiers use Python/.NET colon syntax for precision, width, alignment, and radix — `"{$pi:.2}"`, `"{$n:05}"`, `"{$s:<10}"`, `"{$v:x}"`, `"{$v:b}"` — and the new `String->Format("{0} = {1}", a, b)` adds positional templating with `{{`/`}}` escaping. See [string features →](docs/FEATURES.md#strings)
  * **Generics — bounds, compound/F-bounds, and variance** — type parameters gain compound bounds `T : A & B` (a concrete argument must satisfy every bound), F-bounded constraints `T : Compare<T>`, and declaration-site variance: `out T` (covariant, e.g. `Producer<Dog>` usable as `Producer<Animal>`) and `in T` (contravariant). Variance is checked soundly in both directions and preserved across the `.obl` library boundary; the stdlib's read-only iterators are now covariant. Existing invariant generics and syntax are unchanged (`out` stays a usable identifier), and generic type-mismatch errors now print readable types like `Hash<String, IntRef>`
  * **Multithreaded garbage collection** — the generational collector is now cooperative stop-the-world: mutator threads park at safepoints (interpreter dispatch, JIT loop back-edges on AMD64 and ARM64, allocation, and blocking `join`/`sleep`/socket I/O) so the collector always marks a complete, stable root set. Fixes freed-live-object corruption and use-after-free under heavy thread churn, plus a JIT-loop collector deadlock; single-threaded programs never park
  * **Debugger overhaul — command line and VS Code** — the `obd` debugger gains frame navigation (`frame`/`up`/`down`) with `locals` to inspect any caller's variables, live editing (`set x = 5`), breakpoints by method (`b Class->Method`), temporary and conditional breakpoints with ignore counts, data breakpoints (`watch`), and run-to-line (`until`). The VS Code adapter (DAP) adds editing variables from the panel, function breakpoints, logpoints, in-process restart, and exception breakpoints. CLI conditional breakpoints (`b file:line if <expr>`) now parse correctly, and variables after a `Float` slot read the right value
  * **Secure sockets verify certificates by default** — TLS and DTLS clients (TCP, HTTP/2, HTTP/3, DTLS) now validate the certificate chain and hostname by default instead of accepting any certificate; set `OBJECK_TLS_INSECURE_SKIP_VERIFY=1` to opt into self-signed/dev servers
  * **Serialization correctness** — 64-bit `Int` values and `Int[]` elements (standalone and object-nested) no longer truncate to 32 bits or drop half the array; function-reference object fields now (de)serialize without desyncing the fields after them. *Note:* the serialized integer wire format widened to 8 bytes
  * **Memory-safety hardening** — object deserializers bounds-check attacker-supplied lengths/offsets and 64-bit sizes before reading; fixed a heap overflow in the `Char[]` read traps (file/stdin/pipe/socket/SSL/DTLS) reachable from ordinary code via a large offset
  * **Compiler fixes** — constant propagation no longer keeps a stale literal after a slot is reassigned from a non-constant expression; LICM no longer hoists integer divide/modulo out of a zero-trip loop (which turned a never-evaluated division into a divide-by-zero trap)
  * **New `Web.Server` library** (`-lib web_server`) — a lightweight HTTP server bundle for simple request/response and multipart handling
  * **ONNX** — on macOS the compiled CoreML model is cached across runs (`~/Library/Caches/objeck-onnx`), cutting cold session loads from seconds to milliseconds (~35× faster warm starts); fixed a dangling `TypeInfo` that could mis-detect a model's input dtype/shape
  * **macOS launcher** — portable app bundles now resolve their own location instead of trusting the working directory, so they launch correctly from Finder or any directory
  * **Reproducible builds** — compiling unchanged library source now produces byte-identical `.obl` files (deterministic anonymous-class naming), and Windows/ARM64 build warnings and a `NativeCode` ODR violation were cleared


[📋 Full changelog](CHANGELOG.md) • [🗺️ Roadmap](ROADMAP.md) • [📝 Editor & IDE setup](docs/editors.md)

## Downloads

**Latest Release:** [v2026.6.3](https://github.com/objeck/objeck-lang/releases/latest)

| Platform | Architecture | Download |
|----------|--------------|----------|
| **Windows** | x64 | [MSI Installer](https://github.com/objeck/objeck-lang/releases/latest) / [ZIP](https://github.com/objeck/objeck-lang/releases/latest) |
| **Windows** | ARM64 | [MSI Installer](https://github.com/objeck/objeck-lang/releases/latest) / [ZIP](https://github.com/objeck/objeck-lang/releases/latest) |
| **Linux** | x64 | [TGZ Archive](https://github.com/objeck/objeck-lang/releases/latest) |
| **Linux** | ARM64 | [TGZ Archive](https://github.com/objeck/objeck-lang/releases/latest) |
| **macOS** | ARM64 | [TGZ Archive](https://github.com/objeck/objeck-lang/releases/latest) |
| **LSP** | All platforms | [ZIP Archive](https://github.com/objeck/objeck-lang/releases/latest) |

📦 **Alternative:** [Sourceforge](https://sourceforge.net/projects/objeck/files/) • 📚 **API Docs:** [objeck.org/api/latest](https://www.objeck.org/api/latest/)

> **Note:** All Windows installers are digitally signed. Releases are fully automated via CI/CD and built on GitHub Actions runners.

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
# OpenCV face detection
detector := FaceDetector->New("haarcascade_frontalface_default.xml");
faces := detector->Detect(image);
faces->Size()->PrintLine();  # "5 faces detected"
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
- [Ollama](https://github.com/objeck/objeck-lang/blob/master/core/compiler/lib_src/ollama.obs) — local LLM chat, vision, and embeddings; recommended models: `llama3.2`, `phi3`, `llava` ([get models →](docs/MODELS.md#ollama-models))
- [NLP](https://github.com/objeck/objeck-lang/blob/master/core/compiler/lib_src/nlp.obs) — tokenization, TF-IDF, text similarity, sentiment analysis
- [OpenCV](https://github.com/objeck/objeck-lang/blob/master/core/compiler/lib_src/opencv.obs) — computer vision: detection, transforms, video
- [ONNX Runtime](https://github.com/objeck/objeck-lang/blob/master/core/compiler/lib_src/onnx.obs) — local ML inference: YOLO, ResNet, DeepLab, OpenPose, Phi-3, face recognition ([get models →](docs/MODELS.md#onnx-models))
- [Face Recognition](https://github.com/objeck/objeck-lang/blob/master/core/lib/onnx/README.md) — SCRFD detector + ArcFace R50 (InsightFace buffalo_l)
- [Phi-3 / Phi-3 Vision](https://github.com/objeck/objeck-lang/tree/master/programs/frameworks/opencv_onnx) — local SLM text and multimodal inference

**Web & Networking**
- HTTP/1.1 [server](https://github.com/objeck/objeck-lang/blob/master/core/compiler/lib_src/net_secure.obs)/[client](https://github.com/objeck/objeck-lang/blob/master/core/compiler/lib_src/net_secure.obs), [OAuth](https://github.com/objeck/objeck-lang/blob/master/core/compiler/lib_src/net_common.obs)
- [HTTP/2](https://github.com/objeck/objeck-lang/blob/master/core/compiler/lib_src/net_h2.obs) — multiplexed TLS client via nghttp2
- [HTTP/3 / QUIC](https://github.com/objeck/objeck-lang/blob/master/core/compiler/lib_src/net_quic.obs) — UDP-based client via ngtcp2 + nghttp3
- [RSS](https://github.com/objeck/objeck-lang/blob/master/core/compiler/lib_src/rss.obs)

**Data**
- [JSON](https://github.com/objeck/objeck-lang/blob/master/core/compiler/lib_src/json.obs) (hierarchical + [streaming](https://github.com/objeck/objeck-lang/blob/master/core/compiler/lib_src/json_stream.obs)), [XML](https://github.com/objeck/objeck-lang/blob/master/core/compiler/lib_src/xml.obs), [CSV](https://github.com/objeck/objeck-lang/blob/master/core/compiler/lib_src/csv.obs)
- [SQL/ODBC](https://github.com/objeck/objeck-lang/blob/master/core/compiler/lib_src/odbc.obs), [In-memory queries](https://github.com/objeck/objeck-lang/blob/master/core/compiler/lib_src/query.obs)
- [Collections](https://github.com/objeck/objeck-lang/blob/master/core/compiler/lib_src/gen_collect.obs)

**Other**
- [Encryption](https://github.com/objeck/objeck-lang/blob/master/core/compiler/lib_src/cipher.obs), [Regex](https://github.com/objeck/objeck-lang/blob/master/core/compiler/lib_src/regex.obs)
- [2D Gaming (SDL)](https://github.com/objeck/objeck-lang/blob/master/core/compiler/lib_src/sdl_game.obs)

## Development

**Modern tooling and practices:**
- 🤖 **Claude Code** for pair programming, debugging, and refactoring
- 🔄 **CI/CD**: Fully automated build, test, sign, and release pipeline (GitHub Actions)
  - ✅ Every push triggers multi-platform builds (Windows, Linux, macOS)
  - ✅ Automated code signing for Windows installers
  - ✅ One-tag releases: `git tag v2026.2.1` → automated distribution in 60 minutes
  - ✅ Parallel builds across 6 platforms (x64/ARM64)
  - 📖 [Release Process Documentation](docs/release_process.md) • [CI/CD Architecture](docs/CI_CD.md) • [System Architecture](docs/architecture.md)
- 🔍 **Quality**: Coverity static analysis + CodeQL security scanning
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
- 🏗️ [Architecture](docs/architecture.md) — Mermaid diagrams covering compiler, VM, JIT, libraries, and CI/CD
- 🎯 [Examples](https://github.com/objeck/objeck-lang/tree/master/programs)
- 💬 [Discussions](https://github.com/objeck/objeck-lang/discussions)
- 🐛 [Issues](https://github.com/objeck/objeck-lang/issues)













