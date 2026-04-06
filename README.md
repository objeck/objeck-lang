<p align="center">
  <img src='https://github.com/objeck/objeck-lang/blob/master/core/lib/code_doc/templates/resources/objeck-logo-alt.png' height="125px"/>
</p>

<p align="center">
<strong>Object-oriented • JIT-compiled • AI-ready • Cross-platform</strong><br>
Modern programming language with first-class ML and computer vision support
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
- 🤖 **AI/ML first-class**: OpenAI, Gemini, Ollama, OpenCV, ONNX
- 💻 **Developer-friendly**: REPL shell, LSP plugins for VSCode/Sublime/Kate
- 🌍 **Cross-platform**: Linux, macOS, Windows (including ARM/RPI)
- 🔧 **Full-featured**: Threads, generics, closures, reflection, serialization

**Perfect for:**
AI/ML prototyping • Computer vision • Web services • Game development

## Try It Online

No installation needed - write, compile, and run Objeck code directly in your browser:

👉🏽 Interactive [playground](https://playground.objeck.org) with 33 demo programs across 7 categories with Monaco editor and syntax highlighting.

## Quick Start

```bash
# Install (example for macOS/Linux)
curl -LO https://github.com/objeck/objeck-lang/releases/download/v2026.4.2/objeck-linux-x64_2026.4.2.tgz
tar xzf objeck-linux-x64_2026.4.2.tgz
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

**v2026.4.2** 🏃🏿‍♂️‍➡️🏃🏻‍♀️‍➡️
  * **JIT local variable register cache** (AMD64 + ARM64) — keeps values in registers after store, avoids redundant reloads, evicts on demand when register pool is exhausted
  * **Hardened JSON, JSON stream, and XML parsers** against malformed input
  * **DTLS (Datagram TLS) support** — new `DTLSSocket` and `DTLSSocketServer` classes for secure UDP communication (IoT, VoIP, gaming)
  * **Link-time optimization** — added `-flto=auto` across all GCC Makefiles (AMD64 and ARM64)
  * **ARM64 native CPU tuning** — `-mcpu=native` auto-detects RPi5 (Cortex-A76) and Jetson Orin (Cortex-A78AE)
  * Fixed all MSVC and GCC compiler warnings
  * Fixed doc generator error on `@hidden` tag

**v2026.4.1** 
  * **Debug Adapter Protocol (DAP)** — VS Code debugging with conditional breakpoints, ANSI color output, and readline support
  * **3.3x binarytrees speedup** — young-gen bump allocator, direct JIT-to-JIT calling, atomic CAS mark bits, auto-JIT dispatch fix
  * **MTHD_CALL JIT whitelist** (x64 + ARM64) — methods containing method calls can now be JIT-compiled and auto-JIT'd
  * **GC thread safety** — memory barriers in PushFrame/PopFrame paired with acquire fences in GC to fix intermittent threading segfaults
  * **Code signing** — macOS Developer ID Application + Installer certificates and notarization; Windows Sectigo signing; all automated in CI
  * **macOS .pkg installer** — signed and notarized native installer with PATH setup via productbuild
  * Networking: SSE streaming, socket receive timeouts, ReadBytes partial read fix, HTTP client/server hardening
  * ODBC: BigInt support, connection strings, transactions, error handling, schema discovery
  * OpenCV: contours, VideoWriter, transforms, normalization, 15 new image processing functions
  * Phi-3 Vision multimodal inference — 3-model pipeline with FP16 and DirectML/CUDA support
  * Unified ONNX build system — single source with preprocessor-selected providers (DML, CUDA, QNN, CoreML)
  * Hash auto-resize at 75% load, Vector in-place Remove, JSON escape/keyword fixes
  * Fixed the constructor early return crash, CSV.Median, CSV.Average, URL encoding, Response.ToString nil check
  * [Performance details and benchmarks →](docs/performance.md)

**v2026.2.1** 
  * New 'try/otherwise' error handling framework
  * Fixed emoji and supplementary character output on all platforms (stdout and stderr)
  * Migrated Windows installer from VDPROJ to WiX v4 — MSIs now built in CI without Visual Studio
  * Debugger `help`/`h` command with full command reference
  * Fixed JIT segfaults in AMD64 (MTHD_CALL, DYN_MTHD_CALL, STOR_CLS_INST_INT_VAR) and ARM64 pre-scan rejection
  * Fixed broken `Log`/`Log10` float math (x87 wrong constants) and `Rand` float params
  * Fixed 15 bugs in SDL2 native interface and Objeck bindings
  * Fixed segfaults in CompressGzip/UncompressGzip/CompressBr/UncompressBr (zero-init z_stream)
  * 4.38x speedup on nbody — inline limit increase (128→256) enables JIT to optimize getter/setter-heavy code
  * Compiler CSE, dead code elimination, div-by-zero constant folding bugfix
  * JIT division-by-zero guards in constant folding for x64 and ARM64
  * 14 debugger regression tests with expect-based automation in CI
  * 16 runtime regression tests (ARM64 JIT, core language, JIT native methods)
  * Linux ARM64 and macOS ARM64 test execution enabled in GitHub Actions
  * Fixed VM crash in Try/Otherwise on Nil dereference inside non-virtual method calls
  * Fixed Windows debugger build — `HELP_COMMAND` enum collision with `WinUser.h` macro
  * Web Playground updated to v2026.2.1
  * 10 new performance benchmarks with measurement tooling
  * [Performance details and benchmarks →](docs/performance.md)

[📋 Full changelog](CHANGELOG.md) • [🗺️ Roadmap](ROADMAP.md)

## Downloads

**Latest Release:** [v2026.4.2](https://github.com/objeck/objeck-lang/releases/latest)

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

### AI Integration
```ruby
# OpenAI Realtime API - get text AND audio
response := Realtime->Respond("How many James Bond movies?",
                              "gpt-4o-realtime-preview", token);
text := response->GetFirst();
audio := response->GetSecond();
Mixer->PlayPcm(audio->Get(), 22050, AudioFormat->SDL_AUDIO_S16LSB, 1);
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

**AI & Machine Learning**
- [OpenAI](https://github.com/objeck/objeck-lang/blob/master/core/compiler/lib_src/openai.obs), [Gemini](https://github.com/objeck/objeck-lang/blob/master/core/compiler/lib_src/gemini.obs), [Ollama](https://github.com/objeck/objeck-lang/blob/master/core/compiler/lib_src/ollama.obs)
- [NLP](https://github.com/objeck/objeck-lang/blob/master/core/compiler/lib_src/nlp.obs) (tokenization, TF-IDF, text similarity, sentiment analysis)
- [OpenCV](https://github.com/objeck/objeck-lang/blob/master/core/compiler/lib_src/opencv.obs) (computer vision)
- [ONNX Runtime](https://github.com/objeck/objeck-lang/blob/master/core/compiler/lib_src/onnx.obs) (cross-platform ML inference)
- [Phi-3 / Phi-3 Vision](https://github.com/objeck/objeck-lang/tree/master/programs/frameworks/opencv_onnx) (local SLM text and vision inference)

**Web & Networking**
- HTTP [server](https://github.com/objeck/objeck-lang/blob/master/core/compiler/lib_src/net_secure.obs)/[client](https://github.com/objeck/objeck-lang/blob/master/core/compiler/lib_src/net_secure.obs), [OAuth](https://github.com/objeck/objeck-lang/blob/master/core/compiler/lib_src/net_common.obs)
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
  - 📖 [Release Process Documentation](docs/release_process.md) • [CI/CD Architecture](docs/CI_CD.md)
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
- 🎯 [Examples](https://github.com/objeck/objeck-lang/tree/master/programs)
- 💬 [Discussions](https://github.com/objeck/objeck-lang/discussions)
- 🐛 [Issues](https://github.com/objeck/objeck-lang/issues)

















