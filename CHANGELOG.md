# Changelog

All notable changes to Objeck will be documented in this file.

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
