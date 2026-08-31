# Changelog

All notable changes to Objeck will be documented in this file.

## [v2026.8.4] - 2026-08-30

The release is dominated by one thing: `Game.OpenGL`, a 3D graphics framework built from nothing to a shipping library over roughly forty commits. Beside it sit a library that had never been usable, a class of JIT bugs that produced wrong arithmetic with no diagnostic, three ways the collector could destroy a live object, and a Windows ARM64 distribution that shipped without the runtimes it needs.

### New Features
- **`Game.OpenGL`**: OpenGL 3.3 core over SDL2, on Windows, Linux and macOS &mdash; 26 classes. `GLWindow` (windowing, frame pacing, key/mouse/wheel input, held-key state, gamepad), built-in shaders with uniforms that no longer leak between programs, `Mesh` primitives and OBJ loading, textures with anisotropic filtering, `Transform`/`Vector3`/cameras (first-person, `LookAtPoint`, orthographic), materials, up to eight directional/point/spot lights with Blinn-Phong specular, shadow maps including omnidirectional cube shadows from point lights, render-to-texture, instanced drawing plus the object-layer `PropBatch`, frustum culling on by default, `Scene->Raycast` for hitscan and picking, transparency with correct ordering, multisampling, stencil/points/line-width/depth-func, particles, billboards, a transform hierarchy with keyframed animation, sound, and a pixel-space text overlay with a default font. The draw loop stops allocating: per-frame allocations in both original loops went to zero, and the minimal window demo went from 105 lines to 25. Verified by 453 checks that read pixels back rather than merely exiting cleanly
- **Two 3D demos ship in the distribution**: `3d_gl_24.obs` and `fps_gl.obs`, a first-person shooting gallery with hitscan shooting, sliding collision and controller support. `run_gl` makes setup one command on Linux and macOS
- **`Web.Server` reimplemented in pure Objeck**: the bundle shipped in every release and could not be used by anyone. Its 13 native entry points appeared in exactly one file &mdash; the binding itself &mdash; with no `.cpp`, no build target and no `libobjk_web_server` in any deploy tree; nothing set `OBJECK_LIB_WEB_SERVER`, and `Request` and `Response` declared no constructor, so an instance could not be obtained at all. The missing native library was never a possible fix: `DllProxy` takes the library name from a runtime property and the design is a per-host bridge for Nginx, IIS and Apache, whose request structures differ entirely, so a single generic library cannot exist. It is now an adapter over `Web.HTTP.Server`, which already provided a complete, tested `Request`/`Response` &mdash; same bundle, same class names, same method signatures, no native library, no new build target, no new CI leg. Coverage 0 of 13 methods to 13 of 13
- **HTTP keep-alive**: removes the Windows loopback exposure rather than hiding it, by changing which side closes rather than any socket call. No connection reuse is required for the fix
- **`obu update` and `obu rollback` on Windows**: the in-place swap needed no copy-self-and-re-exec dance &mdash; Windows forbids deleting a running image but permits renaming it. `obu check` also gained offline test coverage on every platform; it had none anywhere, because it built its own request URL instead of going through the function that honors the offline switch
- **`TTF_CloseFont` is bound**, so a font can be released at all
- **macOS ships as a notarizable `.zip`**, and the playground deploys from the release pipeline

### Bug Fixes
- **JIT truncated 64-bit immediates to 32 bits**: on AMD64 for `and`, `or`, `xor`, `add` and `sub`; on Windows ARM64 for every one of them, at the call sites rather than the emitters, because `long` is 32 bits under LLP64. Neither crashed nor diagnosed &mdash; arithmetic above 2^31 was simply wrong. Windows ARM64 had shipped untested since 2026-02-10, which is why its variant survived
- **JIT stored float compare clobbered a callee-saved register** on AMD64
- **JIT `Int[]` copy dropped the write barrier**
- **A trap-returned array held elements a minor GC could destroy**: arrays are old-generation from creation and objects bump-allocate in the nursery, so a minor collection promoted and moved the elements while repairing references only in objects the write barrier had dirtied &mdash; which a freshly filled trap array never was. Measured at 395 of 395 entries destroyed on a single collection, with the reading thread allocating nothing itself
- **A value returned by a native library could be collected** out of a reused argument buffer
- **One failed name lookup shut down Winsock for the whole process**: `WSAStartup` runs once, so the `WSACleanup` on `IPSocket::Open`'s `getaddrinfo` failure path dropped the refcount to zero and closed every open socket on every thread. Dormant only because `getaddrinfo` on a literal address does not fail
- **A short `send()` dropped the remainder of the buffer** on both platforms
- **A real HTTP 500 lost its body, and a dead socket reported one**; discarded queued response data was not closed, closed sockets read as open, and persistence framing disagreed between HTTP and HTTPS
- **A program that started a server and returned normally from `Main` segfaulted during teardown** (Windows): a thread blocked in `accept()` never observes `Halt`, so the drain timed out and `~Loader` freed the program image while that thread was still live; `WSACleanup` on the way out of `win_main` then unblocked every parked socket call process-wide and sent it executing against freed memory. All buffered output was lost with it, so a program that ran to completion looked as though it had done nothing. Linux was always clean for exactly one reason &mdash; it has no equivalent call. The image is no longer freed while a thread may still run against it, and `WaitForThreadsToDrain` now measures real elapsed time instead of counting `sleep_for(1ms)` iterations as 1ms each, which on Windows' ~15.6ms timer granularity turned a nominal 2000ms budget into 20&ndash;30 seconds of hang on every exit with a live thread
- **The debugger had the same defect, and there it was not Windows-only**: `obd` hosts the debuggee's VM in-process, and `ClearProgram` ran after every single run &mdash; freeing the bytecode image, the `StackProgram`, and the whole GC heap, since `MemoryManager::Clear` releases the young region, every old-generation object and the locks the VM takes on the way in. It called neither `HaltAllExcept` nor `WaitForThreadsToDrain`; the strings did not appear anywhere under `core/debugger`. Because `obd` outlives the run and returns to its prompt, an ordinary client connecting to the port the parked thread sits on wakes it with no `WSACleanup` involved &mdash; 6 access violations in 6 runs on Windows and 2 in 2 on Linux, against 8 in 8 for the DAP session-exit shape, which was Windows-only. The drain would also have lied had it been called: it reports "drained" when at most one interpreter is registered, and `obd`, unlike `obr`, never registered its own
- **`obd` could not debug any multithreaded program**: compile one with `-debug` &mdash; which is how a program you intend to debug is compiled &mdash; and it segfaulted on the first instruction the spawned thread executed, before that thread produced a line of output. `StackInterpreter::debugger` was set by exactly one constructor, and spawned VM threads use the default one, which left the member indeterminate while `Execute`'s hot loop called through it on every instruction. It read as a `-debug` problem rather than a threading one because the hook touches nothing until a line number exists, and without symbols every line number is -1. `_DEBUGGER` is defined only for `obd`, so `obr` was never affected and the VM regression suite could not have caught it. Breakpoints and stepping still do not reach inside spawned threads, but they run
- **A failed library load on Windows said nothing about why**
- **Windows ARM64 shipped without its runtimes**: no C++ redistributable, because `VCToolsRedistDir` is empty on the ARM64 runner, and OpenCV without its image codecs. Nothing checked either. Also fixed: the 7-Zip probe stored a quoted path and then re-quoted it, and the vcpkg root probe missed `~/vcpkg` while vcpkg's zlib shadowed the vendored one
- **mbedTLS and nghttp2 came from stale committed blobs**, not vcpkg
- **SDL2 failed to load on Windows without a hand-set `PATH`**: the DLLs shipped in `lib/sdl`, but Windows resolves a dynamically-loaded library's imports against the executable's directory, never the directory holding the library. Every SDL program was affected, and the regression runner hid it by prepending the directory before running the tests. macOS now bundles SDL2 in the distribution instead of asking users to install it
- **ONNX aborted the process when destroying its `Env` at exit** on macOS
- **macOS notarization had been failing on every release, silently**
- **The doc parser was reading prose as code**: the word "bundle" in a comment or string literal re-filed every class after it under a bundle named for the next word. Five files hard-code the library list and had drifted apart &mdash; one short two libraries while still naming a deleted third, so the counts matched and nothing looked wrong; a CI check now set-diffs all five against the sources
- **Game.OpenGL defects found by review rather than by use**: props parented into a hierarchy were culled and sorted in local rather than world space, the instanced shader never wrote `world_position`, textures rendered upside down, the projection went stale on resize, an accidentally-public API surface was narrowed, and `run_gl` never passed `gl_model` its model file

### Performance
- **Native call names are no longer allocated on every call**: a string literal allocates on each evaluation, and the literal naming the native function was 92% of a call's cost &mdash; 1655ns to 130ns. Guarded by `tools/cicd/hoist_native_names.py --check` so it cannot drift back
- **Resolved native entry points are cached, on every platform**: every `EXT_LIB_FUNC_CALL` converted the function's name from wide to narrow and then asked the loader for its address &mdash; `GetProcAddress` walking a DLL's export table, `dlsym` walking the module's symbol hash &mdash; on every call, for a name that never changes. Measured at 110ns per call, 69 in the lookup and 39 in the conversion feeding it, or about 5.5% of a native call. The cache is thread-local rather than shared behind a lock, since an uncontended mutex would hand back a useful fraction of what it saves; invalidation is a generation counter, so a library unloaded and another loaded at the same address cannot be called through a stale pointer
- **Game.OpenGL holds constant strings instead of rebuilding them per call**, and reuses native-call argument buffers on the per-object paths

### Testing
- **125 of 168 assertion-bearing tests printed FAIL and exited 0**, so the runner scored them as passes; the suite scores by exit code, and two tests were failing silently inside a green run
- **The ONNX and LAME bindings had never executed on any platform**; both now have runtime tests
- **A regression test for the VM teardown crash** that fails against the unfixed VM rather than merely passing against the fixed one, and a standalone reproduction for the Windows send-then-close data loss
- **Debugger teardown coverage in both shapes**, CLI and DAP, reusing the VM reproducer rather than duplicating it. It reports 2&ndash;4 failures on 3 runs out of 3 against a build with the fix reverted, and deliberately never asks `obd` to read a second command: the command loop repeats the previous one on an empty read, so a `readline()` that returns rather than blocks on a pipe would re-run the program forever instead of quitting

### Infrastructure
- The Windows ARM64 CI leg could not run a single thing it built; `dumpbin` is now located outside the ARM64 `PATH`, and the runner is asked what the OpenCV DLLs actually import
- A crashing macOS regression test reached the log as one frameless line; the real backtrace is now collected into the failure artifact
- Release monitoring and post-publish are two tested scripts rather than prose, and the release skill no longer contradicts itself on signing &mdash; the certificate is on a hardware token, so CI signing is impossible rather than merely unconfigured
- `release-drafter` stopped advertising a macOS Intel build that has never existed
- The native-interface guide corrected two claims that were not true, and now states the generational rule library authors actually need: everything a native library allocates is old-generation, and why

## [v2026.8.3] - 2026-08-19

A full static-analysis pass. Coverity Scan now runs on Windows as well as Linux, and this release works through both reports: crashes reachable from library input, two hangs, a class of stream-format leaks, and the unguarded entry points behind every “terminated with no message”. Alongside it, the Windows installer signing pipeline was corrected — it had been reporting signatures it never produced.

### Bug Fixes
- **Compiler crash on library input**: `TypeParser::ParseType` and `ParseParameters` switch on the first character of a type string with no default case, so anything outside `l/b/i/f/c/n/m/o` left `type` null and fell straight into `SetGenerics`/`SetDimension`. An **empty** string reaches this too &mdash; `operator[](0)` on an empty `wstring` yields a null character. Both are called from `linker.cpp`/`linker.h` on type strings read out of `.obl` files, so a corrupt or crafted library crashed the compiler. `ParseParameters` already guarded the `SetDimension` dereference, which is how the `SetGenerics` one above it was missed. `ParseGenerics` is still called when `type` is null so the index advances exactly as before &mdash; only the assignment is skipped
- **REPL hang on an unmatched brace**: `Document::List` and `Document::Save` both did `ident_count--` on a `size_t` when a line starts with `}`. With the count already `0` that wraps to `SIZE_MAX` and the indent loop below runs about 1.8e19 times. A buffer whose first non-empty line is `}` was enough to hang the REPL, on listing and on save
- **Lambda silently dropped on a signature collision**: `AddMethod` returns false on a collision and `BuildLambdaFunction` ignored the result, so a colliding lambda was never registered while the code continued to `EncodeSignature` and `AssociateMethod` on it &mdash; with no diagnostic at all. `ParseAnonymousClass` now checks the same return for consistency, so a future change to its `HasDefaultNew()` guard cannot lose the method silently
- **Indeterminate `func_ref_unwrap`**: four of `MethodCall`'s five constructors null it; the `New` constructor did not. `IntermediateEmitter` tests it with `if(func_ref_unwrap)` and emits through it, so non-null garbage meant emitting a bogus call
- **Debug session lost to one malformed request**: only `json::parse` was guarded, so a message that parsed but carried unexpected types reached the handlers, threw from inside nlohmann, unwound out of `Run()` to a `main()` with no handler, and terminated the process. Dispatch is now wrapped: parse errors are dropped as before (there is no `seq` to answer), other JSON errors get a DAP error response, and `main()` keeps a backstop for the clean exit code
- **DAP flags read without a lock**: seven flags written under `mtx` by the DAP thread were read unlocked by the VM thread at every instruction. Beyond tearing, nothing stopped this `-O3 -flto` build hoisting those loads out of the dispatch loop, so a disconnect or step request could go unseen indefinitely. All seven are `std::atomic<bool>` now &mdash; including `break_on_exception`, which Coverity never flagged because its writes never hold `mtx` at all, leaving the least protected flag the one that went unreported
- **Undecodable console line re-ran the previous command**: `ReadLine()` called `BytesToUnicode` unchecked, and it appends rather than assigns while leaving prior contents in place on failure &mdash; so an undecodable line silently handed back and re-executed the last command. It now clears up front and drops undecodable input
- **Connect that never completed reported success**: `getsockopt(SO_ERROR)` was unchecked on both the POSIX and Windows connect-with-timeout paths, so on failure `err` stayed `0`. Both paths could also hand back a socket still in non-blocking mode, giving a caller expecting a blocking read a spurious `EAGAIN`/`WSAEWOULDBLOCK` instead of data; the POSIX case was worse, because an unchecked `F_GETFL` made `flags` `-1` and that value was then restored onto the socket. `SO_REUSEADDR` and the two `SO_RCVTIMEO` defaults are now checked as well, deliberately non-fatal with the consequence recorded. Windows has no `F_GETFL` equivalent to get wrong &mdash; `ioctlsocket` takes an explicit mode &mdash; so it carried the same six defects minus that root cause
- **Editor hang on a child that had not written**: the same unchecked-`F_GETFL` shape in `child_run.h` left `read_fd` blocking, stalling the poll loop inside `read()`. The failure path now also reaps the already-forked child rather than leaving a zombie writing into a closed pipe
- **ONNX leaked `std::fixed` onto every later float**: both generation reports applied `std::fixed` with `std::setprecision(1)` directly to `std::wcout` and never restored it. This is user-visible, not cosmetic &mdash; the VM's `StdOutFloatPer` sets the stream's precision but never its floatfield, so nothing on the Objeck side could clear it, and every float printed after one SLM or vision call rendered in fixed notation at one decimal for the rest of the run. Both sites now format through a local `wostringstream`, so `wcout`'s state is never touched and there is no save/restore to get wrong
- **`StdErrFloat` saved and restored the wrong streams**: it read the saved format state from `std::wcout`, modified `std::wcerr`, and restored onto `std::cout` &mdash; three different objects. `wcerr` therefore kept the fixed/scientific/precision setting permanently while narrow `std::cout` had its flags overwritten
- **Exceptions escaping `main`**: every entry point had a large unprotected prologue &mdash; `obi` (all 89 lines) and the VM's POSIX main (all 126) had no handler at all, and the three that did started their `try` well after the opening brace. That prologue is where the throws are: codecvt/locale construction in the REPL, and the long `usage += ...` `wstring` building in every tool, which is a `bad_alloc` path. Each `main` is now a thin wrapper around a static `objeck_main`. **This introduced and then fixed a POSIX segfault**: the VM's POSIX branch deliberately discards `Execute`'s result and fell off the end of the function, which is an implicit `return 0` in `main` but undefined behaviour in an ordinary function &mdash; at `-O3` no return instruction was emitted and control ran into garbage. Windows structurally could not catch it, since it takes the returning branch above
- **Negative `WriteBytes` return converted to `size_t`**: `SockDtlsOutByteAry` and `SockDtlsOutCharAry` pushed `IPDtlsSocket::WriteBytes`' return straight into `PushInt(size_t, ...)`, so the `-1` failure code reached the op stack through an implicit signed-to-unsigned conversion. It read back as `-1` on the Objeck side because the conversion sign-extends, but nothing in the code said that was intended
- **Unvalidated `/proc` arithmetic**: `GetProcessResidentBytes` computed `pages * sysconf(_SC_PAGESIZE)` from an `fscanf` of `/proc/self/statm` &mdash; a negative page count casts to a huge `size_t`, the multiply can wrap, and `sysconf` returns `-1` on error, which casts to `SIZE_MAX`. All three are checked, and the failure path leaves `rss` at `0` exactly as it already did
- **Malformed terminal escape sequences**: bytes outside the ECMA-48 parameter ranges could be stored as negative characters before being fed to `atoi`; they are now rejected the same way a short read already was

### Performance
- **Move instead of copy in the REPL and compiler entry points**: clears eight `COPY_INSTEAD_OF_MOVE` findings. Stated honestly &mdash; these run once at startup on short strings, so the runtime saving is nil; the value is a clean baseline so a real defect in these files stands out in the next report. Each move was checked for a later use rather than applied mechanically: `compiler/posix_main.cpp`'s `usage` **does** appear 22 lines later, in the mutually exclusive `else` branch, and a mechanical application of the checker's advice would have needed exactly that check to be safe

### Concurrency
- **`free_memory_cache_size` is atomic**: mutated only under `free_memory_cache_lock` but read without it by the sweep and zeroed unlocked in `Initialize`. Now `std::atomic<size_t>`, relaxed throughout &mdash; the lock still orders the mutations, and the sweep's use is a heuristic threshold that tolerates a value one cycle stale
- **Sweep/mark lock hierarchy recorded**: the reported `ORDER_REVERSAL` between `allocated_lock` and `pda_monitor_lock` cannot fire today &mdash; `CheckPdaRoots` is joined before the sweep takes its lock, the JIT thread it spawns is joined inside it, and `marked_sweep_lock` admits one collector at a time. But the safety rested on four unstated invariants, and overlapping sweep with marking &mdash; the natural next step for this collector &mdash; makes the ABBA live and hangs the VM. The hierarchy is now documented at both lock sites
- **`half_to_float_u16` rewritten with a signed exponent**: the subnormal path relied on a `uint32_t` underflow wrapping back to exactly the right exponent field. Bit-identical after the change, verified by an exhaustive sweep of all 65,536 half patterns against an independent `ldexp` reference, but the correctness no longer rests on a wrap the reader has to reconstruct

### Release and Infrastructure
- **Windows installers are signed, and the notes say so only when true**: every Windows MSI from v2026.4.0 through v2026.8.2 shipped **unsigned** while the generated notes asserted “All Windows MSI installers are digitally signed for security”. This was not a missing secret &mdash; `signtool` was configured, ran, and failed on every artifact, after which the build exited 0 by design and the publish workflow tolerated it. A false security claim survived a fully green pipeline for months. The signing certificate lives on a SafeNet eToken, so its private key is non-exportable and a GitHub-hosted runner has no USB port: CI signing is not broken, it is impossible. CI no longer attempts it, the generated notes no longer assert it, and `check_release_signatures.ps1` reports the real `Get-AuthenticodeSignature` status per installer &mdash; alerting, printing the exact `sign_release.cmd` invocation, and exiting non-zero when any installer is unsigned. It also warns when a signature is valid but **not timestamped**, since such a signature dies with the certificate rather than outliving it
- **Signing selects the certificate by thumbprint, not `/a`**: `/a` picks the “best” valid certificate, and the signing machine's store holds three valid code-signing certs with private keys. It was safe while the token was the only candidate; picking a self-signed cert would produce installers that carry a signature and fail every trust check &mdash; worse than shipping them unsigned
- **Coverity Scan on Windows**: the first Windows scan captured the whole solution and found twenty real defects, most of them in cross-platform sources that MSVC compiles differently than GCC, so the Linux scan had never reached them. The scan token is read from the environment rather than the repository
- **`linux-arm64` is smoke tested**: the POSIX smoke test excluded it on the assumption it was cross-compiled. It is not &mdash; the matrix builds it on `ubuntu-24.04-arm`, which runs the binaries it produces &mdash; so the shipped `linux-arm64` toolchain was the one platform never exercised before publication. Windows ARM64 is the only genuinely cross-compiled target and stays excluded

## [v2026.8.2] - 2026-08-16

Supersedes v2026.8.0 and v2026.8.1, which were withdrawn; this entry covers everything since v2026.6.4.

### New Features
- **Subscript the result of a method call**: `GetItems()[0]->GetName()` now works. It had never been implemented, and the grammar swallowed the attempt rather than rejecting it &mdash; `[` after a call was read as the start of a fresh static-array literal, so the call was evaluated and discarded. `GetArr()[0]->Size()` silently returned `1`, the size of the literal `[0]`, and `GetArr()[0]->PrintLine()` printed `[0]`; with an object-typed array the same root cause surfaced instead as "Undefined function/method call". The silent half is the dangerous one, since a wrong value is far harder to notice than a build failure. Implemented across the parser (all three call-parsing paths), the context analyzer (element-type resolution, index analysis, rejection of non-integer and multi-dimensional subscripts) and the emitter. **Behaviour change:** code that previously compiled and returned the wrong value now returns the correct one.
- **`AddHeader` now actually sends headers on HTTP/2 and HTTP/3**: `Http2Client->AddHeader` and `Http3Client->AddHeader` stored headers on the Objeck side and then dropped them &mdash; the trap signature had no headers argument, so they never reached the VM, and both native contexts carried a `request_headers` map the backends already consumed and nothing ever wrote. Headers now cross as a `String[]` of alternating key/value through new `HTTP2_REQUEST_HDRS` / `HTTP3_REQUEST_HDRS` traps and a 5-argument `Request` overload. The existing 4-argument trap is deliberately **untouched**: the `TRAP` operand encodes the argument count, only the JIT reads it, and CI does not rebuild `lang.obl` &mdash; so widening it would have desynced the operand stack silently, and only once the JIT threshold tripped. Verified by a server echoing the value back, not by a status code. HTTP/1.1 was never affected: it serializes headers in Objeck.
- **HTTP/3 on Windows**: `Http3Client` now works on Windows 11 / Server 2022 and later, backed by **WinHTTP over MsQuic**. It had shipped documented-and-dead: the trap handlers compile unconditionally, so the API was present and every request simply failed. The POSIX ngtcp2/GnuTLS path is untouched &mdash; none of it exists in the Windows toolchain &mdash; so this adds no vendored QUIC library, no second TLS backend and no new DLL to deploy (`winhttp.lib` ships with Windows). WinHTTP treats HTTP/3 as a *preference* and falls back transparently, so every request asserts `WINHTTP_OPTION_HTTP_PROTOCOL_USED` and **fails rather than silently downgrading** &mdash; a client that quietly spoke HTTP/2 would be the same false promise this fixes. Verified live against four HTTP/3 hosts (GET, POST with body, custom headers, session reuse) and negatively against two HTTP/1.1-only hosts, which are correctly refused.
- **`runtime.feature.http3` / `runtime.feature.http2`**: report which protocol engines were actually compiled in. Because the traps compile unconditionally, "the API exists" proved nothing, and callers could not tell *not supported* from *the network failed* &mdash; which is how the Windows gap went unnoticed. `Runtime->GetProperty("runtime.feature.http3")` returns `"1"` only when an engine is really present.
- **`obu` updater, phase 2**: `obu update` and `obu rollback` on Linux/macOS. It downloads the platform archive and its `SHA256SUMS`, verifies the digest with an inline SHA-256 **before touching disk**, then does an all-or-nothing staged swap (`.obu-work` &rarr; `.previous` &rarr; root) with an `obc -v` post-install check and automatic rollback. No shell is used for the download, extract or swap &mdash; curl/tar/obc run via argument vectors &mdash; and a per-run lock plus interrupted-swap recovery guard against corruption. Covered by an offline CI test (`test_update.sh`). Windows in-place swap and signature verification are later phases.
- **Full-screen editor in `obi`** (`/e`): raw-mode terminal editing over the REPL buffer with damage-diffed rendering, CJK/tab-correct widths and a protected read-only shell. **F5 compiles and runs the buffer as a subprocess**, streaming its merged output into a pane without blocking the UI and **F8 jumping the cursor through compile errors**; undo/redo with coalesced typing runs, Shift-selection, an internal clipboard, and an opt-in **vi profile** (F2: hjkl, i/a/o, dd/yy/gg, visual mode). `/et` opens a terminal capability test. Header-only implementation; no build-system changes on any platform.
- **`obu` updater, phase 1**: `obu check` compares the installed version against the latest GitHub release (or a pinned `--channel <tag>`), exit 0/1/2 with `--quiet` for scripting, using the system curl with no new dependencies. Every future release now ships a **`SHA256SUMS`** asset generated over the exact released file set, and a weekly workflow snapshots per-asset download counts into a committed CSV. Design in `docs/UPDATER_DESIGN.md`.
- **Debugger: structured variable inspection**: every variable was previously reported with `variablesReference` 0, so an object, array or collection was a dead end in the Variables pane &mdash; a `Map` displayed as `Collection.Map@0x1f4a2c0` and nothing more. Objects now expand into their instance fields, arrays into indexed elements, and `Vector`, `Map` and `Hash` into their contents (`Map` in key order, `Hash` as key &rarr; value), each child expanding recursively. Strings and boxed scalars (`IntRef`, `FloatRef`, &hellip;) render as their value and are deliberately terminal, since drilling into them would only expose backing storage. Expansion handles hold raw object pointers, so they are discarded on every resume and on each new stop &mdash; the collector moves objects, and a handle must never outlive the stop that produced it. Depth, child-count and cycle guards keep a self-referencing structure from expanding forever. Collection internals are located by field name where debug symbols exist and by a validated slot index otherwise, because library classes are compiled without symbols and carry no declaration names.
- **Debugger: watch and hover expressions expand**: `evaluate` returned `variablesReference` 0, so a watch on a collection dead-ended even though the Variables pane could expand it. Expressions resolving to an object or array now hand back an expandable reference, backed by a new typed evaluation entry point (`EvaluateForDapRaw`) &mdash; the existing evaluator returned only a formatted string, with no value to attach a handle to.
- **Debugger: wider DAP protocol coverage** (8 &rarr; 17 capabilities): `terminate` (the graceful stop clients prefer over `disconnect`), `breakpointLocations` (reports only lines that carry an instruction, so editors stop offering breakpoints that cannot bind &mdash; permissive until a program is loaded, since nothing can be validated before then), `exceptionInfo`, `setExpression`, `completions` (debug-console completion over the variables visible in the selected frame), `modules` and `loadedSources`. **Data breakpoints** break when a value changes rather than when a line is reached; the watch machinery already existed for the CLI `watch` command but reported only to the console and could not be created over DAP, so the stop now names the watch and reports its old &rarr; new transition. Variable paging is supported through `indexedVariables` plus `start`/`count`, and the client's own capabilities are now read from the initialize request instead of ignored.
- **Unsigned integer literals**: a `u`/`U` suffix reads a literal across the whole unsigned range and keeps its bit pattern, so `0xFFFFFFFFFFFFFFFFu` and `18446744073709551615u` are now writable. The suffix affects only how the number is scanned &mdash; the value is an ordinary `Int`, so no new type, opcodes or conversion rules come with it. Hex and binary literals are read the same way, since they are bit-pattern notation.
- **Unsigned shift `>>>` and unsigned `Int` operations**: every integer sits in a signed 64-bit slot, so `>>` copies the sign bit into the high end &mdash; there was no way to shift a value that is being used as a bit pattern rather than as a number. `a >>> n` shifts a zero in instead, at the same precedence as `>>` and `*` and left-associative. Alongside it, `Int` gains `ShiftRightUnsigned`, `CompareUnsigned`, `DivideUnsigned`, `ModUnsigned` and `ToUnsignedString`, which read both operands as unsigned 64-bit quantities &mdash; so `-1` is `18446744073709551615` and `Int->DivideUnsigned(-1, 3)` is `6148914691236517205`. Like `?->` and `??`, `>>>` desugars in the parser onto an ordinary library call, so no opcode is added and the bytecode reader, both JIT backends and the VM are unchanged. Because `Vector<Vector<Vector<X>>>` closes three type arguments with a single `>>>` token, the parser now tracks how many `>` a multi-character close still owes rather than a single "expand" flag; two- and three-deep generics are covered by the regression suite.
- **Language server reports `serverInfo`**: the initialize response carried no server name or version, so clients had nothing to display and a stale deployed server was invisible. `codeActionProvider` now also declares its `codeActionKinds` (`quickfix`, `source.organizeImports`) so clients can filter them and route "Organize Imports" to the server, and `signatureHelpProvider` declares `retriggerCharacters` to keep the signature popup alive while arguments are edited.
- **Nil-safe operators `?->` and `??`**: `a?->b()` calls `b` only when `a` is non-`Nil`, yielding `Nil` instead of faulting, and `a ?? b` supplies `b` when `a` is `Nil` (evaluating `b` only when needed). A single `?->` guards the whole remainder of a chain, so `maybe?->Trim()->ToUpper()` is safe throughout. Both desugar in the parser onto the existing `Try()`/`Otherwise()` intrinsics — `a?->b()` is `a->Try()->b()` and `a ?? b` is `a->Otherwise(b)` — so no new opcode is emitted and the bytecode reader, both JIT backends, and the VM are unchanged. A chain ending in a value type needs `??` to supply the result, since an `Int` cannot itself be `Nil`: `maybe?->ToUpper()->Size() ?? -1`. Note that `Try()` guards against any runtime error in the chain, not only a `Nil` dereference. The spelling is `?->` rather than `?.` because Objeck's member accessor is `->`; it also avoids the float-literal ambiguity `?.` would introduce, since `.` followed by a digit starts a number.

### Security
- **HTTP request-line injection via a URL path**: `Url->New` does not sanitize, and both the path and the host were appended verbatim into the HTTP/1.1 request line, so a CR/LF split the request line itself. Proven against a local listener: `Url->New("http://host/evil
- **Header injection via caller-supplied content types (HTTP/1.1 and HTTPS)**: the same flaw as the HTTP/2 and HTTP/3 case below, on the text-serialized path. `net.obs` and `net_secure.obs` appended the caller's content type verbatim at three sites, reachable from fifteen public methods, and the injected line landed **before `Content-Length`** &mdash; the request-smuggling position. Validated and failing closed at each serialization site.
- **HTTP request splitting via `AddHeader` (HTTP/1.1 and HTTPS)**: `net.obs` and `net_secure.obs` serialize the request as text in Objeck and appended caller-supplied header values verbatim at seven sites, so a CR/LF ended the field and began a new one. Demonstrated against a local listener, which received `X-Injected: yes` as a header of its own. Unlike the HTTP/2 and HTTP/3 `AddHeader` &mdash; inert because headers never reached the VM &mdash; this one always worked, and so had always been injectable. Adds `Web.HTTP.HeaderCheck`, guarding all six `AddHeader` methods per RFC 9110 5.1/5.5 as tightened by RFC 9113 8.2.1; it also rejects names beginning with `:`, since a caller-supplied pseudo-header is malformed and is the classic smuggling primitive.
- **Header injection via caller-supplied content types (HTTP/2 and HTTP/3)**: `Post`/`Put`/`Patch` passed the content type into the header serializer unvalidated. On Windows that builds a CRLF-delimited block for `WinHttpAddRequestHeaders`, whose input *is* a CRLF-separated header block &mdash; so `"text/plain
- **HTTP/3 connection IDs could be uninitialised stack memory**: the five `gnutls_rnd` calls in the QUIC client ignored their return value, and the initial connection IDs were declared uninitialised. `gnutls_rnd` leaves the buffer untouched on failure, so a CSPRNG failure transmitted stack contents in the cleartext QUIC Initial header — a memory disclosure plus predictable connection IDs, and a forgeable stateless-reset token. All sites now check, the IDs are zero-initialised, connection setup fails closed, and the callback that cannot report failure returns `NGTCP2_ERR_CALLBACK_FAILURE`. Secrets now draw from `GNUTLS_RND_KEY`.

### Bug Fixes
- **Face detection never returned a single face**: `FaceSession->Detect` and `->Recognize` produced zero results for every image at every confidence threshold, on every platform shipping the ONNX native library, since v2026.5.3. Inference itself ran normally, which made it look like a model or threshold problem. `scrfd_decode` compared a row count against an element count &mdash; each SCRFD output is `[N, cols]` and `n` is already `N`, but bbox and keypoint tensors were tested against `expected * 4` and `expected * 10`. At 640&times;640 stride 8 that is `12800 < 51200`, true for all three strides, so every stride group was skipped and the candidate list was always empty. Only the score tensor, with `cols == 1`, compared correctly, which disguised the error. Verified with the InsightFace buffalo_l models: detection now finds 2 faces in the sample group photo (scores 0.905 and 0.823), embeddings are 512-dimensional, and same-person / different-person similarity comes out at 0.767 and -0.023 respectively. Note this affects only the ONNX face API; YOLO and ResNet inference were unaffected.
- **`obu` was missing from every release archive**: v2026.8.0 shipped the updater as a headline feature and delivered no binary. `release-build.yml` never referenced `obu`, and none of the three deploy scripts built or copied it, so `bin/` held `obb obc obd obi obr` on Windows, Linux x64/ARM64 and macOS alike. The source, its POSIX makefiles and its `.vcxproj` had existed the whole time &mdash; only the packaging was absent. CI did not catch it because `ci-build.yml` exercises `obu` solely through `core/utils/updater/test_update.sh`, which **builds its own copy with test hooks**: the updater was tested continuously while never being shipped. `obu` is now built and installed to `bin/` by `deploy_posix.sh`, `deploy_macos_arm64.sh` and `deploy_windows.cmd` (x64 and ARM64), and both "Verify required binaries" gates in `release-build.yml` now list it, so a future omission fails the build before a tag is ever pushed.
- **`Web.HTTP.HeaderCheck` documentation was largely invisible**: the class landed in v2026.8.0 with only `IsValidRequestTarget` documented. `IsValidName` and `IsValidValue` &mdash; the two functions guarding every `AddHeader` call &mdash; carried no doc comment at all, and the API index only records documented members, so LSP hover and completion showed nothing for the security fix's primary entry points. Separately, `Flatten`'s doc block sat above `IsValidRequestTarget` rather than above `Flatten`, so it documented the wrong function and left `Flatten` undocumented. All four functions are now documented and correctly bound; the index goes from 1 to 4 entries for the class.
- **ARM64 JIT clobbered a general register on every float compare-against-memory**: `move_freg_freg` emitted `fmov d10, x{src}` / `fmov x{dest}, d10`, addressing the **general** register file rather than the FP file, because the `Register` enum restarts float numbering at `D0 = 0`. Every call both failed to move the float and overwrote general register `X{dest}`; when it held a live pointer, the next use followed a float bit pattern as an address &mdash; the long-open *"memory corruption of free block"* crash, whose PC `0x3F947AE147AE147B` is the IEEE-754 bits of the double `0.02`. Correcting the encoding then exposed the real defect: `cmp_mem_freg` ended with a copy that should never have been there, and since that path is reached from every JIT float division via the divide-by-zero guard, it copied `0.0` back into the **divisor**. One line, two bugs &mdash; inert-but-clobbering before, divisor-zeroing after. Removed, with the compare's operand order corrected to match AMD64 and `cond_jmp`/`cmov`. AMD64 was correct throughout. Guarded by `programs/regression/jit_float_mem_ops.obs`, gating on both ARM64 legs.
- **`obd` shipped with HTTP/2 and HTTP/3 compiled out**: `debugger.vcxproj` compiles the same `common.cpp` as the VM but defined neither protocol macro, so a program that worked under `obr` failed under the debugger. Same defect class as the `Http3Client`-on-Windows gap, one level up.
- **HTTP/3 was silently unavailable on Linux ARM64**: `Makefile.arm64` never defined `OBJECK_HAS_NGTCP2` or linked ngtcp2/nghttp3/gnutls, so `Http3Client` had no engine there even though CI installed the dependencies on that leg. Now enabled. HTTP/3 platform support is documented in `net_quic.obs`.
- **ONNX/phi3 native inference hardening**: fixed an out-of-bounds read when a model exported mismatched KV key/value tensor counts (the generation loop indexed four parallel name arrays by a single layer count), guarded the logits-tensor rank/offset access, surfaced an unsupported logits dtype instead of silently sampling token 0, and made the token sampler safe on empty/degenerate distributions. The sampler moved to a dependency-free `phi3_sampling.h` with a unit test that runs in CI.
- **Primitive receiver operand order**: `v->Pow(10)` computed `Pow(10, v)` -- 100 instead of 1024 -- whenever the receiver was a variable, instance variable or array element; literals were correct, so the two spellings of one call disagreed. All 29 multi-argument primitive functions were affected. The emitter now passes the receiver as the first argument for every receiver shape.
- **Calculated-expression receivers in expressions**: `if((1 + 1)->Pow(10) = 1024)` corrupted the heap -- the emitter dropped the call arguments, so the call consumed the comparison operand and the comparison ran on an empty stack.
- **Divide by zero at default optimization**: the constant folder kept a zero-divisor division for its runtime trap but emitted it before its operands, turning the clean trap into a silent crash. **Modulus by zero never trapped at all** -- the interpreter had no zero check on `%` -- and now traps exactly as division does.
- **Debugger String rendering**: the DAP formatter read a String's backing-array capacity instead of its length, returning NUL-padded values on POSIX platforms; both debugger front ends now read the length from the String itself via a shared layout header (`obj_layout.h`), which also ends the CLI/DAP drift that produced wrong collection sizes.
- **Integer literals past the signed range were silently wrong**: the scanner parsed them with a saturating signed conversion and never checked its range error, so `99999999999999999999` compiled as `9223372036854775807` with no diagnostic, and `0xFFFFFFFFFFFFFFFF` &mdash; an ordinary all-bits mask &mdash; produced the same saturated value instead of every bit set. Out-of-range literals are now rejected, and hex and binary are read as the bit patterns they denote. **Behaviour change:** `0xFFFFFFFFFFFFFFFF` now evaluates to `-1` rather than `9223372036854775807`, matching C, Java, C# and Rust. Any hex literal at or above 2^63 changes value accordingly; none appear in the standard library.
- **`JsonElement->Encode` produced invalid JSON**: escapes were built as `"\\u" + Int->ToHexString()`, but `ToHexString` returns a `0x`-prefixed, unpadded value &mdash; so U+2019 encoded as `\u0x2019` and U+00B2 as `\u0xb2`, neither of which is a legal escape. Every non-ASCII character was affected, and the defect was visible in a shipped artifact: `tools/lsp/server/objk_apis.json` could not be read by a strict parser. Escapes are now four zero-padded hex digits with no prefix, and code points above the BMP are emitted as a UTF-16 surrogate pair.
- **`System.IO.ConsoleIO->Instance()` never returned the singleton**: the null check was inverted (`<> Nil` where `= Nil` was meant), so the accessor returned `Nil` on the first call and allocated a fresh instance on every call after one existed. Callers survived only because the class holds no instance state, which made `Console->Print/PrintLine(Bool)` and `ReadLine` work by luck.
- **Language server documentation index silently lost API entries**: `objk_apis.json` backs LSP hover and completion docs, and a class that failed to parse was dropped with no error. A lone inline backtick in a doc comment terminated the description and failed the whole class; a bare `~` in prose (`"(typically ~1.4)"`) did the same; `ParseUse` scanned to the next `#`, swallowing a following `bundle` declaration and filing every class in the file under `Default`; method doc blocks containing a fenced example failed outright because only class comments handled fences; and the `@hidden` tag was unsupported, failing any block that used it. A trailing-comma bug also left the generated file invalid whenever the last bundle was empty. The index went from 368 to 379 entries with none lost, and parses as valid JSON for the first time. The generator's hard-coded source list had also drifted &mdash; `concurrent.obs` (all of `System.Concurrency`) was missing and the deleted `math.obs` was still listed &mdash; and it resolved `obc` through `PATH`, so a system-wide Objeck install shadowed the build tree.
- **Formatter split `??` into `? ?`**: the LSP formatter's scanner tokenized `?` one character at a time, so formatting a file that used `??` emitted `? ?` and produced source that no longer compiled. `??` now scans as a single token.
- **Language server crashed on five requests**: `textDocument/implementation`, `semanticTokens/full`, `inlayHint`, and both call-hierarchy directions returned a `Result[]` through the local argument array and cast it on the way out, which fails at runtime and killed the server process (`Invalid object cast: '?' to 'System.Diagnostics.Result'`). Each now writes into an `Analysis` instance slot, the pattern `FindReferences`/`GetSymbols` already used.
- **Requests resolved to the wrong method**: `FindMethodOrClass` converted a method's start line to the editor's 0-based numbering but not its end line, and the end line records the token *after* the closing brace — so every method's range overran onto the next declaration and any cursor past the first method of a class resolved to the first method. This affected definition, references, hover, rename and call hierarchy.

### Build
- **Windows projects build on VS2022 again**: every `.vcxproj` hard-coded `PlatformToolset` `v145` after the VS2026 migration, so the tree could only be built on a machine with VS2026 installed &mdash; a VS2022 box failed all 20 projects with MSB8020, and `deploy_windows.cmd` drives its builds through `devenv`, which accepts no command-line override. Pinning `v143` instead was not an option either, since the CI runners ship only `v145`. The projects now select `$(DefaultPlatformToolset)`, which resolves per machine.

### Tooling
- **The release build now proves the toolchain works**: every gate before this asserted only that files existed, so a release could ship binaries unable to compile or run anything and still go out green. Both platforms now compile and execute a real program from the shipped tree and assert the reported version matches the tag. Relatedly, `deploy_windows.cmd` re-runs `update_version.ps1`, which regenerates `version.h` from a hardcoded version &mdash; if it disagreed with the tag, Windows silently built a different version whose binaries then rejected the committed `.obl` set on `VER_NUM`, failing every program. The workflow now rewrites that file from the tag, and the release pre-flight refuses to proceed when the two disagree.
- The six standalone DAP test suites (~78 assertions: drill-down, protocol, data breakpoints) now run in POSIX CI -- previously they ran only on Windows, and their first Linux/macOS run immediately caught the String bug above. The tail-call receiver gate from PR #578 is pinned by a regression test, as are all the fixes above.
- **Debugger regression coverage**: two new suites, `dap_drilldown_test.py` (35 assertions over object, nested, array, object-array, `Vector`, `Map`, `Hash` and empty-collection expansion, scalars staying leaves, and handle invalidation across a resume) and `dap_protocol_test.py` (29 assertions over the new capabilities and requests). Both run from `run_dap_tests.sh`, which also gained the previously orphaned `dap_stepout_types_test.py` &mdash; it and `dap_instance_var_test.py` had never been wired into the harness, which is why the latter was able to rot unnoticed against a fixture whose line numbers had shifted.
- **Editor tooling is now tested in CI**: a `tools` job runs the formatter regression suite, a new LSP regression harness (`tools/lsp/tests/run_lsp_tests.py`, 25 assertions over the capability set and handler behavior), and the VS Code extension's TypeScript build and lint. The formatter suite had never run in this repository layout — its `OBJECK_ROOT` resolved outside the repo — and the tooling scripts appended the build tree to `PATH`, so a system-wide Objeck install shadowed it.

## [v2026.6.4] - 2026-06-28

### Bug Fixes
- **Multithreaded minor-GC crash during thread spawn**: a spawning thread's `self` and method argument were held as raw pointers in a heap `ThreadHolder` that the moving collector neither marked nor relocated. A minor GC during the spawn handoff could promote/relocate the still-live object (kept reachable via another root, e.g. the parent's array) without updating the holder, so the new thread started with a stale young pointer — after the nursery reset that slot was reused, and the stale `self` was later dereferenced as a non-object (`0xC0000005`) in `StackInterpreter::LoadClsInstIntVar`. Fixed by tracking the holder's self/param slots in a `pending_thread_roots` registry that the collector marks (`CheckPendingThreadRoots`) and relocates (`FixupPendingThreadRoots`) across every collection during the spawn handoff, registered in `ProcessAsyncMethodCall` and removed at child teardown, on both the Win32 and POSIX thread paths. Intermittent; surfaced only under heavy multithreaded churn.

## [v2026.6.3] - 2026-06-27

### New Features
- **Generational minor garbage collection**: minor (nursery) collection is now enabled. A nursery-full collection scans only the remembered set plus roots and recycles the young generation without sweeping the old generation, falling back to a full major GC under old-generation pressure. JIT and interpreter reference stores emit the generational write barrier on both AMD64 and ARM64 (`allocation_size` is now updated atomically), and the nursery is zeroed at allocation time rather than inside the stop-the-world pause. The young-generation allocator stops the world during `CollectMinor` to fix a multithreaded young-gen corruption.
- **Closure ergonomics**: function references gain three usability improvements — call a `FuncRef` directly with `v()` instead of an explicit `->Call()`; write bare lambdas with an inferred return type (`\(x) => x * 2`) that auto-wrap into `FuncRef<R>` when assigned, returned, passed as a method argument, or stored as a collection element; and give a lambda a block body (`\(x) => { ... }`).
- **`System.Concurrency` library** (`concurrent.obl`): structured concurrency primitives — `TaskScope`, `Task`, and `Monitor` — plus `runtime.*` process/GC/CPU diagnostics (GC pause, promotion, allocation rate, lock contention, and thread/STW/nursery counters) read through `Runtime->GetProperty("runtime.…")`. Now included in API-doc generation.

### Bug Fixes
- **Multi-lambda closure heap corruption**: closures sharing a scope corrupted each other's captured state because captures were keyed by outer-scope ids; captures now use closure-local ids.
- **Compiler**: re-analyzed and repeated `FuncRef` direct calls, and functional-call result chaining in expression context, are now handled correctly. Closure-captured variables no longer raise a spurious "unreferenced variable" warning.

### Performance
- **SDL2 2D rendering**: `Renderer` draws pool the boxing buffer and cache the proxy/method names, reducing per-call overhead.

### Libraries / Infrastructure
- Removed the unused Gtk3 binding (`gtk3.obs`).
- Synced the Linux/Windows standard-library lists and rebuilt all `.obl` libraries against the new toolchain version (VER_NUM 202663).

## [v2026.6.2] - 2026-06-19

### Performance
- **GC safepoint poll made nearly free in JIT'd loops**: the cooperative stop-the-world safepoint (added in v2026.6.1) emitted an unconditional `call MemoryManager::SafePoint` at every JIT label, which regressed label-dense integer loops. The poll is now (1) an inline flag test that calls into the collector only when a collection is actually active, (2) reads `&stw_active` from a callee-saved register cached at the prologue (R12 on AMD64, X19 on ARM64), and (3) emitted only at true loop back-edges rather than every label. Net: `fannkuchredux` dropped roughly in half (~59s → ~31s in the Docker harness), recovering the full regression on both AMD64 and ARM64. Validated against the GC/JIT and multithreaded stop-the-world stress suites.
- **Auto-JIT for `DYN_MTHD_CALL` (closure / function-reference calls)** on AMD64 and ARM64: closure-heavy and function-reference kernels now JIT-compile once warm instead of staying in the interpreter. `spectralnorm` (input 5500) goes from 43s interpreted to 3.5s with `OBJECK_JIT_THRESHOLD=1`, and at n=2000 reaches 0.46s — matching the hand-`native` kernel (0.40s). The remaining lever is auto-JIT threshold/warmup tuning, not coverage.
- **Inline young-generation bump allocation for `NEW_OBJ_INST` (AMD64)**: nursery object allocation is emitted inline (`atomic_fetch_add` + bounds check) instead of a call into the allocator on the hot path.
- **Interpreter float fast-path** inlined directly in the dispatch loop, alongside the integer fast-paths added in v2026.6.0.
- **ARM64 JIT whitelist parity** with AMD64 so the same instruction set auto-JITs on both architectures.

### Bug Fixes
- **JIT float-codegen correctness** (surfaced by forcing JIT with `OBJECK_JIT_THRESHOLD=1`):
  - AMD64 `Floor`/`Ceil`/`ArcTan` float codegen corrected.
  - AMD64: two latent `DYN_MTHD_CALL` miscompiles fixed.
  - ARM64: transcendental/round ops read a `REG_FLOAT` operand's register bookkeeping as a stack slot, producing garbage (e.g. `atan` returned 0); fixed.
  - ARM64: a libc float-call result/argument was dropped when its holder register wasn't `D0` (the GP-bridge `move_freg_freg` swapped FMOV opcodes); now a true `fmov Dd,Dn`. This had diverged `LogisticRegression` (`exp` in `1/(1+exp(x))`).
  - ARM64: pending working-stack integers are now spilled across an inlined float libc call whose `blr` clobbers caller-saved temps (`Sum5`/`Int->Pow`).
  - ARM64: `imm19` is masked (`& 0x7FFFF`) in the error-handler branch backpatch — a backward div-by-zero/bounds/null-deref branch was sign-extending over the `b.cond` opcode and producing an illegal instruction (`ml_gbt` SIGILL).
  - AMD64 and ARM64: a TCO'd self-recursive tail call (e.g. `return Gcd(b, a%b)`) stored into a slot before a deferred `LOAD` of that slot was consumed, corrupting the value; `ProcessStore` now materializes pending references to the slot first.
- **ARM64 JIT negative-offset memory loads** (SIGSEGV invoking a JIT-compiled closure captured in a collection, e.g. `Vector<FuncRef>->Get()` then call): the ARM64 backend's memory encoders only emitted a scaled *unsigned* `LDR`, so a negative displacement — such as the second word of a 2-word func-ref loaded at `op_stack[pos-1]` — was `abs()`'d and read the slot *above* the base, handing the lambda a garbage `self`. All ten ARM64 load/store encoders now route through one signed-offset helper that emits `LDUR`/`STUR` for negative offsets (byte-identical to the prior encoding for non-negative offsets). amd64's displacements were already signed and were never affected.
- **VM shutdown thread race**: worker threads are quiesced before program teardown, fixing a race during JIT program shutdown.
- **UTF-8 in a non-UTF-8 locale**: `obc` failed to read UTF-8 source and `obr` failed to load/print UTF-8 strings when the process locale was `C`/non-UTF-8. Replaced `mbstowcs`/`wcstombs` with systemic locale-independent UTF-8 codecs in `sys.h`, plus `setlocale` fallbacks.

### Libraries
- **`Int->MinSize()`** now returns `INT64_MIN` instead of `INT64_MAX` (`2->Pow(63)` computed `+2^63` in float and saturated on float-to-int conversion; fixed to `1 << 63`).

### Infrastructure
- **Native cross-language perf gate (CI)**: a non-Docker harness (`perf-results/cross-lang/run_native.sh`, `perf-results/check_perf_gate.py`, `perf-results/perf_baseline.json`, `.github/workflows/perf-gate.yml`) measures Objeck against Python/Ruby/LuaJIT/Java with committed baseline ratios, so performance regressions like the safepoint one are caught automatically.
- **Refactor**: `Compile()` flattened into early-return guard clauses.
- **Docs**: `docs/performance.md` refreshed with unified-Docker numbers on a 7950X3D and a prioritized speedup roadmap; static release badge added to `README.md`; deploy/bump skills hardened.

## [v2026.6.1] - 2026-06-14

### New Features
- **String interpolation — expressions, format specifiers, and `String->Format`**: `"{$...}"` interpolation now accepts arbitrary expressions (arithmetic, comparison, logical — e.g. `"{$i + 1}"`, `"{$a * b - c}"`, `"{$x > y}"`), not just bare variables and calls. Inline format specifiers in Python/.NET colon syntax control precision, width, alignment, and radix: `"{$pi:.2}"`, `"{$n:5}"` / `"{$n:05}"`, `"{$s:<10}"` / `"{$s:>10}"`, `"{$v:x}"` (hex), `"{$v:b}"` (binary). A new positional helper `String->Format("{0} = {1}", a, b)` (with `{{`/`}}` escaping) complements interpolation for reusable/runtime format strings. Backed by new library helpers `Float->ToString(value, precision)` and `String->PadTo(width, ch, is_left)`; specifiers desugar onto existing helpers with no new VM opcodes.
- **Generics: bounds, compound/F-bounds, and variance**: type parameters gain `T : A & B` compound bounds (a concrete argument must satisfy every bound), F-bounded constraints `T : Compare<T>`, and declaration-site variance — `out T` (covariant) and `in T` (contravariant) — checked soundly in both directions and preserved across the `.obl` library boundary. Existing invariant generics and syntax are unchanged (`out` stays a usable identifier); generic type-mismatch diagnostics now render readable types (`Hash<String, IntRef>`).
- **Multithreaded stop-the-world garbage collection**: the collector now coordinates safely across threads. Mutators poll safepoints in the interpreter dispatch loop and at allocation, park while a collection runs, and bracket blocking syscalls (thread join/sleep, socket I/O) so a stop-the-world pause can always proceed; the AMD64 and ARM64 JITs emit safepoint polls at every label. Validated on Windows, Linux, and macOS across x64 and ARM64.
- **Reproducible library builds**: compiling unchanged `.obs` source now produces byte-identical `.obl` output. Anonymous classes are named from their source location instead of a random token, and string-`select` cases and closure declarations are emitted in a stable (source/`mthd_id`) order rather than heap-pointer order, so committed libraries no longer churn on every rebuild.
- **Debugger (`obd`) — major CLI and DAP expansion**: the command-line debugger gains frame navigation (`frame [n]` / `up` / `down`, so you can inspect a caller's locals — the `frame` command was previously a no-op) and `locals` (print every local in the selected frame at once); `set <var> = <expr>` to change a live `Int`/`Char`/`Float`; breakpoints by method (`b Class->Method`), temporary one-shot breakpoints (`tbreak`), and per-breakpoint `enable`/`disable`/`ignore <count>` with ids shown by `breaks`; data breakpoints (`watch <expr>` / `watches` / `unwatch`) that stop when a value changes; `until <line>` run-to-line; an empty Enter repeats the previous command; and breakpoints set on a non-executable line are now relocated to the nearest executable line with a note instead of silently never firing. The DAP adapter (VS Code) adds `setVariable` (edit `Int`/`Char`/`Float` locals from the Variables pane), function breakpoints, logpoints (`{expr}`-interpolated log-and-continue messages), in-process restart, and exception breakpoints (break on an uncaught Objeck runtime error). A 24-check CLI suite and a 14-check DAP-protocol suite cover all of it across every platform.

### Security
- **TLS server-certificate verification is now on by default**: secure client sockets and DTLS connections verify the server certificate chain. Self-signed certificates for local testing can be allowed with `OBJECK_TLS_INSECURE_SKIP_VERIFY=1`.
- **Hardened untrusted-input deserialization**: object deserializers reject hostile 64-bit sizes, a `Char[]` read trap no longer lets an attacker-controllable offset overflow the heap, and additional untrusted-input paths were hardened against memory corruption.

### Bug Fixes
- **GC value corruption under heavy thread churn**: a thread executing its top-level method (or holding an object only on a parked thread's operand stack) could have those live references missed by the mark phase and reclaimed mid-use. The mark phase now scans the top-level frame and every thread's operand stack, exactly matching what the fixup phase rewrites. Also: the young-generation bump pointer is bounded with a compare-and-swap so a collection can't run off the region, a thread's GC roots are deregistered before its stacks are freed (no use-after-free during teardown), the JIT join/sleep paths park correctly, and the write-barrier flag is accessed atomically on weakly-ordered ARM64.
- **Serialization correctness**: integer arrays dropped half their elements; object-nested integer arrays truncated 64-bit values to 32 bits; object function-reference fields desynced and lost data; several further 64-bit / `Char[]` / `Float`-slot serialization bugs were fixed.
- **Compiler**: constant propagation emitted a stale literal after a non-constant reassignment; LICM hoisted trapping `DIV_INT`/`MOD_INT` out of loops that may never execute.
- **Debugger**: locals and fields appearing after a `Float` field/local showed wrong values. CLI conditional breakpoints (`b file:line if <expr>`) never parsed — the scanner downgraded the `if` keyword to an identifier, so every conditional breakpoint failed with "Expected statement end". In DAP, variables in single-slot methods (e.g. a one-parameter recursive method) were all reported as `<out of scope>` because the bounds check divided a slot count by `sizeof(size_t)`. The `MemoryManager` is now idempotent on teardown and re-initializes on reload, so the debugger can re-run a program in-process (DAP restart) without a double-free.
- **macOS / launcher**: portable application bundles failed when launched from outside their own directory; a stale `Objeck.app` bundle version was corrected.

### Performance
- **ONNX on macOS**: compiled CoreML models are persisted across runs (~35× faster warm start), and `Ort::TypeInfo` is kept alive while reading tensor type/shape.

### Documentation / Infrastructure
- AMD64/ARM64 JIT and VM/memory-manager architecture READMEs expanded; `architecture.md` documents the cooperative multithreaded stop-the-world model.
- Build: resolved `D9025`/`LNK4099`/`LNK4098` warnings and a `NativeCode` ODR violation.
- CI: Windows runners pinned to `windows-2022` (VS2022 toolset v143).

## [v2026.6.0] - 2026-06-07

### New Features
- **`System.AI` library** (`-lib ai` / `@ai`): classic AI in the standard library — graph search (`Dijkstra`, `AStar`, `BreadthFirst`, `DepthFirst` over a shared best-first core), adversarial game search (`Minimax` with alpha-beta pruning, `MonteCarloTreeSearch`), metaheuristics (`GeneticAlgorithm`, `SimulatedAnnealing`, `HillClimbing`), and tabular reinforcement learning (`QLearning`, `Sarsa`, `MarkovDecisionProcess` value iteration); all stochastic algorithms are seedable for reproducible runs
- **`System.ML` overhaul**: 13 new estimators — `RidgeRegression`, `LassoRegression`, `ElasticNet`, `Perceptron`, `SVM`, `PCA`, `GaussianNaiveBayes`, `AdaBoost`, `DBSCAN`, `GaussianMixture`, `KDTree`, `RegressionTree`, `GradientBoostedTrees`; real recursive `DecisionTree` and voting `RandomForest`; `KMeans` k-means++ seeding, iteration cap, and empty-cluster handling; `NeuralNetwork` hidden/output bias vectors (clean XOR convergence); `LinearRegression`/`LogisticRegression` intercepts, stable sigmoid, L2 regularization, and `Score`; seedable `System.ML.Random`; uniform `Fit`/`Predict`/`Score`/`IsFitted`/`Store`/`Load` API across every estimator; `ml.obs` split into seven thematic source files
- **`record` types**: `record Point { @x : Int; @y : Int; }` generates the constructor and accessors; `record : readonly :` omits setters and the compiler rejects field assignment outside constructors; supports generics, inheritance, and user-defined member overrides
- **Tail Call Optimization (TCO)**: self-recursive tail calls rewritten to jumps, eliminating stack growth (`-opt s1`+)
- **Loop-Invariant Code Motion (LICM)**: hoists `arr->Size()` reads and pure arithmetic out of loop bodies (`-opt s2`+)

### Breaking Changes
- `RandomForest->Train` is now `Fit` (uniform estimator API)
- Stored `NeuralNetwork` model files must be regenerated (serialized format gained bias vectors)

### Bug Fixes
- **VM/JIT frame-dependent traps**: traps reading interpreter locals (`Serializer->Write`, `Date->New`, file-time queries) crashed once a method crossed the auto-JIT threshold; such methods now stay interpreted on AMD64 and ARM64
- **ARM64 JIT**: stale `self` reload after JIT-to-interpreter callbacks; JIT-to-JIT errors are now diagnosable; operand-kind compile guards ported from AMD64
- **Float equality on array elements**: was compiled as an integer compare in the JIT
- **Bool array literals**: every bool static-array literal after the first silently received the first literal's data (broken literal-pool comparator); literal dedup now works for all array types; array dimensions capped at 8 with a proper diagnostic
- **Launchers**: Windows defect sweep; macOS version-check modernization
- **Doc parser**: inline backticks and bare tildes in descriptions no longer break API doc generation
- **XML parser**: truncated documents (elements left unclosed at end of input) and stray closing tags were silently accepted; both now fail cleanly. `XmlElement->DecodeString` dropped the trailing semicolon when decoding `&apos;`

### Libraries
- **Data.XML conveniences** (additive; raw `SetContent`/`GetContent`/`GetValue` contract unchanged): `XmlElement->EncodeText` escapes exactly the five markup characters while leaving whitespace readable; `SetEncodedContent` produces well-formed output; `GetDecodedContent` and `XmlAttribute->GetDecodedValue` return entity-decoded text

### Performance
- Auto-JIT now compiles methods containing `MTHD_CALL` after 10 invocations (5–15% speedup across benchmarks)
- Interpreter fast-path extended with 15 additional inline opcodes (comparisons, bitwise, shifts, logical)
- `bench_matrix_multiply` −14%, `bench_dead_code` −15%, `bench_array_intensive` −12%, `binarytrees` −7%, `mandelbrot` −6%

### Documentation / Infrastructure
- Library aliases documented: `-lib @std`/`@ml`/`@game` and the new `@ai` group, user-editable via `lib/configobjk.ini`; AI/ML developer guide gains `System.ML` and `System.AI` sections with runnable examples
- API reference regenerated (438 pages; `System.AI` classes grouped correctly); `architecture.md` mermaid diagrams fixed and the GC description corrected (young generation moves survivors)
- ARM bootstrap (`update_version_arm.sh`) built `opencv` before its `json` dependency — reordered so a clean ARM bootstrap succeeds
- CI hardening: vcpkg installs retry on transient CDN failures; `mcp_server_test` validates JSON-RPC bodies before accepting; flaky network tests quarantined with failure observability; regression timeouts added

## [v2026.5.4] - 2026-05-28

### Bug Fixes
- **`.obe`/`.obl` format detection**: correctly handles the edge case where a new-format size-header LSB collides with the `0x78` zlib CMF byte (fixed Windows CI debugger tests)
- **LSP shell script permissions**: all `tools/lsp/` shell scripts now have the execute bit set in git, fixing `Permission denied` in release CI

### Infrastructure
- Release workflow: `git checkout -f master` prevents a dirty-tree abort when committing `api.zip` from a tag-based build

## [v2026.5.3] - 2026-05-24

### New Features
- **Three-tier `select` dispatch** (AMD64 + ARM64 JIT): single-case `select` compiles to a direct compare-and-jump; 2–5 integer cases use a linear scan; 6+ dense integer cases emit a native O(1) jump table (`JMP_TABLE`/`JMP_TABLE_SLOT` opcodes); sparse or string `select` uses a binary search tree — matching the fastest dispatch strategy for each shape automatically.

### Bug Fixes
- Fixed `HttpRequestHandler` and `HttpsRequestHandler`: `ReadLine()` can return `Nil` on a dropped or errored connection; calling `->Size()` on `Nil` produced a SIGSEGV in the MCP server and any HTTP server that receives an abrupt client disconnect before sending a request line.
- Fixed `String->Split(Char)`: last token was sliced using `@string->Size()` (array capacity) instead of `@pos` (logical string length), producing an oversized trailing token on strings that did not fill the backing array.
- Fixed `bench_spectralnorm_native` benchmark: allocating arrays inside a `native` JIT function caused op-stack imbalance during nested JIT-to-interpreter callbacks, producing a garbage result (~3.84e-156 instead of ~1.274).
- **AMD64 JIT trig**: `sin`, `cos`, `tan`, `asin`, `acos`, `atan`, `atan2`, `sinh`, `cosh`, `tanh` were using x87 `fsin`/`fcos`/`ftan` — replaced with `call_xfunc` to use the C runtime (consistent with ARM64 and the LOG/EXP fixes)
- **AMD64 JIT float input**: `REG_FLOAT` as the source operand of `call_xfunc`/`sqrt`/`round` caused a crash due to incorrect register state when a float value was loaded from memory immediately before the dispatch
- **Inline optimizer jump tables**: `InlineMethod` did not shift `JMP_TABLE`/`JMP_TABLE_SLOT` label operands by `jump_inline_offset` when inlining methods containing `select` jump tables, causing every slot to resolve to ip=0
- **`CleanLabelsLocation` end-of-stream**: consecutive `LBL` nodes at the very end of an instruction list read one instruction past the end of the stream
- **`CanInlineMethod` conflict check**: used the `JUMP_OFF_INC` constant instead of the actual `jump_inline_offset` accumulator, producing false-positive label conflicts that unnecessarily blocked inlining
- **`String->SubString`**: crash on negative or zero length argument (#534)

### Security / Performance
- **Binary file integrity**: `.obe`/`.obl` files now store the uncompressed size as a 4-byte header before the zlib stream — eliminates the allocation guessing/doubling loop on load and lays the groundwork for future integrity checks
- Switched from `compress()` (level 6) to `compress2()` at `Z_BEST_COMPRESSION` (level 9) — ~10–15% smaller binary files at no runtime cost
- Replaced `calloc` with `malloc` in `CompressZlib`/`UncompressZlib` — removes wasteful zero-initialisation of buffers that are immediately overwritten by the codec
- **Backward-compatible**: files in the old raw-zlib format (CMF byte `0x78`) are automatically detected and continue to load without recompilation

### Performance
- `bench_spectralnorm_native`: rewrote `MultiplyAv`/`MultiplyAtv` with an incremental floating-point denominator, eliminating `I2F` conversions from the inner loop (2000×2000×40 iterations). Only two integer-to-float conversions now occur per outer row instead of per element.

### Infrastructure
- Consolidated the `objeck-lsp` repository into `tools/lsp/` — LSP is tightly coupled to each toolchain build and must be updated with every release
- Rewrote CI `build-lsp` job: Ubuntu runner, builds Linux x64 toolchain, compiles `objeck_lsp.obe` via `build_server.sh`, packages VS Code extension with `vsce`, assembles versioned `objeck-lsp_VERSION.zip`
- Added `publish-vscode` CI job: publishes the VS Code extension to the marketplace on release using the `VSCE_PAT` secret
- `build_server.sh` / `build_server.cmd`: `OBJECK_ROOT` is now configurable via environment variable (defaults to `../../..` relative to `tools/lsp/server/`)

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

## [v2026.2.0] - 2026-02-12

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
