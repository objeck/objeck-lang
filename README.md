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
  <a href="https://github.com/objeck/objeck-lang/releases"><img src="https://img.shields.io/badge/release-v2026.9.1-blue" alt="Latest Release"></a>
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

👉🏽 [Playground](https://playground.objeck.org) — 34 demos across 7 categories, Monaco editor, no install required.

## Quick Start

```bash
# Install (example for macOS/Linux)
curl -LO https://github.com/objeck/objeck-lang/releases/download/v2026.9.1/objeck-linux-x64_2026.9.1.tgz
tar xzf objeck-linux-x64_2026.9.1.tgz
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

### v2026.9.1 
  * **Compiled code now calls compiled code directly** &mdash; a call between JIT'd methods used to cross a C++ bridge: an eleven-argument entry, a pooled frame, a release. The caller now builds the callee's frame on its own stack and enters its native entry, with inline caches for `virtual` and func-ref sites. A bound call went from 26.5 ns to 5.5 ns, a `virtual` call from 125 ns to 6.0 ns, and `Fib(32)` from 0.208 s to 0.043 s on AMD64; on an M4 Max a call went from 17.1 ns to 7.4 ns and `Fib(32)` from 0.135 s to 0.050 s. A dense `select` compiles to a jump table on both backends
  * **Every hash of a string was wrong** &mdash; `Hash->SHA256("abc"->ToByteArray())` digested `"abc"` plus a zero byte — a `Byte[]` size-word convention the JIT and thirteen trap producers got wrong; fixed everywhere, with a test that hashes every producer against known digests
  * **JIT arithmetic matches the interpreter** &mdash; `IMUL` wrote to the wrong register, native-call temporaries were never spilled, 64-bit immediates were truncated; the equivalence fixture now runs on every platform
  * **The JIT is 2-8x faster on common loops** &mdash; `a->Size()` inlined (an array-summing loop 8x), constant division by a multiply (2.5x), one-instruction array addressing, eight allocatable registers on AMD64. Two shipped bugs found on the way: `>>` was a logical shift on AMD64, and `?->` on a nil receiver in JIT'd code exited the process. `OBJECK_JIT_REPORT=1` names the methods the JIT hands back
  * **Breakpoints fire inside threads** &mdash; spawned threads were invisible to `obd` and DAP faked a single thread; all-stop with real thread ids, an `obd threads` command, and conditional breakpoints in the stopping thread's frame
  * **Debugger commands that lied** &mdash; nine CLI commands reported success while doing the wrong thing; DAP `evaluate` honours `frameId`; hex literals scan; `setVariable` assigns the field or element it names
  * **`Game.OpenGL`** &mdash; `Skybox`, `Quaternion`, tangent-space normal maps, `Material->SetEmissive`; the overlay is no longer upside down
  * **`--jit=off|<calls>` and `--lib-path=<dir>`** &mdash; every VM flag states its valid values and refuses others
  * **The language server crashed at startup** &mdash; a GC range test accepted an unaligned pointer; fixed and proven over 200 runs
  * **Release process** &mdash; checksums regenerated after signing, manual steps declared once, open issues triaged before a tag, POSIX deploys verify their native libraries, TLS refusals tested
  * **Three ways compiled code could corrupt memory** &mdash; a frame holding a declared-but-unreferenced local was laid out from its references but scanned by declaration, so the collector read every lower slot under the wrong type; a loop that parked at a GC safepoint kept a stale `self` after another thread's collection moved it ([#746](https://github.com/objeck/objeck-lang/issues/746)); and a constant character stored into a `Char[]` wrote eight bytes on ARM64 and Linux x64, overwriting its neighbours and running past the end of the array ([#781](https://github.com/objeck/objeck-lang/pull/781))
  * **The compiler mis-built two common shapes** &mdash; every string concatenation in a method shared one accumulator, so `F(a+b) + c` produced the wrong string or crashed ([#750](https://github.com/objeck/objeck-lang/issues/750), [#752](https://github.com/objeck/objeck-lang/issues/752)); and the `-opt s3` inliner shifted only `Int` and `Float` slots, so a method taking a func-ref parameter stored the reference over its caller's locals once inlined ([#763](https://github.com/objeck/objeck-lang/issues/763))
  * **Runtime properties, locales and exit codes** &mdash; `Runtime->SetProperty` dropped every set after a property's first, and on Linux and macOS a first set kept the runtime's own directories from ever loading; `SetLocale` refuses a name the system cannot supply; and `obr` on POSIX exits non-zero after a VM-reported error, as it always did on Windows
  * **`Console->WriteBuffer(Char[])` corrupted every write after it** &mdash; the VM encoded the buffer to UTF-8 itself and wrote the bytes into a stdout already in a wide CRT mode, so the data was encoded twice (`'A','B'` arrived as U+4241) and the invalid sequence left the stream unusable for the rest of the program. It also ignored the count it was given. Found through `fasta`, which had never compiled ([#793](https://github.com/objeck/objeck-lang/pull/793))
  * **The standard libraries are rebuilt by the compiler that ships with them** &mdash; the committed `.obl` predated four compiler correctness fixes, including the func-ref inliner that library code is compiled with (`-opt s3`). 30 of 32 libraries changed ([#792](https://github.com/objeck/objeck-lang/pull/792))

### v2026.9.0 ✅
  * **A server that wrote a response and closed could lose all of it** &mdash; on Windows loopback the reader got a connection reset and zero bytes, even though every byte had been accepted and delivered. `TCPSocket` and `TCPSecureSocket` gain `CloseGracefully()`, which reads until the peer hangs up and then closes, so the client owns the teardown. Measured over 180 transfers of a 16KB response: `Close()` lost 21, `CloseGracefully()` lost none
  * **The language server serialized every request behind one lock** &mdash; concurrent analysis was correct only because of it, with `TreeFactory` and `TypeFactory` as process-wide singletons underneath. They are now bound per thread through a scope guard, so each analysis gets its own and the coarse lock gives way to per-program locking
  * **Four publish steps reported success while doing nothing** &mdash; Sourceforge, the Marketplace, the playground and the API docs each skipped on an absent credential and passed, so v2026.8.4 published with all four green while the playground served a three-month-old engine. A missing credential now fails and names the secret, or is declared manual in one place that also prints as a to-do, and a pre-flight gate checks the pipeline can do what it advertises before the tag is pushed

### v2026.8.4
  * **`Game.OpenGL` — 3D graphics for Objeck** — OpenGL 3.3 core over SDL2 on Windows, Linux and macOS. 26 classes covering windowing and frame pacing, built-in shaders, meshes and OBJ loading, textures, cameras, materials, up to eight directional/point/spot lights with Blinn-Phong specular, shadow maps including omnidirectional cube shadows, render-to-texture, instancing through a one-call `PropBatch`, frustum culling, raycasting for hitscan and picking, gamepad input, a pixel-space text overlay, and a scene that answers collision. The examples got **shorter** as it grew — the minimal window demo went from 105 lines to 25, and per-frame allocations in both original draw loops went to zero. Verified by 453 checks that read pixels back rather than merely exiting cleanly, and two demos ship in the distribution
  * **`Web.Server` could not be used by anyone** — it shipped in every release with 13 native entry points that existed in exactly one file: the binding itself. No `.cpp`, no build target, no library in any deploy tree, and `Request`/`Response` declared no constructor, so a program could not obtain an instance at all. Writing the missing native library was never an option — the design is a per-host bridge for Nginx, IIS and Apache, whose request structures differ entirely, so one generic library cannot exist. It is now implemented in pure Objeck over `Web.HTTP.Server`: same bundle, same class names, same signatures, no native library. Coverage went from 0 of 13 methods to 13 of 13
  * **The JIT silently computed the wrong answer above 2³¹** — 64-bit immediates were truncated to 32 bits: on AMD64 for `and`, `or`, `xor`, `add` and `sub`, and on Windows ARM64 for every one of them, where `long` is 32 bits under LLP64. No crash and no diagnostic, just wrong arithmetic. A stored float compare also clobbered a callee-saved register on AMD64. Windows ARM64 had shipped untested since February, which is why its variant survived
  * **A server that wrote and closed could lose the response** — on Windows loopback, roughly 47% of responses, because the sender tearing down first discards what the receiver has not read. HTTP now uses keep-alive, removing the exposure rather than hiding it. Alongside it: a short `send()` silently dropped the rest of the buffer on **both** platforms, a real HTTP 500 lost its body while a dead socket reported one, and one failed name lookup called `WSACleanup` and shut Winsock down for the **whole process** — every open socket on every thread
  * **Three ways a live object could be collected** — an array returned by a VM trap held elements a minor collection could destroy (measured at 395 of 395 entries lost in one collection: arrays are born old, objects young, and the trap array was never dirtied through the write barrier); a value returned by a native library could be collected out of a reused argument buffer; and the JIT's `Int[]` copy dropped the write barrier entirely
  * **Windows ARM64 installs shipped without their runtimes** — no C++ redistributable, because `VCToolsRedistDir` is empty on the ARM64 runner, and OpenCV without its image codecs. Nothing checked either, so both failed on the user's machine rather than in CI. Cross-architecture native dependencies are now verified during the build
  * **A server that returns normally from `Main` no longer segfaults** — a thread blocked in a syscall never observes the halt request, so teardown freed the program image while that thread was still live, and `WSACleanup` on the way out then unblocked it into freed memory — losing all buffered output, so it looked as though the program had done nothing. Linux was always clean for one reason: it has no equivalent call
  * **The debugger had the same defect, and there it was not Windows-only** — `obd` hosts the debuggee's VM in-process and freed the program image *and the whole GC heap* after every run, halting and waiting for nothing. Because `obd` goes back to its prompt rather than exiting, an ordinary client connecting to the port the parked thread sits on wakes it with no `WSACleanup` involved: 6 access violations in 6 runs on Windows, 2 in 2 on Linux
  * **`obd` could not debug any multithreaded program** — compiled with `-debug`, it segfaulted on the first instruction a spawned thread executed. Those threads are built by a constructor that never initialized the debugger pointer, and the per-instruction hook called through it. Only `obd` compiles that hook in, so `obr` was never affected
  * **Native calls got materially cheaper** — a string literal allocates on every evaluation, and the literal naming the native function turned out to be 92% of a call's cost: 1655ns down to 130ns. Resolved entry points are now cached too, on every platform, removing a `GetProcAddress`/`dlsym` lookup and a wide-to-narrow conversion from every single call. Both are guarded by CI so they cannot drift back
  * **SDL2 loads on Windows without a hand-set `PATH`** — the DLLs shipped in `lib/sdl`, but Windows resolves a dynamically-loaded library's imports against the **executable's** directory, never the library's, so `libobjk_sdl.dll` failed to load for anyone who had not added it themselves. Every SDL program was affected, and the regression runner hid it by prepending the directory first
  * **The API reference stopped omitting whole libraries** — five files hard-code the library list and had drifted apart, one short two libraries while still naming a deleted third, so the counts matched and nothing looked wrong. Underneath, the doc parser was reading prose as code: the word "bundle" in a comment re-filed every class after it

## Downloads

**Latest Release:** [v2026.9.1](https://github.com/objeck/objeck-lang/releases/latest)

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













