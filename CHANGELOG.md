# Changelog

All notable changes to Objeck will be documented in this file.

## [v2026.5.3] - 2026-05-18

### New Features
- **Three-tier `select` dispatch** (AMD64 + ARM64 JIT): single-case `select` compiles to a direct compare-and-jump; 2–5 integer cases use a linear scan; 6+ dense integer cases emit a native O(1) jump table (`JMP_TABLE`/`JMP_TABLE_SLOT` opcodes); sparse or string `select` uses a binary search tree — matching the fastest dispatch strategy for each shape automatically.

### Bug Fixes
- Fixed `HttpRequestHandler` and `HttpsRequestHandler`: `ReadLine()` can return `Nil` on a dropped or errored connection; calling `->Size()` on `Nil` produced a SIGSEGV in the MCP server and any HTTP server that receives an abrupt client disconnect before sending a request line.
- Fixed `String->Split(Char)`: last token was sliced using `@string->Size()` (array capacity) instead of `@pos` (logical string length), producing an oversized trailing token on strings that did not fill the backing array.
- Fixed `bench_spectralnorm_native` benchmark: allocating arrays inside a `native` JIT function caused op-stack imbalance during nested JIT-to-interpreter callbacks, producing a garbage result (~3.84e-156 instead of ~1.274).

### Performance
- `bench_spectralnorm_native`: rewrote `MultiplyAv`/`MultiplyAtv` with an incremental floating-point denominator, eliminating `I2F` conversions from the inner loop (2000×2000×40 iterations). Only two integer-to-float conversions now occur per outer row instead of per element.

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

## [v2026.2.0] - 2026-02-12 ✅ Current Release

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
