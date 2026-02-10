# Changelog

All notable changes to Objeck will be documented in this file.

## [v2026.2.1] - 2026-02-09 ✅

### Performance Optimizations
- **Memory Manager**: Switched to `std::unordered_set` for O(1) lookups (previously O(log n))
- **Memory Manager**: Eliminated unnecessary thread creation in `CollectAllMemory()`
- **Memory Manager**: In-place sweep instead of rebuilding allocation set (reduced GC pause time)
- **ARM64 JIT**: Fixed multiply-by-6 optimization (LSL #2 instead of LSL #1)
- **ARM64 JIT**: Optimized register targeting for immediates > 4095
- **ARM64 JIT**: Fixed FP register pool management (D8-D15 no longer overlap with temp slots)
- **x64 JIT**: Instruction encoding optimizations with dynamic backpatching
- **x64 JIT**: Fixed FLAGS-clobbering in XOR-for-zero optimization

### New Platform Support
- **Windows ARM64**: Full support with automated mbedTLS 3.6.3 build
- **Windows ARM64**: One-command deployment with `deploy_windows.cmd arm64`
- **Crypto Library**: Migrated from OpenSSL to mbedTLS for lighter footprint and ARM64 support

### Testing Infrastructure
- **Regression Suite**: 10 focused tests validating critical functionality
  - 4 ARM64 JIT-specific tests (char arrays, large immediates, bitwise ops, multiply constants)
  - 6 core language tests (arithmetic, arrays, classes, control flow, recursion, strings)
- **CI/CD Integration**: Automated regression testing on every commit
- **Documentation**: Comprehensive [TESTING.md](programs/TESTING.md) covering all 350+ tests
- **Cross-platform**: All tests run on Windows/Linux/macOS, x64/ARM64

### Compiler Improvements
- **Error Messages**: Show operator symbols in binary operation errors (`Invalid operation '+' between types`)
- **Error Messages**: Normalized type names (Bool, Int, Float instead of System.Bool, System.Int)
- **Error Messages**: Inline hints for undefined variables and enums
- **Error Messages**: Better generic type parameter mismatch reporting

### Bug Fixes
- **ARM64 JIT**: Fixed STRH/LDRH opcodes for char array operations (11 total fixes)
- **ARM64 JIT**: Fixed 32/16/8-bit memory offset scaling
- **ARM64 JIT**: Fixed `cmp_imm_reg` off-by-one allowing invalid immediate 4096
- **ARM64 JIT**: Fixed bitwise NOT using 32-bit ORN instead of 64-bit
- **Memory Manager**: Fixed duplicate frame push in `CheckPdaRoots` causing incorrect root scanning
- **Memory Manager**: Added missing `allocated_lock` in `CheckObject` race condition
- **Memory Manager**: Removed redundant O(n) binary search on set iterators
- **Crypto**: Fixed AES-256 CBC with explicit PKCS7 padding mode
- **Crypto**: Fixed AES-256 key derivation to match original OpenSSL behavior

## [v2026.2.0] - 2026-01-20 ✅

### Added
- Modern GNU-style CLI flags (`--source`/`-s`, `--destination`/`-d`, `--debug`/`-D`)
- Enhanced library path handling
- Development workflow improvements with Claude Code

### Changed
- Maintained full backward compatibility with legacy command syntax
- Improved error messages and diagnostics

## [v2025.9.1] - 2025-09-15 🚧

### In Progress
- ONNX Runtime support for cross-platform ML inference (targeting v2026.3.x)
- ORT is a cross-platform inference engine designed to accelerate ML across hardware and software platforms
- Initial support for macOS and Linux

## [v2025.9.0] - 2025-09-01 ✅

### Added
- **OpenCV Integration**: Real-time computer vision support
  - Face detection
  - Image processing
  - Video capture and processing
- **OpenAI Realtime API**: Support for `gpt-4o-realtime-preview-2025-06-03`
  - Text and audio responses
  - Real-time streaming
- **PCM16 Audio**: Recording and playback APIs via SDL2 mixer
- **Cursor AI IDE**: Initial support for Cursor AI development environment
- **GPT-5 Models**: Support for OpenAI's reasoning models
  - Reasoning capabilities
  - Verbosity controls
- **Audio Conversion**: PCM16 to MP3 Lame audio translation support

### In Testing
- OpenAI MCP (Model Context Protocol) support

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
