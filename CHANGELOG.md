# Changelog

All notable changes to Objeck will be documented in this file.

## [v2026.5.2] - 2026-05-16

### Bug Fixes
- Fixed `obr` (VM runtime) absent from all v2026.5.1 platform archives — root cause was `libnghttp2-dev` missing from the main `build` CI job dependency installs (was only in `build-docs` and CodeQL jobs)
- Fixed API docs (`docs/api.zip`) containing only style files in all prior releases — `build-docs` CI job now commits freshly generated `api.zip` back to `master` with `[skip ci]`

### Infrastructure
- Added `libnghttp2-dev` to Linux apt-get installs in the main `build` CI job (`release-build.yml`)
- Added `nghttp2` to macOS Homebrew installs in the main `build` CI job
- Added `nghttp2:x64-windows` and `nghttp2:arm64-windows` to Windows vcpkg installs in the main `build` CI job
- Added `vcpkg` include/lib paths to `core/vm/vs/vm.vcxproj` via `$(OBJECK_VCPKG_ROOT)` (falls back to `C:\vcpkg`) so Windows developer builds find nghttp2 headers/libs without manual environment variable setup
- Added "Verify required binaries" CI step after build: checks that `obr`, `obc`, `obd`, `obi`, `obb` all exist and that `doc/api/` contains 50+ HTML files before artifacts are uploaded
- `build-docs` job now commits regenerated `api.zip` to `master` after each successful doc build

### Website
- Added "Changelog" card to the home page (`docs/web/index.html`) creating a clean 2×3 six-card grid; removed the redundant featured DAP card

## [v2026.5.1] - 2026-05-10

### New Features
- **HTTP/2 client** (`-lib net_h2`): New `Http2Client` class for persistent multiplexed TLS connections using ALPN h2 via nghttp2. Supports GET, POST, PUT, DELETE, PATCH and `Quick*` static convenience methods (QuickGet, QuickPost, QuickPut, QuickDelete, QuickPatch) that accept a `Url` object for one-shot requests. Linux, macOS, and MSYS2.
- **HTTP/3 client** (`-lib net_quic`): New `Http3Client` class for QUIC connections via ngtcp2 + GnuTLS + nghttp3. Same API as Http2Client — instance methods for streaming use and `Quick*` statics for one-shot use. Linux and macOS only (requires `brew install ngtcp2 nghttp3 gnutls`).
- **Socket options**: New methods on `TCPSocket` and `TCPSecureSocket` — `SetKeepAlive`, `SetNoDelay`, `SetRecvTimeout`, `SetSendTimeout`, `SetRecvBufferSize`, `SetSendBufferSize`, `SetConnectTimeout` (non-blocking connect with timeout). `TCPSecureSocket` adds `SetMinTLSVersion`, `SetVerifyPeer`, `GetCertFingerprint`.

### Libraries
- **HTTP/2** (`net_h2`): `Http2Client` — persistent HTTP/2 session with full verb support and URL-based `Quick*` convenience functions.
- **HTTP/3** (`net_quic`): `Http3Client` — QUIC-based HTTP/3 session with full verb support and URL-based `Quick*` convenience functions.
- **HTTP/1.1** (`net`, `net_secure`): Added `HttpClient->PATCH`, `HttpsClient->PATCH`. Fixed redirect handling for POST and PUT. Added retry-on-reset parity across HTTP/1.1 verbs.

### Infrastructure
- CI: `net_h2` and `net_quic` tests now run in the Linux extended build step; FAIL output causes the CI job to fail.
- CI / CodeQL: added `libngtcp2-dev`, `libngtcp2-crypto-gnutls-dev`, `libnghttp3-dev`, `libgnutls28-dev` to all Linux apt-get install blocks. macOS CI adds the equivalent Homebrew packages.
- macOS Xcode project (`core/vm/xcode/VM.xcodeproj`): added `OBJECK_HAS_NGTCP2` preprocessor flag and `-lngtcp2 -lngtcp2_crypto_gnutls -lnghttp3 -lgnutls` linker flags for Debug and Release.
- `update_version.sh`: `net_h2.obl` and `net_quic.obl` are now compiled as part of the standard library build.
- `core/README.md` and `core/vm/README.md` updated with per-platform build dependency tables covering all network libraries.

### Bug Fixes
- Fixed `HttpsClient` and `HttpClient` redirect not following POST/PUT bodies.

## [v2026.5.0] - 2026-05-06

### New Features
- **Face recognition** (`FaceSession`): SCRFD 10G-KPS face detector + ArcFace R50 512-dim embedding recognizer from InsightFace buffalo_l pack. Cross-platform: DirectML (Windows), CPU/CUDA (Linux), CoreML (macOS). No additional native libraries required.
- **Windows emoji support**: Full Unicode supplementary plane output (emoji and other non-BMP characters) now works correctly in cmd.exe and Windows Terminal. Console string/char output routes through `WriteConsoleW` so surrogate pairs arrive intact; pipe and file redirection continue to emit correct UTF-8 bytes.
- **LSP enhancements**: New `typeHierarchy` (prepare/supertypes/subtypes), `textDocument/selectionRange`, `workspace/symbol`, `textDocument/foldingRange`, `textDocument/documentHighlight`, and `textDocument/typeDefinition` handlers. Fixed hover non-determinism from per-request `ContextAnalyzer` re-analysis; fixed hover on variable declarations and class references.
- **`OBJECK_JIT_DISABLE`**: New boolean environment variable for cleanly disabling the auto-JIT at startup without recompiling. Accepts `true`/`1`; useful for debugging and regression isolation.

### Libraries
- **ONNX**: New `FaceSession`, `FaceDetectionResult`, `FaceRecognitionResult`, `FaceDetection`, `FaceResult` classes. `FaceSession->Compare()` computes cosine similarity between embeddings (>0.35 = same person).

### Bug Fixes
- Fixed Windows console output corrupting characters above U+FFFF — surrogate pairs were split by `std::wcout` causing each half to render as U+FFFD. Fixed via `WinWriteWide` helper using `WriteConsoleW` for live console handles.
- Fixed Windows console code page — `SetConsoleCP`/`SetConsoleOutputCP(CP_UTF8)` now called at startup so pipe/redirect output is interpreted correctly.
- Fixed ARM64 JIT crash on `EXT_LIB_FUNC_CALL` opcodes.
- Fixed macOS ONNX build (Xcode/Homebrew header path issues); improved CodeQL build configuration.

### Documentation
- Expanded `core/lib/onnx/README.md` with platform/EP matrix, quick-start examples, and demo list.
- Updated `core/lib/onnx/MODELS.md` with SCRFD + ArcFace download instructions, Linux CPU/CUDA and macOS CoreML runtime sections.

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
