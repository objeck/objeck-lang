<p align="center">
  <img src='https://github.com/objeck/objeck-lang/blob/master/core/lib/code_doc/templates/resources/objeck-logo-alt.png' height="125px"/>
</p>

<p align="center">
<strong>Object-oriented • JIT-compiled • AI-native • Network-complete</strong><br>
Modern programming language — from HTTP/3 to GPT-4, it's all standard library
</p>

<hr/>

<p align="center">
  <a href="https://github.com/objeck/objeck-lang/actions/workflows/codeql.yml"><img src="https://github.com/objeck/objeck-lang/actions/workflows/codeql.yml/badge.svg" alt="GitHub CodeQL"></a>
  <a href="https://github.com/objeck/objeck-lang/actions/workflows/ci-build.yml"><img src="https://github.com/objeck/objeck-lang/actions/workflows/ci-build.yml/badge.svg" alt="CI Build"></a>
  <a href="https://github.com/objeck/objeck-lang/actions/workflows/release-build.yml"><img src="https://github.com/objeck/objeck-lang/actions/workflows/release-build.yml/badge.svg" alt="Release Build"></a>
  <a href="https://github.com/objeck/objeck-lang/releases"><img src="https://img.shields.io/github/v/release/objeck/objeck-lang?sort=semver" alt="Latest Release"></a>
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

No installation needed - write, compile, and run Objeck code directly in your browser:

👉🏽 Interactive [playground](https://playground.objeck.org) with 33 demo programs across 7 categories with Monaco editor and syntax highlighting.

## Quick Start

```bash
# Install (example for macOS/Linux)
curl -LO https://github.com/objeck/objeck-lang/releases/download/v2026.5.0/objeck-linux-x64_2026.5.0.tgz
tar xzf objeck-linux-x64_2026.5.0.tgz
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

**Web Playground** — [Try Objeck in your browser](https://playground.objeck.org). Code runs in sandboxed Docker containers on a dedicated server. Includes 33 demos covering the language basics, OOP, algorithms, collections, data processing, and more.

**v2026.5.1**
  * **HTTP/2 client** — `Http2Client` with persistent TLS connections, custom headers, GET/POST/PUT/DELETE, and `QuickGet`/`QuickPost` one-liners (nghttp2 + ALPN)
  * **HTTP/3 / QUIC client** — `Http3Client` over UDP with connection reuse and the same `Quick*` API (ngtcp2 + nghttp3 + GnuTLS)
  * **HTTP/1.1 improvements** — PATCH method, redirect handling fixes, retry parity across `HttpClient`/`HttpsClient`
  * **OpenAI Moderation** — `Moderation->Check()` returns per-category flags and confidence scores
  * **OpenAI Batch** — `Batch->Create()`/`Get()` for async 50%-cost batch requests (up to 50k at a time)
  * **Gemini Files API** — upload, list, get, and delete files via `FileManager`
  * **Gemini Context Caching** — `CachedContent->Create()` for server-side prompt caching with configurable TTL
  * **Gemini Search Grounding** — `Model->GenerateContentWithGrounding()` anchors responses in live Google Search results
  * **Gemini Batch Embeddings** — `Model->BatchEmbedContent()` embeds multiple texts in one round-trip
  * **WebSocket hardening** — 8 bug fixes + bulk `ReadBuffer` I/O replacing per-byte reads
  * **MCP server fixes** — hang on shutdown and crash-on-stop resolved; regression tests added
  * **Socket reliability** — `SO_REUSEADDR` on `TCPSocketServer::Bind()` survives TIME_WAIT; `IPSocket::Open()` falls through to next address on `socket()` failure
  * **Security hardening** — LSP concurrent-request mutex; scrfd tensor bounds check; `conf_threshold` NaN/range guard; `WinWriteWide` DWORD truncation guard
  * **MSVC optimizations** — Release build improvements for VM and compiler

**v2026.5.0** ✅
  * **Face recognition** — new `FaceSession` API with SCRFD 10G-KPS detector + ArcFace R50 512-dim embeddings (InsightFace buffalo_l). Cross-platform: DirectML (Windows), CPU/CUDA (Linux), CoreML (macOS). No extra native libs required.
  * **Windows emoji** — full Unicode supplementary plane output (emoji and other non-BMP characters) now works correctly in cmd.exe and Windows Terminal via `WriteConsoleW`
  * **LSP enhancements** — typeHierarchy, selectionRange, workspace/symbol, foldingRange, documentHighlight, go-to-type-definition; hover correctness and non-determinism fixes
  * **ARM64 JIT** — fixed EXT_LIB_FUNC_CALL crash; macOS ONNX build and CodeQL fixes
  * **`OBJECK_JIT_DISABLE`** — new boolean env var for cleanly disabling auto-JIT at startup

**v2026.4.3** 
  * **DAP debugger hover** — hovering an object shows `ClassName { field=val, ... }` with one-level instance field expansion via `FormatObjectForDap`
  * **DAP instance/class variable scopes** — Variables pane now shows separate Locals, Instance, and Class scopes with correct memory mapping
  * **DAP stepping + crash fixes** — fixed step-into crash, step-over/step-out scoping, stdout corruption, disconnect access violation, and variable display
  * **Editor setup refresh** — updated VS Code, Sublime Text, and gvim/Vim DAP+LSP setup for Windows, Linux, and macOS
  * **LSP crash fixes** — null guards for `textDocument/codeAction` with inferred locals, hover position fix (1-based to 0-based)
  * **Configurable JIT threshold** — auto-JIT invocation count can now be tuned
  * Fixed JIT S2F callback param count causing segfault on `String:ToFloat`
  * Hardened HTTPS client against null `ReadLine` on connection failures

[📋 Full changelog](CHANGELOG.md) • [🗺️ Roadmap](ROADMAP.md) • [📝 Editor & IDE setup](docs/editors.md)

## Downloads

**Latest Release:** [v2026.5.0](https://github.com/objeck/objeck-lang/releases/latest)

| Platform | Architecture | Download |
|----------|--------------|----------|
| **Windows** | x64 | [MSI Installer](https://github.com/objeck/objeck-lang/releases/latest) / [ZIP](https://github.com/objeck/objeck-lang/releases/latest) |
| **Windows** | ARM64 | [MSI Installer](https://github.com/objeck/objeck-lang/releases/latest) / [ZIP](https://github.com/objeck/objeck-lang/releases/latest) |
| **Linux** | x64 | [TGZ Archive](https://github.com/objeck/objeck-lang/releases/latest) |
| **Linux** | ARM64 | [TGZ Archive](https://github.com/objeck/objeck-lang/releases/latest) |
| **macOS** | ARM64 (M1/M2/M3) | [TGZ Archive](https://github.com/objeck/objeck-lang/releases/latest) |
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

**Platform Support**
- Unicode, file I/O, sockets, named pipes
- Threading with mutexes
- [See platform features →](docs/FEATURES.md#platform)

## Libraries

**AI & Machine Learning** — [📖 Full AI Guide](docs/AI.md)
- [OpenAI](https://github.com/objeck/objeck-lang/blob/master/core/compiler/lib_src/openai.obs), [Gemini](https://github.com/objeck/objeck-lang/blob/master/core/compiler/lib_src/gemini.obs), [Ollama](https://github.com/objeck/objeck-lang/blob/master/core/compiler/lib_src/ollama.obs)
- [NLP](https://github.com/objeck/objeck-lang/blob/master/core/compiler/lib_src/nlp.obs) (tokenization, TF-IDF, text similarity, sentiment analysis)
- [OpenCV](https://github.com/objeck/objeck-lang/blob/master/core/compiler/lib_src/opencv.obs) (computer vision)
- [ONNX Runtime](https://github.com/objeck/objeck-lang/blob/master/core/compiler/lib_src/onnx.obs) (cross-platform ML inference — YOLO, ResNet, DeepLab, OpenPose, Phi-3, face recognition)
- [Face Recognition](https://github.com/objeck/objeck-lang/blob/master/core/lib/onnx/README.md) (SCRFD detector + ArcFace R50 embeddings, InsightFace buffalo_l)
- [Phi-3 / Phi-3 Vision](https://github.com/objeck/objeck-lang/tree/master/programs/frameworks/opencv_onnx) (local SLM text and vision inference)

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
- LSP plugins for [VSCode, Sublime, Kate, and more](https://github.com/objeck/objeck-lsp)
- REPL for interactive development
- API docs at [objeck.org](https://www.objeck.org)

**📚 [Testing Documentation](programs/TESTING.md)** • **🧪 [Regression Tests](programs/regression/)** • **📊 [Performance & Benchmarks](docs/performance.md)**

## Resources

- 🌐 [Web Playground](https://playground.objeck.org) — try Objeck in your browser
- 📖 [Documentation](https://www.objeck.org)
- 🏗️ [Architecture](docs/architecture.md) — Mermaid diagrams covering compiler, VM, JIT, libraries, and CI/CD
- 🎯 [Examples](https://github.com/objeck/objeck-lang/tree/master/programs)
- 💬 [Discussions](https://github.com/objeck/objeck-lang/discussions)
- 🐛 [Issues](https://github.com/objeck/objeck-lang/issues)

















