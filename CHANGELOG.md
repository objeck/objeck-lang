# Changelog

All notable changes to Objeck will be documented in this file.

## [v2026.2.1] - 2026-02-14

### New Features
- **Try/Otherwise Error Handling**: `Try()` and `Otherwise()` compiler intrinsics on the `Base` class for safe method chaining with graceful error recovery (e.g., `obj->Try()->Method()->Otherwise(default)`)

### Improvements
- **Editor Syntax Highlighting**: Updated Monaco (playground) and VSCode syntax definitions to support `Try` and `Otherwise` as built-in keywords
- **Web Playground**: Updated version tag and editor keyword support for v2026

### Internal
- Refactored Try/Otherwise from expression-level keywords to method intrinsics on `Base` class for cleaner semantics
- Removed disabled legacy CI workflow (superseded by `ci-build.yml`)

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
