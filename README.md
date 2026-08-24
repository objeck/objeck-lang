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
  <a href="https://github.com/objeck/objeck-lang/releases"><img src="https://img.shields.io/badge/release-v2026.8.3-blue" alt="Latest Release"></a>
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
curl -LO https://github.com/objeck/objeck-lang/releases/download/v2026.8.3/objeck-linux-x64_2026.8.3.tgz
tar xzf objeck-linux-x64_2026.8.3.tgz
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

### v2026.8.3 ✅
  * **A corrupt library could crash the compiler** — `TypeParser::ParseType` and `ParseParameters` switch on the first character of a type string and had no default case, so anything outside the known set — including an **empty** string, whose `operator[](0)` yields a null character — left the type null and was dereferenced immediately. The linker calls both on type strings read straight out of `.obl` files, so the input is not the compiler's own
  * **A `}` on the first line hung the REPL** — an unsigned indent counter decremented at `0` wrapped to `SIZE_MAX`, and the indent loop below then ran about 1.8×10¹⁹ times. Listing and saving both hit it
  * **One malformed request no longer ends a debug session** — only JSON parse errors were guarded, so a message that parsed but carried an unexpected type threw from inside the handler, unwound out of `Run()` to a `main()` with no handler, and terminated the process — losing every breakpoint and the running program over one bad request. Seven flags shared between the DAP and VM threads are atomics now: they were written under a mutex and read without one at **every instruction**, so nothing stopped an `-O3 -flto` build hoisting those loads out of the dispatch loop and a disconnect or step could go unseen indefinitely
  * **A connect that never completed could report success** — `getsockopt(SO_ERROR)` was unchecked on both the POSIX and Windows connect-with-timeout paths, and both could hand back a socket still in non-blocking mode, so a caller expecting a blocking read got a spurious `EAGAIN`/`WSAEWOULDBLOCK` instead of data. The unchecked `F_GETFL` behind the POSIX case also restored garbage flags onto the socket
  * **One ONNX call reformatted every float for the rest of the run** — both generation reports applied `std::fixed` with `setprecision(1)` directly to `std::wcout` and never restored it, and nothing on the Objeck side can clear a leaked floatfield. `StdErrFloat` carried the mirror-image bug: it read the saved state from `std::wcout`, modified `std::wcerr` and restored onto narrow `std::cout` — three different objects
  * **Two silent compiler mistakes** — a lambda whose signature collided with an existing method was dropped with **no diagnostic at all** while the code carried on encoding and associating it, and one of `MethodCall`'s five constructors left `func_ref_unwrap` indeterminate, so non-null garbage meant emitting a bogus call
  * **Entry points survive what they throw** — `obc`, `obr`, `obd` and `obi` each had a long unprotected prologue (locale and codecvt construction, and the usage-string building that is a `bad_alloc` path) where anything thrown called `terminate()` with no message. Every entry point is now wrapped
  * **Windows installers are signed — and the notes claim it only when true** — every Windows MSI from v2026.4.0 through v2026.8.2 shipped **unsigned** while the generated notes asserted otherwise: `signtool` was configured, ran, failed on every artifact, and the build warned and continued. The key is on a hardware token, so CI can never sign; signing is now an explicit local step, and a checker reports the real `Get-AuthenticodeSignature` status rather than inferring it from the file existing
  * **Coverity Scan on Windows as well as Linux** — the first Windows scan turned up twenty real defects in cross-platform sources that MSVC compiles differently than GCC, so the Linux scan had never reached them. The scan token now lives outside the tree

### v2026.8.2
  * **Subscript the result of a method call** — `GetItems()[0]->GetName()` now works. It had never been implemented, and the grammar swallowed the attempt: `[` after a call was read as a fresh static-array literal, so the call was evaluated and discarded — `GetArr()[0]->Size()` silently returned `1`. **Behaviour change:** code that compiled and returned the wrong value now returns the correct one
  * **Face detection actually detects faces** — `FaceSession` returned zero results for every image at every threshold since v2026.5.3, because the SCRFD decoder compared a row count against an element count and skipped every stride group. Inference ran normally throughout, which is why it read as a model or threshold problem
  * **The release build proves the toolchain works** — every earlier gate asserted only that files existed, so a release could ship binaries unable to compile or run anything and still go out green. Both platforms now compile and run a real program from the shipped tree and assert the version matches the tag
  * **`obu` now actually ships** — v2026.8.0 advertised the updater as a headline feature and shipped no binary on any platform. The build never packaged it, and CI missed it because the updater's own test builds a separate copy with test hooks — so it was tested continuously while never being delivered. All three deploy scripts now install it to `bin/`, and the release build's binary-verification gates list it, so a future omission fails before a tag is pushed
  * **`HeaderCheck` is documented** — the validators behind every `AddHeader` call (`IsValidName`, `IsValidValue`) had no doc comments, so editor hover and completion showed nothing for the security fix's main entry points; `Flatten`'s doc block was also attached to the wrong function
  * **Full-screen editor in `obi`** — `/e` opens raw-mode terminal editing over the REPL buffer: syntax coloring driven by the compiler's own scanner, damage-diffed rendering, CJK/tab-correct widths, undo/redo with coalesced typing, Shift-selection and a clipboard, plus an opt-in vi profile (F2). **F5 compiles and runs the buffer as a subprocess**, streaming its output into a pane that stays cancellable with Esc, and **F8 walks the cursor through compile errors**. Header-only — no build-system changes on any platform
  * **HTTP/3 on Windows** — `Http3Client` now works on Windows 11 / Server 2022 and later, backed by WinHTTP over MsQuic. It had shipped documented-and-dead: the trap handlers compile unconditionally, so the API was present and every request simply failed. WinHTTP treats HTTP/3 as a *preference*, so every request asserts the protocol actually used and **fails rather than silently downgrading**. No vendored QUIC library, no second TLS backend, no new DLL to deploy
  * **Five HTTP request-injection fixes** — the most severe needs only a URL: `Url->New` does not sanitize, and the path and host were appended verbatim into the HTTP/1.1 request line, so a CR/LF split the request line itself. Caller-supplied header values and content types were injectable the same way across HTTP/1.1, HTTPS, HTTP/2 and HTTP/3. A new `Web.HTTP.HeaderCheck` validates names, values and request targets per RFC 9110/9113 at every serialization site. Separately, HTTP/3 connection IDs could be **uninitialised stack memory** when the CSPRNG failed, since the `gnutls_rnd` return value was ignored — a memory disclosure in the cleartext QUIC Initial header
  * **`obu` updater** — `obu check` compares the installed version against the latest GitHub release; `obu update` and `obu rollback` (Linux/macOS) download the platform archive and its `SHA256SUMS`, verify the digest **before touching disk**, then do an all-or-nothing staged swap with a post-install check and automatic rollback. No shell is used for the download, extract or swap. Every release now ships a `SHA256SUMS` asset
  * **`AddHeader` now actually sends headers on HTTP/2 and HTTP/3** — both clients stored headers on the Objeck side and then dropped them, because the trap signature had no headers argument and nothing ever wrote the map the native backends were already reading. Verified by a server echoing the value back, not by a status code. HTTP/1.1 was never affected
  * **ARM64 JIT clobbered a general register on every float compare** — `move_freg_freg` addressed the general register file rather than the FP file, because the `Register` enum restarts float numbering at `D0 = 0`. Every call both failed to move the float and overwrote a general register; when it held a live pointer, the next use followed a float bit pattern as an address — the long-open *"memory corruption of free block"* crash, whose faulting PC `0x3F947AE147AE147B` is the IEEE-754 bits of `0.02`. Fixing the encoding then exposed a stray copy that zeroed the **divisor** of every JIT float division. AMD64 was correct throughout
  * **`v->Pow(10)` computed `Pow(10, v)`** — 100 instead of 1024 — whenever the receiver was a variable, instance variable or array element; literals were correct, so the two spellings of one call disagreed. All 29 multi-argument primitive functions were affected. Calculated receivers in an expression (`(1 + 1)->Pow(10)`) corrupted the heap outright, because the emitter dropped the call arguments
  * **Divide by zero crashed at default optimization** — the constant folder kept a zero-divisor division for its runtime trap but emitted it *before* its operands, turning a clean trap into a silent crash. **Modulus by zero never trapped at all**, and now traps exactly as division does
  * **Nil-safe operators** — `a?->b()` calls `b` only when `a` is non-`Nil`, yielding `Nil` instead of faulting, and `a ?? b` supplies `b` when `a` is `Nil` (evaluating `b` only when needed). A single `?->` guards the whole rest of a chain — `maybe?->Trim()->ToUpper()` — and the two combine: `maybe?->ToUpper()->Size() ?? -1`. Both desugar onto the existing `Try()`/`Otherwise()` intrinsics, so no new bytecode is emitted and the JIT backends and VM are unchanged. Spelled `?->` rather than `?.` because Objeck's member accessor is `->`
  * **Unsigned shift `>>>` and unsigned `Int` operations** — every integer sits in a signed 64-bit slot, so `>>` copies the sign bit into the high end. `a >>> n` shifts a zero in instead, at the same precedence as `>>` and left-associative, for values used as bit patterns rather than as numbers. `Int` also gains `ShiftRightUnsigned`, `CompareUnsigned`, `DivideUnsigned`, `ModUnsigned` and `ToUnsignedString`, which read both operands as unsigned 64-bit quantities — so `Int->ToUnsignedString(-1)` is `18446744073709551615`. Like `?->` and `??`, `>>>` desugars onto an ordinary library call, so no opcode is added and the VM and both JIT backends are unchanged
  * **Unsigned integer literals** — a `u` suffix reads a literal across the whole unsigned range and keeps its bit pattern, so `0xFFFFFFFFFFFFFFFFu` and `18446744073709551615u` are writable. It changes only how the number is scanned, so the value stays an ordinary `Int` — no new type, no conversion rules. This also fixes a silent bug: literals past the signed range used to saturate without a diagnostic, so `0xFFFFFFFFFFFFFFFF` produced `9223372036854775807` instead of every bit set. It now evaluates to `-1`, as it does in C, Java, C# and Rust
  * **Debugger inspects structures, not addresses** — every variable used to be a dead end, so a `Map` showed as `Collection.Map@0x1f4a2c0` and nothing more. Objects now expand into their fields, arrays into indexed elements, and `Vector`/`Map`/`Hash` into their contents (`Map` in key order, `Hash` as key → value), each child expanding recursively. Strings and boxed scalars show their value instead of their backing storage, and watch and hover expressions expand the same way
  * **Data breakpoints** — break when a value *changes* rather than when execution reaches a line, which is how you find the one write among twenty that corrupts a field. Right-click a variable and choose "Break on Value Change"; the stop names the watch and reports the old → new transition
  * **Wider debug-adapter coverage** — 17 DAP capabilities, up from 8: `terminate`, `breakpointLocations` (so editors stop offering breakpoints that cannot bind), `exceptionInfo`, `setExpression`, `completions`, `modules`, `loadedSources`, data breakpoints, and variable paging
  * **Language server reports itself** — `serverInfo` (name and version) so clients can display which server they are talking to and a stale deployment is visible, plus declared code-action kinds (routing "Organize Imports" to the server) and signature-help retrigger characters
  * **Language server crash fixes** — five requests took the server process down rather than returning an error: go-to-implementation, semantic tokens, inlay hints, and both call-hierarchy directions. Each returned a `Result[]` through the local argument array and failed its cast
  * **Language server resolved the wrong method** — a method's line range was converted to 0-based numbering at its start but not its end, so every method overran onto the next declaration and any cursor past the first method of a class resolved to the first method. This affected go-to-definition, find-references, hover, rename and call hierarchy
  * **`Otherwise()` dropped its chain** — anything written after it was silently discarded, so `x->Otherwise("abc")->ToUpper()` returned `"abc"`. `Try()`/`Otherwise()` also now resolve on an indexed receiver, so `arr[i]?->m()` compiles
  * **Formatter no longer breaks source** — it scanned `?` one character at a time, so formatting a file using `??` emitted `? ?`, producing code that would not compile
  * **`runtime.feature.http2` / `runtime.feature.http3`** — report which protocol engines were actually compiled in, so a caller can tell *not supported* from *the network failed*. Because the traps compile unconditionally, "the API exists" proved nothing — which is how the Windows HTTP/3 gap went unnoticed. Relatedly, `obd` had shipped with HTTP/2 and HTTP/3 compiled out, and HTTP/3 was silently unavailable on Linux ARM64
  * **Editor tooling is tested in CI** — the formatter, language server and VS Code extension run on every push, and the six standalone DAP suites (~78 assertions) now run on POSIX rather than Windows only; their first Linux/macOS run immediately caught a debugger String-rendering bug. Two suites had never executed at all: the formatter's runner pointed outside the repository, and the tooling scripts appended the build tree to `PATH` so a system-wide install shadowed it

### v2026.6.4
  * **Multithreaded GC stability fix** — fixed an intermittent crash (`0xC0000005`) in the generational minor garbage collector during thread startup: a thread being spawned held its `self` and argument as untracked raw pointers, so a moving collection during the spawn handoff could relocate the object and leave the new thread a stale reference. These are now tracked and relocated across collection. Surfaced only under heavy multithreaded churn

[📋 Full changelog](CHANGELOG.md) • [🗺️ Roadmap](docs/performance.md#speedup-roadmap) • [📝 Editor & IDE setup](docs/editors.md)

## Downloads

**Latest Release:** [v2026.8.3](https://github.com/objeck/objeck-lang/releases/latest)

| Platform | Architecture | Download |
|----------|--------------|----------|
| **Windows** | x64 | [MSI Installer](https://github.com/objeck/objeck-lang/releases/latest) / [ZIP](https://github.com/objeck/objeck-lang/releases/latest) |
| **Windows** | ARM64 | [MSI Installer](https://github.com/objeck/objeck-lang/releases/latest) / [ZIP](https://github.com/objeck/objeck-lang/releases/latest) |
| **Linux** | x64 | [TGZ Archive](https://github.com/objeck/objeck-lang/releases/latest) |
| **Linux** | ARM64 | [TGZ Archive](https://github.com/objeck/objeck-lang/releases/latest) |
| **macOS** | ARM64 | [TGZ Archive](https://github.com/objeck/objeck-lang/releases/latest) |
| **LSP** | All platforms | [ZIP Archive](https://github.com/objeck/objeck-lang/releases/latest) |

📦 **Alternative:** [Sourceforge](https://sourceforge.net/projects/objeck/files/) • 📚 **API Docs:** [objeck.org/api/latest](https://www.objeck.org/api/latest/)

> **Note:** Windows installers are signed and timestamped (`CN=Randy Hollines`, Sectigo); the macOS `.pkg` is signed and notarized. Signing uses a hardware token and therefore happens locally after publication, so verify rather than assume — `Get-AuthenticodeSignature <file>.msi` reports `Valid` only when it really is signed. Check any download against the release's `SHA256SUMS`, which is regenerated after signing. Builds are automated on GitHub Actions runners.

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
- 🏗️ [Architecture](docs/architecture.md) — Mermaid diagrams covering compiler, VM, JIT, libraries, and CI/CD
- 🎯 [Examples](https://github.com/objeck/objeck-lang/tree/master/programs)
- 💬 [Discussions](https://github.com/objeck/objeck-lang/discussions)
- 🐛 [Issues](https://github.com/objeck/objeck-lang/issues)













