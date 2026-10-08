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
  <a href="https://github.com/objeck/objeck-lang/releases"><img src="https://img.shields.io/badge/release-v2026.9.7-blue" alt="Latest Release"></a>
</p>

## Why Objeck?

**Built for modern development:**
- 🚀 **JIT-compiled** for performance (ARM64/AMD64)
- 🤖 **AI-native**: OpenAI, Gemini, Ollama, ONNX, OpenCV — no third-party packages
- 🌐 **Network-complete**: HTTP/1.1 · HTTP/2 · HTTP/3/QUIC · WebSocket · DTLS — all standard library
- 💻 **Developer-friendly**: REPL shell, LSP plugins for VSCode/Sublime/Kate, DAP debugger
- 🌍 **Cross-platform**: Linux, macOS, Windows (x64 + ARM64, including 64-bit Raspberry Pi)
- 🔧 **Full-featured**: Threads, generics, closures, reflection, serialization

**Perfect for:**
AI/ML prototyping • Computer vision • Web services • Real-time applications • Game development

## Try It Online

👉🏽 [Playground](https://playground.objeck.org) — 34 demos across 7 categories, Monaco editor, no install required.

## Quick Start

```bash
# Install (example for macOS/Linux)
curl -LO https://github.com/objeck/objeck-lang/releases/download/v2026.9.7/objeck-linux-x64_2026.9.7.tgz
tar xzf objeck-linux-x64_2026.9.7.tgz
# Linux only: install the system libraries the toolchain links against
# (mbedTLS, readline, SDL2/GL, OpenCV, unixODBC, LAME) --
# obr does not start without them. --check reports without installing.
./objeck-lang/install_deps.sh
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

### v2026.10.0
  * **A program could compile to an infinite loop at the default optimization level** &mdash; two helper methods containing the same construct were inlined into one shared label space, so the second body's jump resolved to the first body's label: a backward jump into already-executed code. Two helpers that each evaluate `->Abs()` was enough to trigger it, the program was correct at every lower optimization level, and `-opt s3` is what `obc` uses when no level is given, so no flag was needed to reach it. One shipped library, `misc.obl`, was built wrong by it ([#1037](https://github.com/objeck/objeck-lang/issues/1037))
  * **A classifier can be evaluated against a false-positive budget, end to end** &mdash; `CrossValidation->Evaluate` now returns the pooled out-of-fold score for every row, so a decision threshold can be chosen from scores no model saw its own row in, and applied unchanged to held-out data. Folds can be stratified by any key rather than only the binary label, which matters when the positive class is a union of rare categories. `LogisticRegression` implements `ScoreModel`, so a linear baseline goes through the same evaluation path as the tree classifiers ([#1035](https://github.com/objeck/objeck-lang/pull/1035))
  * **`Metrics->AtThreshold` reports what a threshold actually did** &mdash; recall, false-positive rate and precision at one given cutoff, which is the inverse of `RecallAtFpr` and the only way to check whether a budget chosen on one sample still holds on another. `Metrics->Bootstrap` puts a percentile confidence interval around any of them; a resample that draws a single class cannot support the statistic, so those are discarded rather than averaged in, and the surviving count travels with the interval ([#1036](https://github.com/objeck/objeck-lang/pull/1036))
  * **Reporting on rare categories** &mdash; `Metrics->RecallByGroup` breaks recall out per group and carries the count each rate rests on, because a recall of 1.0 over two positives is not the claim a recall of 1.0 over two thousand is. `UnseenGroups` identifies categories a model was never trained on. `PrecisionAtBaseRate` reprojects a measured operating point onto an assumed prevalence, and `ShuffleLabels` gives the shuffled-label control a seed so a control that finds something can be reproduced ([#1038](https://github.com/objeck/objeck-lang/pull/1038))
  * **Any ONNX model can be run, not only the eight with a hand-written wrapper** &mdash; `API.Onnx.Session` takes named tensors and returns the model's outputs as named tensors, and `GetModelInfo()` reports the names, element types and shapes a model expects, with a symbolic dimension reported as `-1` rather than guessed at. The caller never states an element type: tensors cross as `Float[]` and the model's own declaration decides what each input becomes, so FP32, FP16, INT64 and quantized models are driven identically. Preprocessing and decoding move into Objeck, where they can be written once and reused across backends. Checked against real models: it agrees exactly with `ResNetSession` on `resnet34`, and a Phi-3 prefill runs through it with 67 inputs ([#1047](https://github.com/objeck/objeck-lang/pull/1047))
  * **`API.Inference` reaches a model server over HTTP** &mdash; a new library, the sibling of `API.Onnx`: the same models, served rather than loaded. For a model that is not an ONNX file you have locally &mdash; one on a GPU machine, a TensorFlow model you would rather not convert, or anything already behind Triton, KServe, TorchServe or TF Serving. Speaks KServe v2 and TF Serving v1, and `DescribeRequest` prints the body it would send, which is usually quicker than interpreting a rejection ([#1051](https://github.com/objeck/objeck-lang/pull/1051))
  * **The FP16 ONNX input tensor wrapped memory that had already been freed** &mdash; the helper built the halves in a vector local to itself and handed back a tensor wrapping it, so inference read whatever had replaced it. The FP32 path is correct, which is why it survived: no model in this tree is FP16. The buffer is now a required parameter, so the signature enforces its lifetime instead of a comment asking for it ([#1045](https://github.com/objeck/objeck-lang/pull/1045))
  * **Closing an ONNX session twice corrupted the heap** &mdash; the native close deletes the session and nulls its own local copy, which the caller never sees, so none of the seven session classes cleared the handle. `IsOpen()` stayed true forever after the first close &mdash; the very thing it was added to report &mdash; and a second close was a double free. Ten handles across seven classes now clear ([#1046](https://github.com/objeck/objeck-lang/issues/1046))
  * **`--nursery` now reduces memory on Windows** &mdash; the young region was reserved *and committed* at its full 128 MB, so lowering the limit moved the collector's boundary without changing what the process charged to the system. POSIX got this for free from lazy commit ([#841](https://github.com/objeck/objeck-lang/issues/841))
  * **A native library could store an object the collector tracks without a write barrier** &mdash; `AllocateObjectNative` puts an allocation in the old generation, where nothing scans it, so a native library storing a received object left the collector unaware of the reference. Three sites in the ONNX binding had it wrong. The VM now exposes a barrier a native library can call, and `check_native_write_barrier.py` holds every native store to it in CI, so the next one is caught rather than written ([#864](https://github.com/objeck/objeck-lang/issues/864))
  * **A failed ONNX session was undetectable, and two families then crashed on it** &mdash; `onnx_new_session` wrote its return slot only when it succeeded, so a failed session returned whatever the caller had left there. Nothing exposed the failure, and YOLO and ResNet then called a method on the Nil result &mdash; the program died inside the library with the real cause on stderr much earlier. Every session class now answers `IsOpen()`, and inference returns Nil instead of calling into a null handle ([#1028](https://github.com/objeck/objeck-lang/issues/1028))
  * **Every ONNX inference printed to the program's stdout** &mdash; progress and timing went to `std::wcout` at thirteen sites across every model family, four of them behind a `#ifdef _DEBUG` that had been commented out and never restored. A library has no business on its caller's stdout: anything that pipes or parses its own output got these interleaved into the data. All of them now go to stderr, where every error path in the same file already wrote ([#1030](https://github.com/objeck/objeck-lang/issues/1030))
  * **`API.Ollama` advertised streaming completions and had none** &mdash; every request it can make sends `"stream": false` and there is no alternative path, but the class comment claimed otherwise and `code_doc` published that claim in the API docs. It now says what actually happens: a request completes before returning, so a long generation produces nothing until it finishes ([#1027](https://github.com/objeck/objeck-lang/issues/1027))
  * **A generic array parameter did not resolve across a library boundary** &mdash; `Vector[]<T>` as a parameter compiled inside a library and callers in that same library reached it, but a program linking the library was told the method did not exist, offering an alternative whose signature was missing its `[]`. The array dimension was dropped reading the signature back out of the `.obl`, which is why the same type worked as a return value and failed as a parameter ([#998](https://github.com/objeck/objeck-lang/issues/998))
  * **Build and test plumbing** &mdash; the generated POSIX `Makefile`s are treated as build output rather than tracked source, so building one architecture no longer leaves tracked files modified ([#1005](https://github.com/objeck/objeck-lang/issues/1005)); a missing `expect` let the debugger suite skip every test and still report success ([#1022](https://github.com/objeck/objeck-lang/pull/1022)); and `cov_scan.sh` reported a submission Coverity had refused, which also deleted the archive it had just promised to keep ([#1031](https://github.com/objeck/objeck-lang/pull/1031))

### v2026.9.7 ✅
  * **A model can be evaluated honestly, end to end** &mdash; `System.ML` could fit a model but not measure one. Nothing held a scaler's statistics across a train/test split, so test data was scaled by its own numbers, which leaks the test distribution into the result and flatters any model read at a fixed false-alarm rate. `FeatureScaler` and `TableEncoder` learn from the training split and reapply it, `StratifiedKFold` keeps each class's ratio in every fold from a seed, and `CrossValidation->Evaluate` runs the folds over a common `ScoreModel` interface, reporting per-fold values alongside the mean and spread ([#999](https://github.com/objeck/objeck-lang/pull/999))
  * **Metrics that take a score rather than a decision** &mdash; `RecallAtFpr`, `ThresholdAtFpr`, `AucRoc`, `AveragePrecision`, `RocCurve` and `PrCurve`. A `Bool` array is already thresholded, so it cannot say what recall would be at a different false-alarm rate ([#983](https://github.com/objeck/objeck-lang/pull/983))
  * **Classifiers that split continuous features directly** &mdash; `DecisionTreeClassifier`, `RandomForestClassifier` and `GradientBoostedClassifier`. The existing trees take `Bool[,]`, so data had to be quantile-binned first &mdash; and the binning discards the very thresholds a tree exists to find ([#984](https://github.com/objeck/objeck-lang/pull/984))
  * **A `Bool[]` returned from a method was mishandled in three separate places** &mdash; comparing such a call against `Nil` would not compile, subscripting the result read several bytes instead of one, and `Bool->New[src]` returned the source array rather than a copy. A `Bool[]` held in a local was correct throughout, which is what disguised all three. The indexed-call defect was a silent wrong answer rather than an error, and it broke a different expression on each platform, so from any one machine it looked like an isolated quirk ([#988](https://github.com/objeck/objeck-lang/issues/988))
  * **Every comparison involving NaN returned true on Windows** &mdash; NaN compared equal to 1.0 while being both less than and greater than it, and the same program answered differently on Linux and macOS. Two causes in opposite directions: the VM is built with `/fp:fast`, which permits assuming operands are not NaN, and the amd64 JIT read `ucomisd`'s zero flag for equality without the parity flag that separates equal from unordered ([#1000](https://github.com/objeck/objeck-lang/issues/1000))
  * **Nineteen library divisions aborted the VM on ordinary input** &mdash; a division by zero raises a runtime error rather than yielding infinity, so an unguarded one ends the program. A library should not oblige every caller to wrap an ordinary call in `?->` just to survive its own input. `CsvColumn->Average(1, 1)`, the average of an empty range on a populated table, was confirmed as a live crash before anything was changed
  * **The collector's old generation moved to open addressing** &mdash; Windows `binarytrees` went from 2.392s to 1.789s ([#871](https://github.com/objeck/objeck-lang/issues/871))
  * **`obu verify <archive> <SHA256SUMS>`** &mdash; the integrity check `update` already performs, exposed for anyone who downloads with `curl`. A mismatch and a check that could not be carried out are different exit codes, so a script can tell a wrong file from an inconclusive result ([#723](https://github.com/objeck/objeck-lang/issues/723))
  * **An update no longer aborts because something briefly held a file** &mdash; `obu update` renames the install tree aside, and Windows denies renaming a directory while another process holds a handle inside it, which antivirus does to a binary whose bytes just changed. A single attempt saw `Access is denied` and unwound the whole update; it now retries. It always failed safe, but the only recourse was to run it again ([#1006](https://github.com/objeck/objeck-lang/issues/1006))
  * **A toolchain-version mismatch said the wrong side was stale** &mdash; the error read "the LIBRARY appears to be compiled with a different version", which sends you to rebuild libraries that are already current, when the stale side is usually the tool reading them. It now names which side is which, and both versions ([#1010](https://github.com/objeck/objeck-lang/issues/1010))
  * **`obc` names the library that would supply an unresolved class** &mdash; `Unable to resolve external library class: X; check library path` was true and unactionable: the path is almost always right, one entry is missing from `-lib`, and finding which one meant grepping the library sources. It now says `Add it with '-lib net_server'` ([#1015](https://github.com/objeck/objeck-lang/pull/1015))
  * **The `@web` library group never worked, and `@` groups now expand anywhere in the list** &mdash; `-lib @web` failed on any program, even one that never touched HTTP, because the group omitted `net_server.obl`. Separately, when building a library, `-lib @std` worked while `-lib @std,misc` tried to open a file named `@std.obl`. Both fixed, and the groups are documented in `docs/cli_options.md` &mdash; `obc`'s usage had never mentioned `@` at all ([#1014](https://github.com/objeck/objeck-lang/pull/1014), [#1016](https://github.com/objeck/objeck-lang/pull/1016))

### v2026.9.6
  * **`System.ML` stopped returning wrong numbers** &mdash; column sums and averages truncated every fractional value, so `LinearSolver` reported the wrong R-squared; `KMeans->Group` could hand back empty groups depending on the order of its labels, and the Dunn index then divided by zero ([#903](https://github.com/objeck/objeck-lang/issues/903))
  * **`Matrix2D` says no instead of stopping the program** &mdash; operations on shapes it cannot combine return `Nil`, a `Nil` operand no longer crashes the native code, `Inverse` of a non-square matrix no longer hangs, and `NeuralNetwork->Train` refuses an input or target that is not a column of the right height ([#903](https://github.com/objeck/objeck-lang/issues/903))
  * **`obi` and the embedding API speak HTTP/2 and HTTP/3** &mdash; both run code through the module build, which never got the flags or the libraries `obr` has, so they quietly used HTTP/1.1. Every CI leg now runs one program under both and fails if they disagree ([#897](https://github.com/objeck/objeck-lang/issues/897))
  * **Two ARM64-only crashes** &mdash; `obi` aborted with &ldquo;double free or corruption&rdquo; on Linux ARM64, because the REPL's module and the VM it links were built with different macros and disagreed about the objects they share; and a program using function references could be killed at exit by a race between the collector's marking threads ([#913](https://github.com/objeck/objeck-lang/issues/913))
  * **`API.OpenAI` answers, and sends the picture** &mdash; a text `Respond` posted its request and never read the reply, so the call was billed and returned `Nil`; every image call sent its request as text ([#901](https://github.com/objeck/objeck-lang/issues/901))
  * **`obi` exits when its input ends** &mdash; piping a program into the REPL, or closing its input, left it running forever ([#917](https://github.com/objeck/objeck-lang/issues/917))

### v2026.9.5
  * **Linux ARM64 runs on every ARM64 CPU** &mdash; v2026.9.4 was built for the build server's CPU and needed its SVE instructions, so it stopped with "Illegal instruction" on a Raspberry Pi 4 or 5, Graviton2, Snapdragon X or Apple silicon in a Linux VM. It now targets ARMv8-A at no measured cost, and CI runs every build on an emulated CPU without SVE ([#893](https://github.com/objeck/objeck-lang/issues/893))
  * **Programs with threads exit and join safely** &mdash; a runtime error or `Runtime->Exit` while another thread ran crashed the VM after printing its message, on every platform ([#877](https://github.com/objeck/objeck-lang/issues/877)); `Thread->Join` could fail with "Unable to join thread!" when a collection ran while it waited ([#874](https://github.com/objeck/objeck-lang/issues/874))
  * **Garbage collector** &mdash; a closure's captured values could be freed while still held ([#881](https://github.com/objeck/objeck-lang/issues/881)), plus fixes for objects lost at the end of a small nursery, a thread-exit race and objects allocated at twice their size. A heap verifier (`OBJECK_GC_VERIFY`) checks the collector's invariants and runs nightly on all five platforms
  * **Sorting** &mdash; `Int->Sort` and its `Float`, `Char` and `Byte` counterparts could exhaust the call stack on input of a particular shape; they are about 15% faster, `ArraySort->Sort` sorts an array in place, and `Data.CSV` medians are about 12 times faster ([#887](https://github.com/objeck/objeck-lang/issues/887))
  * **Compiler** &mdash; dozens of fixes for lambdas, enums and conditional expressions, most found by holding every test to the same output across optimization levels and JIT settings, and by a program fuzzer
  * **Integer arithmetic has one definition** &mdash; shared by the interpreter, compiler and both JITs. `INT64_MIN / -1` no longer stops the program, and `>>>` takes its shift count modulo 64 like `<<` and `>>`, so `-1 >>> 64` is `-1` rather than `0`

## Downloads

**Latest Release:** [v2026.9.7](https://github.com/objeck/objeck-lang/releases/latest)

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













