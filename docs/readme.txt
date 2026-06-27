v2026.6.3 (June 27, 2026)
===
Generational minor garbage collection, closure ergonomics (direct FuncRef calls, bare lambdas, lambda block bodies), and a new System.Concurrency library.

v2026.6.3
- VM: generational minor (nursery) garbage collection is now enabled -- a nursery-full collection scans only the remembered set plus roots and recycles the young generation without sweeping old gen, with a major-GC fallback under old-gen pressure; JIT and interpreter reference stores emit the write barrier on AMD64 and ARM64, and the nursery is zeroed at allocation time instead of inside the stop-the-world pause
- Language: closure ergonomics -- call a FuncRef directly with v() (no ->Call()), bare lambdas with an inferred return type (\(x) => x * 2) that auto-wrap into FuncRef<R> in assignments, returns, method arguments, and collection elements, and lambda block bodies
- Library: new System.Concurrency (concurrent.obl) -- structured concurrency with TaskScope, Task, and Monitor, plus runtime.* process/GC/CPU diagnostics (GC pause, promotion, allocation rate, lock contention, thread/STW/nursery counters) read through Runtime->GetProperty
- Fixed multi-capture closure heap corruption (captures now use closure-local ids), plus re-analyzed/repeated FuncRef direct calls and a spurious unreferenced-variable warning for closure-captured variables
- Performance: SDL2 Renderer 2D draws pool the boxing buffer and cache proxy/method names
- Removed the unused Gtk3 binding

v2026.6.2 (June 19, 2026)
===
Major JIT and garbage-collection performance work (a near-free GC safepoint poll, auto-JIT for closure/function-reference calls, inline nursery allocation), a sweep of JIT float-codegen correctness fixes, and locale-independent UTF-8 I/O.

v2026.6.2
- Performance: the cooperative stop-the-world GC safepoint poll is now nearly free in JIT'd code -- an inline flag test that calls the collector only when a collection is active, reading &stw_active from a register cached at the prologue (R12 on AMD64, X19 on ARM64), emitted only at loop back-edges. fannkuchredux roughly halved (~59s -> ~31s), recovering the full v2026.6.1 regression on both AMD64 and ARM64
- Performance: auto-JIT for DYN_MTHD_CALL (closure / function-reference calls) on AMD64 and ARM64 -- spectralnorm reaches native-level speed once warm (43s interpreted -> 0.46s at n=2000, matching the hand-native kernel)
- Performance: inline young-generation bump allocation for NEW_OBJ_INST (AMD64); interpreter float fast-path; ARM64 JIT whitelist parity with AMD64
- Fixed JIT float-codegen and tail-call bugs surfaced by forcing JIT (OBJECK_JIT_THRESHOLD=1): AMD64 Floor/Ceil/ArcTan and two latent DYN_MTHD_CALL miscompiles; ARM64 transcendental/round cached-local operands, dropped libc float result/argument, working-stack registers across inlined float calls, imm19 backpatch SIGILL (ml_gbt); TCO deferred-load corruption (return Gcd(b, a%b)) on both architectures; an ARM64 negative-offset load crash invoking a JIT-compiled closure captured in a collection (Vector<FuncRef>) -- the memory encoders couldn't represent a negative displacement and read the wrong stack slot, now routed through a signed-offset LDUR/STUR helper (x64 unaffected). Full ARM64 suite green at OBJECK_JIT_THRESHOLD=1
- Fixed UTF-8 breaking under a C/non-UTF-8 process locale: obc reading UTF-8 source and obr loading/printing UTF-8 strings; sys.h now uses systemic locale-independent UTF-8 codecs instead of mbstowcs/wcstombs
- Fixed a VM shutdown thread race by quiescing worker threads before program teardown
- Int->MinSize() now returns INT64_MIN instead of INT64_MAX
- CI: native (non-Docker) cross-language perf gate measures Objeck against Python/Ruby/LuaJIT/Java with committed baseline ratios; refreshed performance docs and speedup roadmap

v2026.6.1 (June 14, 2026)
===
String interpolation with expressions and format specifiers, generic bounds and variance, cooperative multithreaded GC, a major command-line and VS Code debugger expansion, and TLS certificate verification.

v2026.6.1
- String interpolation: "{$...}" now accepts arbitrary expressions ("{$i + 1}", "{$a * b - c}", "{$x > y}"), Python/.NET-style format specifiers for precision/width/alignment/radix ("{$pi:.2}", "{$n:05}", "{$s:<10}", "{$v:x}", "{$v:b}"), and a positional String->Format("{0} = {1}", a, b) helper
- Generics: compound bounds (T : A & B), F-bounded constraints (T : Compare<T>), and declaration-site variance (out T covariant, in T contravariant), checked soundly and preserved across the .obl boundary; readable generic type-mismatch diagnostics
- Multithreaded stop-the-world GC: mutator threads park at safepoints (interpreter dispatch, JIT back-edges on AMD64/ARM64, allocation, and blocking join/sleep/socket I/O) so the collector always marks a complete root set; fixes freed-live-object corruption and use-after-free under thread churn
- Debugger (obd): frame navigation (frame/up/down) and locals, set <var> = <expr>, breakpoints by method (b Class->Method), temporary breakpoints (tbreak), enable/disable/ignore counts, watchpoints (watch/watches/unwatch), until <line>, repeat-on-Enter, and non-executable-line relocation; conditional breakpoints (b file:line if <expr>) now parse correctly
- Debugger (DAP / VS Code): setVariable, function breakpoints, logpoints, in-process restart, and exception breakpoints (break on an uncaught runtime error)
- New Web.Server library (-lib web_server) for simple HTTP servers
- Reproducible library builds: compiling unchanged .obs source now produces byte-identical .obl output
- Security: secure client and DTLS sockets verify server certificates by default (set OBJECK_TLS_INSECURE_SKIP_VERIFY=1 to opt out); hardened VM deserializers against hostile 64-bit sizes and a Char[] read-trap heap overflow
- Serialization correctness: int arrays dropped half their elements and truncated 64-bit values to 32 bits; Char[] desync; Float field slot; object function-reference fields. Note: the serialized integer wire format widened to 8 bytes
- Compiler: LICM no longer hoists trapping DIV_INT/MOD_INT out of zero-trip loops; ConstantProp no longer emits a stale literal after a non-constant reassignment
- ONNX: macOS persists compiled CoreML models across runs (35x faster warm start); keep Ort::TypeInfo alive while reading tensor type/shape
- Launcher: fixed portable bundles failing when launched outside their directory
- Build: fixed D9025/LNK4099/LNK4098 warnings and a NativeCode ODR violation; CI pinned to windows-2022 (VS2022 toolset v143)

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