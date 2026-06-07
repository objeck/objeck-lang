v2026.6.0 (June 7, 2026)
===
New System.AI library, a System.ML overhaul, record types, JIT/compiler fixes, and library improvements.

v2026.6.0
- New System.AI library (-lib ai / @ai): graph search (Dijkstra, AStar, BreadthFirst, DepthFirst), adversarial game search (Minimax with alpha-beta, MonteCarloTreeSearch), metaheuristics (GeneticAlgorithm, SimulatedAnnealing, HillClimbing), and tabular reinforcement learning (QLearning, Sarsa, MarkovDecisionProcess); all stochastic algorithms seedable
- System.ML overhaul: 13 new estimators (RidgeRegression, LassoRegression, ElasticNet, Perceptron, SVM, PCA, GaussianNaiveBayes, AdaBoost, DBSCAN, GaussianMixture, KDTree, RegressionTree, GradientBoostedTrees); real recursive DecisionTree and voting RandomForest; KMeans k-means++ seeding; NeuralNetwork hidden/output bias; seedable System.ML.Random; uniform Fit/Predict/Score/IsFitted/Store/Load API; ml.obs split into seven source files
- BREAKING: RandomForest->Train is now Fit; stored NeuralNetwork model files must be regenerated
- record types: record Point { @x : Int; @y : Int; } generates the constructor and accessors; record : readonly : omits setters and rejects field assignment outside constructors; supports generics, inheritance, and user-defined member overrides
- Fixed VM/JIT frame-dependent traps (Serializer->Write, Date->New, file-time queries) crashing past the auto-JIT threshold on AMD64 and ARM64
- Fixed ARM64 JIT stale self after JIT-to-interpreter callbacks; JIT-to-JIT errors now diagnosable; operand-kind compile guards ported from AMD64
- Fixed float equality on array elements compiled as an integer compare in the JIT
- Fixed bool array literals: every bool static-array literal after the first received the first literal's data; literal dedup now works for all array types; array dimensions capped at 8
- Data.XML: truncated/malformed documents now rejected instead of parsing as success; &apos; decoding fixed; added EncodeText, SetEncodedContent, GetDecodedContent, GetDecodedValue
- Library aliases (-lib @std/@ml/@ai/@game) documented and user-editable via lib/configobjk.ini
- Launchers: Windows defect sweep; macOS version-check modernization
- Performance: auto-JIT compiles MTHD_CALL methods after 10 invocations (5-15% faster); 15 additional inline opcodes; bench_matrix_multiply -14%, bench_dead_code -15%

v2026.5.4 (May 28, 2026)
===
AMD64 JIT trig/float crash fixes, inline optimizer jump-table fix, binary file integrity hardening, LSP consolidated into main repo.

v2026.5.4
- Fixed AMD64 JIT sin/cos/tan and related trig: x87 fsin/fcos/ftan replaced with call_xfunc for consistent cross-platform results
- Fixed AMD64 JIT REG_FLOAT input crash in call_xfunc/sqrt/round (float register state corruption before dispatch)
- Fixed inline optimizer: JMP_TABLE/JMP_TABLE_SLOT label operands not shifted by jump_inline_offset, causing select-heavy inlined methods to jump to ip=0
- Fixed CleanLabelsLocation: end-of-stream overread on consecutive LBL nodes at end of instruction list
- Fixed String->SubString crash on negative or zero length argument (#534)
- Binary file hardening: [uncmp_size:4] prepended before zlib stream; compress2() at level 9; malloc replaces calloc; old format auto-detected via 0x78 CMF byte
- LSP server consolidated into tools/lsp/ in main repo; CI build-lsp rewritten (Ubuntu, full toolchain, vsce package); publish-vscode job added for marketplace publishing

v2026.5.3 (May 18, 2026)
===
Three-tier select dispatch with native jump table (AMD64+ARM64), String->Split fix, spectralnorm fix and optimization.

v2026.5.3
- Three-tier select dispatch (AMD64 + ARM64 JIT): direct compare for 1 case, linear scan for 2-5 integer cases, O(1) native jump table for 6+ dense integer cases, binary search tree for sparse/string cases
- Fixed String->Split(Char): last token returned oversized result due to using array capacity instead of logical string length
- Fixed bench_spectralnorm_native: native allocation bug caused garbage output (~3.84e-156); also rewrote inner loops with incremental float denominator to eliminate I2F conversions

v2026.5.2 (May 17, 2026)
===
HTTP/2+3/QUIC clients, Gemini/OpenAI API expansion, ARM64 Windows support, WebSocket hardening.

v2026.5.2
- HTTP/2 client (Http2Client): persistent TLS connections, GET/POST/PUT/DELETE/PATCH, Quick* one-liners via nghttp2 + ALPN
- HTTP/3 / QUIC client (Http3Client): UDP connections with connection reuse and Quick* API via ngtcp2 + nghttp3 + GnuTLS
- HTTP/1.1 improvements: PATCH method, redirect handling fixes for POST/PUT, retry parity across HttpClient/HttpsClient
- OpenAI Moderation: Moderation->Check() returns per-category flags and confidence scores
- OpenAI Batch: Batch->Create()/Get() for async 50%-cost batch requests (up to 50k at a time)
- Gemini Files API: upload, list, get, delete files via FileManager
- Gemini Context Caching: CachedContent->Create() for server-side prompt caching with configurable TTL
- Gemini Search Grounding: Model->GenerateContentWithGrounding() anchors responses in live Google Search results
- Gemini Batch Embeddings: Model->BatchEmbedContent() embeds multiple texts in one round-trip
- WebSocket hardening: 8 bug fixes including bulk ReadBuffer I/O replacing per-byte reads
- MCP server fixes: hang on shutdown and crash-on-stop resolved
- Socket reliability: SO_REUSEADDR on TCPSocketServer::Bind() survives TIME_WAIT; IPSocket::Open() falls through to next address on failure
- EmbeddingValues wrapper to avoid Float[] as generic type parameter
- ARM64 Windows: OpenCV and ONNX fully supported; build configurations corrected
- Improved release process: self-contained Windows builds; CI verifies all binaries and API docs before publishing

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