# Changelog

All notable changes to Objeck will be documented in this file.

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
