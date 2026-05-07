v2026.5.0 (May 7, 2026)
===
Face recognition (SCRFD+ArcFace), Windows emoji fix, LSP enhancements.

v2026.5.0
- Face recognition (FaceSession): SCRFD 10G-KPS detector + ArcFace R50 512-dim embeddings from InsightFace buffalo_l. Cross-platform: DirectML (Windows), CPU/CUDA (Linux), CoreML (macOS)
- Windows emoji: full Unicode supplementary plane output now works in cmd.exe and Windows Terminal via WriteConsoleW; pipe/file redirection emits correct UTF-8
- LSP enhancements: typeHierarchy (supertypes/subtypes), selectionRange, workspace/symbol, foldingRange, documentHighlight, go-to-type-definition; hover correctness and non-determinism fixes
- ARM64 JIT: fixed EXT_LIB_FUNC_CALL crash; macOS ONNX build fixes; CodeQL build improvements
- OBJECK_JIT_DISABLE: new boolean env var for cleanly disabling auto-JIT at startup

v2026.4.3 (April 12, 2026)
===
DAP debugger hover + scoping, editor setup (VSCode/Sublime/gvim), LSP crash fixes.

v2026.4.3
- DAP debugger hover: hovering an object shows ClassName { field=val, ... } with instance field expansion
- DAP instance/class variable scopes: Variables pane shows Locals, Instance, and Class scopes
- DAP stepping + crash fixes: step-into crash, step-over/out scoping, stdout corruption, disconnect AV
- Editor setup refresh: VS Code, Sublime Text, and gvim DAP+LSP for Windows, Linux, and macOS
- LSP crash fixes: null guards for textDocument/codeAction with inferred locals, hover position fix
- Configurable JIT threshold for auto-JIT invocation count
- Fixed JIT S2F callback param count causing segfault on String:ToFloat
- Hardened HTTPS client against null ReadLine on connection failures

v2026.4.2 (April 6, 2026)
===
JIT register cache (~3x perf), AI library refresh, S2F JIT fix, editor support, and more.

v2026.4.2
- JIT local variable register cache (AMD64 and ARM64): ~3x speedup across all benchmarks
- Fixed JIT S2F callback param count causing segfault on String:ToFloat (AMD64 and ARM64)
- Gemini API: added 2.5 Pro/Flash model constants, system instruction support
- Ollama API: configurable host, Options class (temperature/top_p/top_k), Tool class for function calling
- OpenAI API: new Embedding class, Models constants (GPT-4.1, O3, O4-mini, etc.)
- ML library: fixed EuclideanDistance/StdDev bugs, added LinearRegression and LogisticRegression
- New editor setup guide (docs/editors.md) with Vim, Emacs, Sublime, and DAP debugging docs
- Hardened JSON, JSON stream, and XML parsers against malformed input
- Hardened HTTPS client against null ReadLine on connection failures
- DTLS (Datagram TLS) support: DTLSSocket and DTLSSocketServer for secure UDP
- Link-time optimization (-flto=auto) across all GCC Makefiles (AMD64 and ARM64)
- ARM64 native CPU tuning (-mcpu=native) for RPi5 and Jetson Orin
- Fixed all MSVC and GCC compiler warnings
- Fixed doc generator error on @hidden tag

v2026.4.1
- Debug Adapter Protocol (DAP) for VS Code debugging
- 3.3x binarytrees speedup with young-gen bump allocator and JIT-to-JIT calling
- MTHD_CALL JIT whitelist for x64 and ARM64
- Networking: SSE streaming, socket receive timeouts, HTTP hardening
- ODBC: BigInt support, connection strings, transactions, schema discovery
- OpenCV: contours, VideoWriter, transforms, 15 new image processing functions
- Phi-3 Vision multimodal inference with FP16 and DirectML/CUDA support

v2026.2.1 (February 26, 2026)
===
New try/otherwise error handling, JIT stability fixes, debugger improvements, SDL2 bug fixes, and expanded test coverage.

v2026.2.1
- New 'try/otherwise' error handling framework
- Fixed VM crash in Try/Otherwise when Nil dereference occurs inside non-virtual method calls
- Fixed Windows debugger build: HELP_COMMAND enum collision with WinUser.h macro
- Added debugger help command with full command reference
- Fixed AMD64 JIT segfaults (MTHD_CALL, DYN_MTHD_CALL, class instance vars)
- Fixed ARM64 JIT pre-scan rejection for rewritten opcodes
- Fixed broken Log/Log10 float math functions
- Fixed 15 bugs in SDL2 native interface and Objeck bindings
- Fixed compression crashes (zero-init z_stream in gzip/brotli)
- Added 14 debugger regression tests with expect-based CI automation
- Added 16 runtime regression tests covering JIT and core language
- Web playground updated to v2026.2.1 Preview
- Performance: 4.38x nbody speedup via inline limit increase
- Compiler: CSE, dead code elimination, constant folding fixes
- JIT: Division-by-zero guards in constant folding for x64 and ARM64
- 10 new performance benchmarks with measurement tooling
- CI: Linux ARM64 and macOS ARM64 test execution in GitHub Actions

v2026.2.0 (February 12, 2026)
===
NLP library, web playground, Gemini 2.0/2.5 support, Windows ARM64 platform, and major performance optimizations.

v2026.2.0
- Added NLP library with tokenization, TF-IDF, similarity, and sentiment analysis
- Added web playground at playground.objeck.org
- Added Gemini 2.0/2.5 support with audio capabilities
- Added OpenCV integration for real-time computer vision
- Added OpenAI Realtime API support
- Added ONNX Runtime for cross-platform ML inference
- Added PCM16 audio recording/playback and MP3 conversion
- Added Windows ARM64 platform with code signing
- Migrated crypto library from OpenSSL to mbedTLS
- Modern GNU-style CLI flags with backward compatibility
- Memory manager and JIT performance optimizations
- Fixed method chaining on array-indexed elements after cast (#524)
- Fixed String buffer size in ToString() for Int and Float
- Collections and String bug fixes and new methods
- Fully automated CI/CD release pipeline
- Bug fixes

v2025.7.0
- Added Hash->Dict(..), Map->Dict(..) and Vector->Zip(..) to collections
- Updated style (docs, logos, etc.)
- Bug fixes

v2025.6.3
- Support for user-provided HTTPS PEM files
- Added multi-statement pre/update support 'for' loops

v2025.6.2
- New API documentation system
- Added support for OpenAI's Responses API
- Updated Windows launcher
- Improved JSON scheme support