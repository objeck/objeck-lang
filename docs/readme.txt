v2026.8.1 (August 16, 2026)
===
Ships obu, which v2026.8.0 advertised but omitted from every archive, and documents the HeaderCheck validators.

v2026.8.1
- Packaging: obu was missing from every v2026.8.0 archive. The release advertised the updater as a headline feature and delivered no binary -- release-build.yml never referenced obu, and none of the three deploy scripts built or copied it, so bin/ held obb, obc, obd, obi and obr on Windows, Linux x64/ARM64 and macOS alike. The source, its POSIX makefiles and its .vcxproj had existed the whole time; only the packaging was absent. CI missed it because ci-build.yml exercises obu only through test_update.sh, which builds its own copy with test hooks -- so the updater was tested continuously while never being shipped. obu is now built and installed to bin/ by all three deploy scripts, and both "Verify required binaries" gates list it, so a future omission fails the build before a tag is pushed
- Documentation: Web.HTTP.HeaderCheck shipped in v2026.8.0 with only IsValidRequestTarget documented. IsValidName and IsValidValue -- the two functions guarding every AddHeader call -- had no doc comment, and the API index records only documented members, so LSP hover and completion showed nothing for the security fix's primary entry points. Flatten's doc block also sat above IsValidRequestTarget rather than above Flatten, documenting the wrong function. All four are now documented and correctly bound

v2026.8.0 (August 15, 2026)
===
Full-screen editor in obi, HTTP/3 on Windows, the obu updater, five HTTP request-injection fixes, nil-safe operators, and structured variable inspection in the debugger.

v2026.8.0
- Editor: obi gains a full-screen editor ('/e') -- raw-mode terminal editing over the REPL buffer with syntax coloring driven by the compiler's own scanner, damage-diffed rendering, CJK/tab-correct widths, undo/redo with coalesced typing, Shift-selection, an internal clipboard, and an opt-in vi profile (F2). F5 compiles and runs the buffer as a subprocess, streaming its output into a pane that Esc can cancel, and F8 walks the cursor through compile errors. Header-only, with no build-system changes on any platform
- Network: HTTP/3 works on Windows 11 / Server 2022 and later, backed by WinHTTP over MsQuic. It had shipped documented-and-dead -- the trap handlers compile unconditionally, so the API was present and every request simply failed. WinHTTP treats HTTP/3 as a preference, so every request asserts the protocol actually used and fails rather than silently downgrading. No vendored QUIC library, no second TLS backend, no new DLL to deploy
- Security: five HTTP request-injection fixes. The most severe needs only a URL -- Url->New does not sanitize, and the path and host were appended verbatim into the HTTP/1.1 request line, so a CR/LF split the request line itself. Caller-supplied header values and content types were injectable the same way across HTTP/1.1, HTTPS, HTTP/2 and HTTP/3; a new Web.HTTP.HeaderCheck validates names, values and request targets per RFC 9110/9113 at every serialization site. Separately, HTTP/3 connection IDs could be uninitialised stack memory when the CSPRNG failed, since the gnutls_rnd return value was ignored -- a memory disclosure in the cleartext QUIC Initial header
- Tooling: 'obu check' compares the installed version against the latest GitHub release; 'obu update' and 'obu rollback' (Linux/macOS) download the platform archive and its SHA256SUMS, verify the digest before touching disk, then do an all-or-nothing staged swap with a post-install check and automatic rollback. No shell is used for the download, extract or swap. Every release now ships a SHA256SUMS asset
- Network: AddHeader now actually sends headers on HTTP/2 and HTTP/3. Both clients stored headers on the Objeck side and then dropped them, because the trap signature had no headers argument and nothing ever wrote the map the native backends were already reading. Verified by a server echoing the value back, not by a status code. HTTP/1.1 was never affected
- JIT: the ARM64 backend clobbered a general register on every float compare against memory -- move_freg_freg addressed the general register file rather than the FP file, because the Register enum restarts float numbering at D0 = 0. When the clobbered register held a live pointer, the next use followed a float bit pattern as an address: the long-open "memory corruption of free block" crash, whose faulting PC 0x3F947AE147AE147B is the IEEE-754 bits of 0.02. Correcting the encoding then exposed a stray copy that zeroed the divisor of every JIT float division. AMD64 was correct throughout
- Compiler: 'v->Pow(10)' computed Pow(10, v) -- 100 instead of 1024 -- whenever the receiver was a variable, instance variable or array element; literals were correct, so the two spellings of one call disagreed. All 29 multi-argument primitive functions were affected. A calculated receiver in an expression, '(1 + 1)->Pow(10)', corrupted the heap outright because the emitter dropped the call arguments
- Compiler: divide by zero crashed at the default optimization level -- the constant folder kept a zero-divisor division for its runtime trap but emitted it before its operands, turning a clean trap into a silent crash. Modulus by zero never trapped at all, and now traps exactly as division does
- Runtime: 'runtime.feature.http2' and 'runtime.feature.http3' report which protocol engines were actually compiled in, so a caller can tell "not supported" from "the network failed". Relatedly, obd had shipped with HTTP/2 and HTTP/3 compiled out, and HTTP/3 was silently unavailable on Linux ARM64
- ONNX: hardened native phi3 inference -- fixed an out-of-bounds read when a model exported mismatched KV key/value tensor counts, guarded the logits rank/offset access, surfaced an unsupported logits dtype instead of silently sampling token 0, and made the token sampler safe on empty/degenerate distributions
- Debugger: the DAP formatter read a String's backing-array capacity instead of its length, returning NUL-padded values on POSIX platforms; both front ends now read the length from the String itself via a shared layout header
- Debugger: structured variable inspection -- objects expand into their fields, arrays into indexed elements, and Vector/Map/Hash into their contents (Map in key order, Hash as key -> value), each child expanding recursively. Previously every variable was a dead end and a Map displayed only as Collection.Map@0x1f4a2c0. Strings and boxed scalars render as their value rather than expanding into backing storage. Expansion handles are discarded on every resume, since the collector moves objects
- Language: unsigned shift '>>>' and unsigned Int operations -- '>>' copies the sign bit into the high end, since every integer sits in a signed 64-bit slot; 'a >>> n' shifts a zero in instead, at the same precedence as '>>' and left-associative. Int also gains ShiftRightUnsigned, CompareUnsigned, DivideUnsigned, ModUnsigned and ToUnsignedString, which read both operands as unsigned 64-bit quantities, so Int->ToUnsignedString(-1) is 18446744073709551615. '>>>' desugars onto a library call, so no opcode is added and the VM and both JIT backends are unchanged
- Language: unsigned integer literals -- a 'u' suffix reads a literal across the whole unsigned range and keeps its bit pattern, so 0xFFFFFFFFFFFFFFFFu and 18446744073709551615u are writable; the value stays an ordinary Int. Fixes a silent bug where literals past the signed range saturated without a diagnostic, so 0xFFFFFFFFFFFFFFFF gave 9223372036854775807 instead of every bit set. It now evaluates to -1, as in C, Java, C# and Rust
- Debugger: data breakpoints -- break when a value changes rather than when a line is reached, which is how you find the one write among many that corrupts a field; the stop names the watch and reports the old -> new transition
- Debugger: watch and hover expressions now expand the same way, and the adapter supports 17 DAP capabilities (up from 8) -- terminate, breakpointLocations (so editors stop offering breakpoints that cannot bind), exceptionInfo, setExpression, completions, modules, loadedSources, data breakpoints, and variable paging
- Language server: reports serverInfo (name and version), declares its code-action kinds so clients can filter them and route "Organize Imports", and declares signature-help retrigger characters
- JSON: JsonElement->Encode produced invalid escapes for every non-ASCII character (U+2019 encoded as \u0x2019), because Int->ToHexString returns a 0x-prefixed, unpadded value; escapes are now four zero-padded hex digits, with surrogate pairs above the BMP
- Runtime: System.IO.ConsoleIO->Instance() had an inverted null check, so it returned Nil on the first call and a fresh instance on every call thereafter
- Build: Windows projects select $(DefaultPlatformToolset) instead of pinning v145, so the tree builds on VS2022 and VS2026 alike
- Language: nil-safe operators -- a?->b() calls b only when a is non-Nil and yields Nil instead of faulting; a ?? b supplies b when a is Nil, evaluating b only when needed. A single ?-> guards the whole rest of a chain, and the two combine: maybe?->ToUpper()->Size() ?? -1. Both desugar onto the existing Try()/Otherwise() intrinsics, so no new bytecode is emitted and the JIT backends and VM are unchanged. Spelled ?-> rather than ?. because Objeck's member accessor is ->
- Language server: fixed five requests that crashed the server process -- go-to-implementation, semantic tokens, inlay hints, and both call-hierarchy directions -- each returned a Result[] through the local argument array and failed its cast
- Language server: fixed a line-range bug that made any cursor past the first method of a class resolve to the first method, affecting definition, references, hover, rename and call hierarchy
- Compiler: anything chained after Otherwise() was silently dropped, so x->Otherwise("abc")->ToUpper() returned "abc"; Try()/Otherwise() now also resolve on an indexed receiver, so arr[i]?->m() compiles
- Formatter: '??' scanned as two tokens, so formatting a file that used it emitted '? ?' and produced source that would not compile
- Tooling: the formatter, language server and VS Code extension are now covered by CI on every push

v2026.6.4 (June 28, 2026)
===
Stability fix for an intermittent multithreaded crash in the generational minor garbage collector during thread startup.

v2026.6.4
- VM: fixed an intermittent crash (0xC0000005) in the generational minor garbage collector during thread startup -- a thread being spawned held its self and argument as untracked raw pointers, so a moving collection during the spawn handoff could relocate the still-live object (kept reachable via another root) without updating the holder, leaving the new thread a stale reference; these are now tracked and relocated across collection (pending_thread_roots, marked and fixed up on both Win32 and POSIX thread paths). Surfaced only under heavy multithreaded churn

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