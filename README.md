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
  <a href="https://github.com/objeck/objeck-lang/actions/workflows/c-cpp.yml"><img src="https://github.com/objeck/objeck-lang/actions/workflows/c-cpp.yml/badge.svg" alt="GitHub CI"></a>
  <!-- <a href="https://scan.coverity.com/projects/objeck"><img src="https://img.shields.io/coverity/scan/10314.svg" alt="Coverity SCA"></a> -->
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

## Quick Start

```bash
# Install (example for macOS/Linux)
curl -LO https://github.com/objeck/objeck-lang/releases/download/v2026.2.1/objeck-linux-x64_v2026.2.1.tgz
tar xzf objeck-linux-x64_v2026.2.1.tgz
export PATH=$PATH:~/objeck-lang/bin

# Hello World
echo 'class Hello {
  function : Main(args : String[]) ~ Nil {
    "Hello World"->PrintLine();
  }
}' > hello.obs

# Compile and run (modern syntax)
obc --source hello.obs --destination hello.obe
obr hello.obe

# Or use concise shortcuts
obc -s hello.obs -d hello.obe
```

📖 **Full docs**: [objeck.org](https://www.objeck.org)
💡 **Examples**: [github.com/objeck/objeck-lang/programs](https://github.com/objeck/objeck-lang/tree/master/programs)

## What's New

**v2026.2.1** ✅
  * **Performance**: Memory manager optimized with O(1) lookups and in-place sweeping
  * **ARM64 JIT**: 11 critical optimizations, including char arrays, register targeting, multiply-by-constant
  * **x64 JIT**: Instruction encoding optimizations with dynamic backpatching
  * **Windows ARM64**: Full platform support with mbedTLS 3.6.3
  * **Testing**: Comprehensive regression suite with 350+ tests and automated CI/CD
  * **Compiler**: Enhanced error messages with operator symbols and inline hints
  * **NLP Library**: New natural language processing library with tokenization, TF-IDF, text similarity, and sentiment analysis
  * **ML Enhancements**: Extended ML library with activation functions, feature scaling, metrics, and cross-validation
  * **Crypto**: Migrated to mbedTLS for lighter footprint (OpenSSL replacement)
  * **Bug Fix**: Fixed critical String.Trim()/TrimFront() index out of bounds crash that affected string processing

**v2026.2.0** ✅
  * Modern GNU-style CLI (`--source`/`-s`, `--debug`/`-D`) with full backward compatibility
  * Enhanced library path handling
  * Development workflow improvements with Claude Code

**v2025.9.0** ✅
  * OpenCV integration for real-time computer vision
  * OpenAI Realtime API (`gpt-4o-realtime-preview`)
  * GPT-5 reasoning models

[📋 Full changelog](CHANGELOG.md) • [🗺️ Roadmap](ROADMAP.md)

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
- 🔄 **CI/CD**: Automated testing on every commit (GitHub Actions)
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

**📚 [Testing Documentation](programs/TESTING.md)** • **🧪 [Regression Tests](programs/regression/)**

## Resources

- 📖 [Documentation](https://www.objeck.org)
- 🎯 [Examples](https://github.com/objeck/objeck-lang/tree/master/programs)
- 💬 [Discussions](https://github.com/objeck/objeck-lang/discussions)
- 🐛 [Issues](https://github.com/objeck/objeck-lang/issues)









