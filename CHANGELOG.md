# Changelog

All notable changes to Objeck will be documented in this file.

## [Unreleased]

### New Features
- **String interpolation — expressions, format specifiers, and `String->Format`**: `"{$...}"` interpolation now accepts arbitrary expressions (arithmetic, comparison, logical — e.g. `"{$i + 1}"`, `"{$a * b - c}"`, `"{$x > y}"`), not just bare variables and calls. Inline format specifiers in Python/.NET colon syntax control precision, width, alignment, and radix: `"{$pi:.2}"`, `"{$n:5}"` / `"{$n:05}"`, `"{$s:<10}"` / `"{$s:>10}"`, `"{$v:x}"` (hex), `"{$v:b}"` (binary). A new positional helper `String->Format("{0} = {1}", a, b)` (with `{{`/`}}` escaping) complements interpolation for reusable/runtime format strings. Backed by new library helpers `Float->ToString(value, precision)` and `String->PadTo(width, ch, is_left)`; specifiers desugar onto existing helpers with no new VM opcodes.
- **Generics: bounds, compound/F-bounds, and variance**: type parameters gain `T : A & B` compound bounds (a concrete argument must satisfy every bound), F-bounded constraints `T : Compare<T>`, and declaration-site variance — `out T` (covariant) and `in T` (contravariant) — checked soundly in both directions and preserved across the `.obl` library boundary. Existing invariant generics and syntax are unchanged (`out` stays a usable identifier); generic type-mismatch diagnostics now render readable types (`Hash<String, IntRef>`).
- **Multithreaded stop-the-world garbage collection**: the collector now coordinates safely across threads. Mutators poll safepoints in the interpreter dispatch loop and at allocation, park while a collection runs, and bracket blocking syscalls (thread join/sleep, socket I/O) so a stop-the-world pause can always proceed; the AMD64 and ARM64 JITs emit safepoint polls at every label. Validated on Windows, Linux, and macOS across x64 and ARM64.
- **Reproducible library builds**: compiling unchanged `.obs` source now produces byte-identical `.obl` output. Anonymous classes are named from their source location instead of a random token, and string-`select` cases and closure declarations are emitted in a stable (source/`mthd_id`) order rather than heap-pointer order, so committed libraries no longer churn on every rebuild.

### Security
- **TLS server-certificate verification is now on by default**: secure client sockets and DTLS connections verify the server certificate chain. Self-signed certificates for local testing can be allowed with `OBJECK_TLS_INSECURE_SKIP_VERIFY=1`.
- **Hardened untrusted-input deserialization**: object deserializers reject hostile 64-bit sizes, a `Char[]` read trap no longer lets an attacker-controllable offset overflow the heap, and additional untrusted-input paths were hardened against memory corruption.

### Bug Fixes
- **GC value corruption under heavy thread churn**: a thread executing its top-level method (or holding an object only on a parked thread's operand stack) could have those live references missed by the mark phase and reclaimed mid-use. The mark phase now scans the top-level frame and every thread's operand stack, exactly matching what the fixup phase rewrites. Also: the young-generation bump pointer is bounded with a compare-and-swap so a collection can't run off the region, a thread's GC roots are deregistered before its stacks are freed (no use-after-free during teardown), the JIT join/sleep paths park correctly, and the write-barrier flag is accessed atomically on weakly-ordered ARM64.
- **Serialization correctness**: integer arrays dropped half their elements; object-nested integer arrays truncated 64-bit values to 32 bits; object function-reference fields desynced and lost data; several further 64-bit / `Char[]` / `Float`-slot serialization bugs were fixed.
- **Compiler**: constant propagation emitted a stale literal after a non-constant reassignment; LICM hoisted trapping `DIV_INT`/`MOD_INT` out of loops that may never execute.
- **Debugger**: locals and fields appearing after a `Float` field/local showed wrong values.
- **macOS / launcher**: portable application bundles failed when launched from outside their own directory; a stale `Objeck.app` bundle version was corrected.

### Performance
- **ONNX on macOS**: compiled CoreML models are persisted across runs (~35× faster warm start), and `Ort::TypeInfo` is kept alive while reading tensor type/shape.

### Documentation / Infrastructure
- AMD64/ARM64 JIT and VM/memory-manager architecture READMEs expanded; `architecture.md` documents the cooperative multithreaded stop-the-world model.
- Build: resolved `D9025`/`LNK4099`/`LNK4098` warnings and a `NativeCode` ODR violation.
- CI: Windows runners pinned to `windows-2022` (VS2022 toolset v143).

## [v2026.6.0] - 2026-06-07

### New Features
- **`System.AI` library** (`-lib ai` / `@ai`): classic AI in the standard library — graph search (`Dijkstra`, `AStar`, `BreadthFirst`, `DepthFirst` over a shared best-first core), adversarial game search (`Minimax` with alpha-beta pruning, `MonteCarloTreeSearch`), metaheuristics (`GeneticAlgorithm`, `SimulatedAnnealing`, `HillClimbing`), and tabular reinforcement learning (`QLearning`, `Sarsa`, `MarkovDecisionProcess` value iteration); all stochastic algorithms are seedable for reproducible runs
- **`System.ML` overhaul**: 13 new estimators — `RidgeRegression`, `LassoRegression`, `ElasticNet`, `Perceptron`, `SVM`, `PCA`, `GaussianNaiveBayes`, `AdaBoost`, `DBSCAN`, `GaussianMixture`, `KDTree`, `RegressionTree`, `GradientBoostedTrees`; real recursive `DecisionTree` and voting `RandomForest`; `KMeans` k-means++ seeding, iteration cap, and empty-cluster handling; `NeuralNetwork` hidden/output bias vectors (clean XOR convergence); `LinearRegression`/`LogisticRegression` intercepts, stable sigmoid, L2 regularization, and `Score`; seedable `System.ML.Random`; uniform `Fit`/`Predict`/`Score`/`IsFitted`/`Store`/`Load` API across every estimator; `ml.obs` split into seven thematic source files
- **`record` types**: `record Point { @x : Int; @y : Int; }` generates the constructor and accessors; `record : readonly :` omits setters and the compiler rejects field assignment outside constructors; supports generics, inheritance, and user-defined member overrides
- **Tail Call Optimization (TCO)**: self-recursive tail calls rewritten to jumps, eliminating stack growth (`-opt s1`+)
- **Loop-Invariant Code Motion (LICM)**: hoists `arr->Size()` reads and pure arithmetic out of loop bodies (`-opt s2`+)

### Breaking Changes
- `RandomForest->Train` is now `Fit` (uniform estimator API)
- Stored `NeuralNetwork` model files must be regenerated (serialized format gained bias vectors)

### Bug Fixes
- **VM/JIT frame-dependent traps**: traps reading interpreter locals (`Serializer->Write`, `Date->New`, file-time queries) crashed once a method crossed the auto-JIT threshold; such methods now stay interpreted on AMD64 and ARM64
- **ARM64 JIT**: stale `self` reload after JIT-to-interpreter callbacks; JIT-to-JIT errors are now diagnosable; operand-kind compile guards ported from AMD64
- **Float equality on array elements**: was compiled as an integer compare in the JIT
- **Bool array literals**: every bool static-array literal after the first silently received the first literal's data (broken literal-pool comparator); literal dedup now works for all array types; array dimensions capped at 8 with a proper diagnostic
- **Launchers**: Windows defect sweep; macOS version-check modernization
- **Doc parser**: inline backticks and bare tildes in descriptions no longer break API doc generation
- **XML parser**: truncated documents (elements left unclosed at end of input) and stray closing tags were silently accepted; both now fail cleanly. `XmlElement->DecodeString` dropped the trailing semicolon when decoding `&apos;`

### Libraries
- **Data.XML conveniences** (additive; raw `SetContent`/`GetContent`/`GetValue` contract unchanged): `XmlElement->EncodeText` escapes exactly the five markup characters while leaving whitespace readable; `SetEncodedContent` produces well-formed output; `GetDecodedContent` and `XmlAttribute->GetDecodedValue` return entity-decoded text

### Performance
- Auto-JIT now compiles methods containing `MTHD_CALL` after 10 invocations (5–15% speedup across benchmarks)
- Interpreter fast-path extended with 15 additional inline opcodes (comparisons, bitwise, shifts, logical)
- `bench_matrix_multiply` −14%, `bench_dead_code` −15%, `bench_array_intensive` −12%, `binarytrees` −7%, `mandelbrot` −6%

### Documentation / Infrastructure
- Library aliases documented: `-lib @std`/`@ml`/`@game` and the new `@ai` group, user-editable via `lib/configobjk.ini`; AI/ML developer guide gains `System.ML` and `System.AI` sections with runnable examples
- API reference regenerated (438 pages; `System.AI` classes grouped correctly); `architecture.md` mermaid diagrams fixed and the GC description corrected (young generation moves survivors)
- ARM bootstrap (`update_version_arm.sh`) built `opencv` before its `json` dependency — reordered so a clean ARM bootstrap succeeds
- CI hardening: vcpkg installs retry on transient CDN failures; `mcp_server_test` validates JSON-RPC bodies before accepting; flaky network tests quarantined with failure observability; regression timeouts added

## [v2026.5.4] - 2026-05-28

### Bug Fixes
- **`.obe`/`.obl` format detection**: correctly handles the edge case where a new-format size-header LSB collides with the `0x78` zlib CMF byte (fixed Windows CI debugger tests)
- **LSP shell script permissions**: all `tools/lsp/` shell scripts now have the execute bit set in git, fixing `Permission denied` in release CI

### Infrastructure
- Release workflow: `git checkout -f master` prevents a dirty-tree abort when committing `api.zip` from a tag-based build

## [v2026.5.3] - 2026-05-24

### New Features
- **Three-tier `select` dispatch** (AMD64 + ARM64 JIT): single-case `select` compiles to a direct compare-and-jump; 2–5 integer cases use a linear scan; 6+ dense integer cases emit a native O(1) jump table (`JMP_TABLE`/`JMP_TABLE_SLOT` opcodes); sparse or string `select` uses a binary search tree — matching the fastest dispatch strategy for each shape automatically.

### Bug Fixes
- Fixed `HttpRequestHandler` and `HttpsRequestHandler`: `ReadLine()` can return `Nil` on a dropped or errored connection; calling `->Size()` on `Nil` produced a SIGSEGV in the MCP server and any HTTP server that receives an abrupt client disconnect before sending a request line.
- Fixed `String->Split(Char)`: last token was sliced using `@string->Size()` (array capacity) instead of `@pos` (logical string length), producing an oversized trailing token on strings that did not fill the backing array.
- Fixed `bench_spectralnorm_native` benchmark: allocating arrays inside a `native` JIT function caused op-stack imbalance during nested JIT-to-interpreter callbacks, producing a garbage result (~3.84e-156 instead of ~1.274).
- **AMD64 JIT trig**: `sin`, `cos`, `tan`, `asin`, `acos`, `atan`, `atan2`, `sinh`, `cosh`, `tanh` were using x87 `fsin`/`fcos`/`ftan` — replaced with `call_xfunc` to use the C runtime (consistent with ARM64 and the LOG/EXP fixes)
- **AMD64 JIT float input**: `REG_FLOAT` as the source operand of `call_xfunc`/`sqrt`/`round` caused a crash due to incorrect register state when a float value was loaded from memory immediately before the dispatch
- **Inline optimizer jump tables**: `InlineMethod` did not shift `JMP_TABLE`/`JMP_TABLE_SLOT` label operands by `jump_inline_offset` when inlining methods containing `select` jump tables, causing every slot to resolve to ip=0
- **`CleanLabelsLocation` end-of-stream**: consecutive `LBL` nodes at the very end of an instruction list read one instruction past the end of the stream
- **`CanInlineMethod` conflict check**: used the `JUMP_OFF_INC` constant instead of the actual `jump_inline_offset` accumulator, producing false-positive label conflicts that unnecessarily blocked inlining
- **`String->SubString`**: crash on negative or zero length argument (#534)

### Security / Performance
- **Binary file integrity**: `.obe`/`.obl` files now store the uncompressed size as a 4-byte header before the zlib stream — eliminates the allocation guessing/doubling loop on load and lays the groundwork for future integrity checks
- Switched from `compress()` (level 6) to `compress2()` at `Z_BEST_COMPRESSION` (level 9) — ~10–15% smaller binary files at no runtime cost
- Replaced `calloc` with `malloc` in `CompressZlib`/`UncompressZlib` — removes wasteful zero-initialisation of buffers that are immediately overwritten by the codec
- **Backward-compatible**: files in the old raw-zlib format (CMF byte `0x78`) are automatically detected and continue to load without recompilation

### Performance
- `bench_spectralnorm_native`: rewrote `MultiplyAv`/`MultiplyAtv` with an incremental floating-point denominator, eliminating `I2F` conversions from the inner loop (2000×2000×40 iterations). Only two integer-to-float conversions now occur per outer row instead of per element.

### Infrastructure
- Consolidated the `objeck-lsp` repository into `tools/lsp/` — LSP is tightly coupled to each toolchain build and must be updated with every release
- Rewrote CI `build-lsp` job: Ubuntu runner, builds Linux x64 toolchain, compiles `objeck_lsp.obe` via `build_server.sh`, packages VS Code extension with `vsce`, assembles versioned `objeck-lsp_VERSION.zip`
- Added `publish-vscode` CI job: publishes the VS Code extension to the marketplace on release using the `VSCE_PAT` secret
- `build_server.sh` / `build_server.cmd`: `OBJECK_ROOT` is now configurable via environment variable (defaults to `../../..` relative to `tools/lsp/server/`)

## [v2026.5.2] - 2026-05-17

### New Features
- **HTTP/2 client** (`-lib net_h2`): New `Http2Client` class for persistent multiplexed TLS connections using ALPN h2 via nghttp2. Supports GET, POST, PUT, DELETE, PATCH and `Quick*` static convenience methods that accept a `Url` object for one-shot requests. Linux, macOS, and MSYS2.
- **HTTP/3 client** (`-lib net_quic`): New `Http3Client` class for QUIC connections via ngtcp2 + GnuTLS + nghttp3. Same API as `Http2Client` with `Quick*` statics for one-shot use. Linux and macOS only.
- **OpenAI Moderation**: `Moderation->Check()` returns per-category flags and confidence scores.
- **OpenAI Batch**: `Batch->Create()`/`Get()` for async 50%-cost batch requests (up to 50k at a time).
- **Gemini Files API**: upload, list, get, and delete files via `FileManager`.
- **Gemini Context Caching**: `CachedContent->Create()` for server-side prompt caching with configurable TTL.
- **Gemini Search Grounding**: `Model->GenerateContentWithGrounding()` anchors responses in live Google Search results.
- **Gemini Batch Embeddings**: `Model->BatchEmbedContent()` embeds multiple texts in one round-trip.
- **EmbeddingValues wrapper**: avoids `Float[]` as generic type parameter.
- **Socket options**: New methods on `TCPSocket` and `TCPSecureSocket` — `SetKeepAlive`, `SetNoDelay`, `SetRecvTimeout`, `SetSendTimeout`, `SetRecvBufferSize`, `SetSendBufferSize`, `SetConnectTimeout`. `TCPSecureSocket` adds `SetMinTLSVersion`, `SetVerifyPeer`, `GetCertFingerprint`.
- **`SO_REUSEADDR`** on `TCPSocketServer::Bind()` survives TIME_WAIT restarts; `IPSocket::Open()` falls through to next address on `socket()` failure.

### Bug Fixes
- Fixed `obr` (VM executable) absent from all platform archives — `libnghttp2-dev` was missing from the main `build` CI job; equivalent gaps on macOS and Windows
- Fixed `HttpsClient` and `HttpClient` redirect not following POST/PUT bodies
- Fixed HTTP/1.1 redirect handling for POST and PUT; added retry-on-reset parity across verbs
- Fixed 8 WebSocket bugs; replaced per-byte reads with bulk `ReadBuffer` I/O
- Fixed MCP server hang on shutdown and crash-on-stop
- Fixed MSVC compatibility: NOMINMAX ordering, `std::min()` ambiguity; Release VM/compiler optimizations
- Fixed Realtime API null-safety issues
- Fixed ARM64 Windows ONNX `.vcxproj` configs (`Release-QNN|ARM64`, `Release-DML|ARM64`); removed non-existent `onnxruntime_providers.lib` from link deps

### Libraries
- **HTTP/2** (`net_h2`): `Http2Client` — persistent HTTP/2 session with full verb support and URL-based `Quick*` convenience functions.
- **HTTP/3** (`net_quic`): `Http3Client` — QUIC-based HTTP/3 session with full verb support and URL-based `Quick*` convenience functions.
- **HTTP/1.1** (`net`, `net_secure`): Added `HttpClient->PATCH`, `HttpsClient->PATCH`. Fixed redirect handling for POST and PUT.

### Infrastructure
- ARM64 Windows OpenCV: switched to split module libs via vcpkg; corrected `Release|ARM64` and `Debug|ARM64` `.vcxproj` configs
- Committed `nghttp2` headers and import libraries to `core/lib/nghttp2/win/`; Windows builds are now fully self-contained without vcpkg
- Multi-level NuGet restore: VS-bundled `nuget.exe` → PATH (Chocolatey on CI runners) → download from nuget.org
- Added HTTP/3 dependencies (`libngtcp2-dev`, `libngtcp2-crypto-gnutls-dev`, `libnghttp3-dev`, `libgnutls28-dev`) to Linux and macOS `release-build.yml`; added ngtcp2 GnuTLS backend build step for macOS
- Hardened `deploy_windows.cmd` with artifact existence checks after each `devenv` build
- Added binary verification CI step: `obr`, `obc`, `obd`, `obi`, `obb` + 50+ API HTML files must exist before artifact upload
- CI: CodeQL v4, node24-compatible Actions, nghttp2/ngtcp2 on all platforms
- `net_h2.obl` and `net_quic.obl` compiled as part of the standard library build in `update_version.sh`
- macOS Xcode project: added `OBJECK_HAS_NGTCP2` flag and `-lngtcp2 -lngtcp2_crypto_gnutls -lnghttp3 -lgnutls` linker flags

### Website
- Added Changelog card to home page (`docs/web/index.html`) creating a clean 2×3 six-card grid

## [v2026.4.3] - 2026-04-12

### New Features
- **DAP debugger hover**: Hovering an object shows `ClassName { field=val, ... }` with one-level instance field expansion via `FormatObjectForDap`
- **DAP instance/class variable scopes**: Variables pane now shows separate Locals, Instance, and Class scopes with correct memory mapping
- **Configurable JIT threshold**: Auto-JIT invocation count can now be tuned at startup
- **Editor setup refresh**: Updated VS Code, Sublime Text, and gvim/Vim DAP+LSP setup for Windows, Linux, and macOS with per-platform instructions

### Bug Fixes
- Fixed DAP step-into crash, step-over/step-out scoping, and disconnect access violation
- Fixed DAP stdout/stderr corruption — redirected program output through capture pipes so DAP protocol stream stays clean
- Fixed DAP variable display, source path resolution, and pipe crashes
- Fixed DAP variable memory mapping to match CLI debugger
- Fixed LSP server crash on `textDocument/codeAction` with inferred locals
- Fixed LSP hover position conversion from 1-based to 0-based
- Fixed JIT S2F callback param count causing segfault on `String:ToFloat`
- Hardened HTTPS client against null `ReadLine` on connection failures

### Infrastructure
- DAP integration test suite (`programs/regression/run_dap_tests.sh`)
- Removed redundant `HandleEvaluate` fallback in DAP adapter (consolidated into `EvaluateForDap`)

## [v2026.4.2] - 2026-04-06

### New Features
- **JIT local variable register cache** (AMD64 + ARM64): Keeps values in registers after store, avoids redundant reloads, evicts on demand when register pool is exhausted — ~3x speedup across all benchmarks
- **DTLS (Datagram TLS) support**: New `DTLSSocket` and `DTLSSocketServer` classes for secure UDP communication (IoT, VoIP, gaming)
- **Editor guide**: New `docs/editors.md` with Vim, Emacs, Sublime, Neovim, and DAP debugging setup

### Libraries
- **Gemini API**: Added 2.5 Pro/Flash model constants, system instruction support
- **Ollama API**: Configurable host, Options class (temperature/top_p/top_k), Tool class for function calling
- **OpenAI API**: New `Embedding` class, `Models` constants (GPT-4.1, O3, O4-mini)
- **ML library**: Fixed EuclideanDistance/StdDev bugs, added `LinearRegression` and `LogisticRegression`
- Hardened JSON, JSON stream, and XML parsers against malformed input
- Hardened HTTPS client against null ReadLine on connection failures

### Performance
- **Link-time optimization**: Added `-flto=auto` across all GCC Makefiles (AMD64 and ARM64)
- **ARM64 native CPU tuning**: `-mcpu=native` auto-detects RPi5 (Cortex-A76) and Jetson Orin (Cortex-A78AE)

### Bug Fixes
- Fixed all MSVC and GCC compiler warnings
- Fixed doc generator error on `@hidden` tag

## [v2026.4.1] - 2026-04-05

### Changes
- Fixed ONNX API documentation: trimmed bundle-level doc comment that was dumping code examples into the API page description
- Fixed release pipeline: macOS .pkg staple non-fatal, Windows ARM64 signing skip (cross-compiled), temp directory creation
- Rebuilt libraries with v2026.4.1 version

## [v2026.4.0] - 2026-04-05

### New Features
- **Debug Adapter Protocol (DAP)**: Full VS Code debugging support with breakpoints, stepping, variable inspection, and stack traces
- **Conditional Breakpoints**: Break on expressions (e.g., `b file.obs:30 when count > 5`)
- **Debugger ANSI Colors**: Syntax-highlighted source listing with color-coded breakpoints, current line, and line numbers
- **Readline Support**: Command history and line editing in the interactive debugger
- **macOS .pkg Installer**: Signed and notarized native package installer with PATH auto-configuration
- **SSE Streaming**: Server-Sent Events support for HTTP client/server
- **ODBC BigInt & Connection Strings**: Extended database connectivity with BigInt type and connection string authentication

### Performance Optimizations
- **3.3x binarytrees speedup**: Young-gen bump allocator, direct JIT-to-JIT calling, atomic CAS mark bits
- **MTHD_CALL JIT whitelist** (x64 + ARM64): Methods containing method calls can now be JIT-compiled
- **GC thread safety**: Memory barriers in PushFrame/PopFrame paired with acquire fences in GC marking — fixes intermittent threading segfaults on Linux
- **JIT instance method inlining fix**: Save/restore INSTANCE_MEM around inlined code

### Networking & I/O
- Socket default receive timeouts to prevent hung connections
- Fixed ReadBytes partial read bug (short reads no longer silently truncated)
- HTTP client/server stack hardening with loopback regression tests

### Libraries
- **OpenCV**: Contours, VideoWriter, transforms, normalization, 15 new image processing functions
- **ODBC**: Transactions, error handling, schema discovery, BigInt support
- **ONNX**: Phi-3 Vision multimodal inference, unified build system (DML/CUDA/QNN/CoreML)
- **Collections**: Hash auto-resize at 75% load, Vector in-place Remove
- **JSON**: Escape and keyword parsing fixes

### CI/CD & Infrastructure
- **Apple code signing**: Developer ID Application + Installer certificates with notarization
- **Windows code signing**: Sectigo certificate with SHA-256 timestamping
- CI library rebuild on all platforms (not just bootstrap)
- Threading test retry logic for CI runner timing sensitivity
- 14 debugger regression tests with expect-based automation
- HTTP loopback and network buffer regression tests

### Bug Fixes
- Fixed constructor early return crash
- Fixed CSV.Median, CSV.Average, URL encoding, Response.ToString nil check
- Fixed debugger list command corrupting Windows console on Unicode source files
- Fixed OpenCV Xcode header paths (ABI namespace mismatch with Homebrew)

## [v2026.2.1] - 2026-02-26

### New Features
- **Try/Otherwise Error Handling**: `Try()` and `Otherwise()` compiler intrinsics on the `Base` class for safe method chaining with graceful error recovery (e.g., `obj->Try()->Method()->Otherwise(default)`)

### Performance Optimizations
- **4.38x speedup on nbody benchmark** via increased method inline limit (128→256 bytes), allowing getter/setter-heavy code to be fully inlined and JIT-optimized
- **Common Subexpression Elimination (CSE)**: New compiler pass at s2+ eliminates redundant computations within basic blocks
- **Dead Code Elimination**: New compiler pass removes redundant jumps to immediately-following labels
- **Bug Fix**: Division-by-zero crash in constant folding — `DIV_INT`/`MOD_INT` now guards against zero divisor
- **Bug Fix**: Dead condition in `InstructionReplacement` — `&&` corrected to `||` (both types could never match simultaneously)
- **JIT Safety**: Division-by-zero guards added to `ProcessIntFold` in both x64 and ARM64 JIT backends
- **Fixed Try/Otherwise crash**: VM segfault when calling non-virtual methods (e.g., `Size()`, `ToUpper()`) on Nil objects inside `Try()` chains — call stack and instruction pointer now properly unwound during error recovery
- **Fixed debugger build error on Windows**: `HELP_COMMAND` enum collision with Windows SDK macro (`WinUser.h`)
- **Benchmarks**: 10 new performance benchmark programs with measurement tooling ([details](docs/performance.md))

### Improvements
- **Editor Syntax Highlighting**: Updated Monaco (playground) and VSCode syntax definitions to support `Try` and `Otherwise` as built-in keywords
- **Web Playground**: Updated version tag and editor keyword support for v2026
- **ARM64 CI Testing**: Enabled Linux ARM64 (`ubuntu-24.04-arm64`) and macOS ARM64 test execution in GitHub Actions
- **Documentation**: Bootstrap & cross-platform build workflow documented in `core/readme.md`

### Internal
- Refactored Try/Otherwise from expression-level keywords to method intrinsics on `Base` class for cleaner semantics
- Removed disabled legacy CI workflow (superseded by `ci-build.yml`)
- Refactored `OptimizeMethod()` with `RunPass` helper for cleaner pass management

## [v2026.2.0] - 2026-02-12

### New Features
- **NLP Library**: Comprehensive natural language processing with tokenization, TF-IDF, similarity, and sentiment analysis
- **Web Playground**: Browser-based Objeck playground at playground.objeck.org
- **Gemini 2.0/2.5 Support**: Google Gemini integration with audio capabilities
- **OpenCV Integration**: Real-time computer vision (face detection, image/video processing)
- **OpenAI Realtime API**: Support for `gpt-4o-realtime-preview` with text and audio
- **PCM16 Audio**: Recording and playback APIs via SDL2 mixer
- **Audio Conversion**: PCM16 to MP3 via Lame
- **ONNX Runtime**: Cross-platform ML inference support
- **Modern CLI Flags**: GNU-style flags (`--source`/`-s`, `--destination`/`-d`, `--debug`/`-D`) with full backward compatibility

### Performance Optimizations
- **Memory Manager**: O(1) lookups via `std::unordered_set`, in-place sweep, eliminated unnecessary thread creation
- **ARM64 JIT**: 11 critical fixes including multiply optimization, register targeting, FP register pool management
- **x64 JIT**: Instruction encoding optimizations with dynamic backpatching

### New Platform Support
- **Windows ARM64**: Full support with automated builds and code signing
- **Crypto Library**: Migrated from OpenSSL to mbedTLS for lighter footprint and ARM64 support

### Collections and String Improvements
- **String**: New methods (`Contains`, `Count`, character checks) and critical bug fixes (`Trim`, `TrimFront`, `Set`)
- **Collections**: 10+ bug fixes in `gen_collect.obs` (memory safety, iterator API, tree corruption, type issues)

### CI/CD and Infrastructure
- Fully automated release pipeline via GitHub Actions (build, sign, publish)
- Automated regression testing on every commit (350+ tests)
- Windows MSI code signing with timestamping
- Cross-platform CI for Windows x64/ARM64, Linux x64, macOS ARM64

### Compiler Improvements
- Better error messages with operator symbols, normalized type names, inline hints
- Fixed method chaining on array-indexed elements after cast (#524)

### Bug Fixes
- **String**: Fixed `ToString()` buffer size for Int and Float (returned buffer capacity instead of content length)
- ARM64 JIT: Fixed STRH/LDRH opcodes, memory offset scaling, bitwise NOT
- Memory Manager: Fixed root scanning, race conditions, redundant binary search
- Crypto: Fixed AES-256 CBC padding and key derivation

## [v2025.7.0] - 2025-07-01 ✅

### Added
- `Hash->Dict(..)` method to collections
- `Map->Dict(..)` method to collections
- `Vector->Zip(..)` method to collections

### Changed
- Updated documentation style
- Updated logos and branding
- Improved visual design

### Fixed
- Various bug fixes and stability improvements

## Previous Releases

For releases prior to v2025.7.0, please see the commit history on GitHub:
[https://github.com/objeck/objeck-lang/commits/master](https://github.com/objeck/objeck-lang/commits/master)

---

## Versioning

Objeck uses calendar versioning (CalVer) with the format `YYYY.MINOR.PATCH`:
- **YYYY**: Year of release
- **MINOR**: Minor version number (incremented for feature releases)
- **PATCH**: Patch number (incremented for bug fixes)

---

[Back to README](README.md)
