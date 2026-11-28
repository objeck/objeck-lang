# Patching call sites while other threads run them

Status: describes v2026.11.4 as shipped. Written 2026-11-27 for the second independent review of the release candidate (finding R2-S4-4), which asked whether the stores `JitCompiler::PatchCallSites` makes into a running program are safe, on ARM64 in particular. The answer is yes, for the reasons below; one race the review found was removed, and the one that remains is accepted and measured.

## What is written, and by whom

A `StackInstr` (`core/vm/common.h`) has an opcode (`type`, a 4-byte enum), three operands and a native offset. After the loader has built a method's instructions, four things store into them:

| Store | Where | When |
|---|---|---|
| `type`: `MTHD_CALL` to `MTHD_CALL_JIT` | `PatchCallSites` | once per call site, when the callee's native code has been published |
| `operand3` = 1 | `PatchCallSites` | with the above |
| `operand3` = -1 | `StackInterpreter::CheckAutoJit`, at a direct call site only | when the JIT definitively refused the callee |
| `operand3` = frame offset, of a variable-access instruction | the backends' `ProcessIndices` | while compiling the method the instruction belongs to |

Nothing stores into `operand` or `operand2` after load. A `DYN_MTHD_CALL` is never written at all: its operands are an argument word count and a return type, not a method, so neither a patch (it was patched until v2026.11.4) nor the "refused" mark belongs on it. The interpreter counts a dynamic call's callee with `CheckAutoJitDynamic`, which takes no instruction; it used `CheckAutoJit`, and two threads calling different refused functions through one site stored -1 into the same word unsynchronised (R3-D-5).

The readers of an instruction another thread may be patching are:

| Reader | What it reads | Synchronised with a patch? |
|---|---|---|
| `PatchCallSites` itself: its walk of the program and the build of the call-site table | every instruction's `type` | yes: it holds `call_site_mutex` exclusively from its first read to its last store |
| a backend compiling a method (`JitAmd64::Compile`, `JitArm64::Compile`) | every `type` of that method, several times: the pre-scan, the reference map, the emitter and its look-aheads | yes: `TryAutoJitCompile` holds `call_site_mutex` shared around `Compile` |
| `JitCompiler::HasLoop`, which the interpreter calls before it asks for a compile | every `type` of the method | yes: it takes `call_site_mutex` shared |
| the interpreters of every thread executing those instructions | the `type` they dispatch on and, at a direct call site, `operand3` | no: see "The race that remains" |

## The races that were removed

`call_site_mutex` is a reader-writer lock (`std::shared_mutex`) and `PatchCallSites` is its only writer.

**A walk or table build against a patch (R2-S4-4).** The table is built by the second method compiled; the first method's sites come from a plain walk. Only the table build used to hold the lock, so the first method's patch could store an instruction's `type` while another compiling thread's walk or build was reading it. The build accepts either opcode as a call site, so no wrong table could result on any machine this runs on, but the two were unsynchronised. `PatchCallSites` now holds the lock, exclusively, from its first read of the program to its last store.

**A backend's compile against a patch (R3-D-4).** A method's call sites belong to its callees. While thread A compiled a caller, thread B could finish compiling one of its callees and patch the caller's call site: B stored the opcode A's backend was reading. `MTHD_CALL` and `MTHD_CALL_JIT` compile to the same call on both backends, so no wrong code came of it, but it was a data race whenever two threads compiled a caller and its callee together. A compile now runs with the lock held shared, and so does `HasLoop`. Compiles still run side by side; a patch waits for the compiles in flight (milliseconds) and is a handful of stores. The backends themselves are unchanged: the lock is taken in `TryAutoJitCompile`, the one place a compile is started, and neither `Compile` nor `HasLoop` is entered with it held.

The cost is one shared lock per compile and per `HasLoop`, and one exclusive lock per compiled method.

## The race that remains, and why it is accepted

An interpreter can execute a call site while `PatchCallSites` stores into it. `type` and `operand3` are plain fields, so by the C++ standard that is a data race. It is left that way deliberately:

- Making the two fields `std::atomic` with relaxed loads was measured on 2026-10-31 (Linux x64, 17 interpreted benchmarks, interleaved medians of five, functions aligned to 64 bytes): +1.5% geometric mean, up to +10.7% on one benchmark, because the compiler schedules the dispatch loop's opcode load differently. The bar for the interpreter is 1%.
- The protocol does not depend on the order in which another thread sees the stores.

The protocol:

1. **The operands a call needs never change.** The class and method ids of a call site are written by the loader before any thread runs. `MTHD_CALL_JIT` reads the same two operands `MTHD_CALL` read. There is no state in which a reader holds a new opcode and old operands.
2. **Each store is one aligned word holding a value that is valid alone.** A reader sees the old word or the new one, never a mixture, on x64 and ARM64.
3. **No reader needs `type` and `operand3` to agree.** `MTHD_CALL` with `operand3` 0 or 1, and `MTHD_CALL_JIT` with either, all reach `ProcessJitMethodCall` or the interpreted call for the same callee. `operand3` -1 means "interpret this site", which is always correct.
4. **The code pointer is not taken from the site.** `ProcessJitMethodCall` loads the callee's `native_code`, an atomic published with a release store by `Compile` before `PatchCallSites` is called, and read with acquire.

Point 4 is the ARM64 question. x64 shows other cores a thread's stores in the order it made them, so a reader that sees the new opcode also sees the code pointer. ARM64 does not promise that for two plain stores to different addresses: a reader can see `MTHD_CALL_JIT` before it can see the pointer. That reader finds `GetNativeCode()` null and calls `TryAutoJitCompile`. The method is still `JIT_COMPILING` (it becomes `JIT_DONE` only after `PatchCallSites` returns), so the claim fails, `WaitForCompile` waits until the state leaves `JIT_COMPILING`, and the caller reads the pointer again. `jit_state` is stored with release after the pointer and loaded with acquire, so a thread that has seen `JIT_DONE` sees the pointer. If the pointer is somehow still not there, the call is interpreted. A stale read costs one slower call. It cannot run code that is not published, and it cannot call the wrong method.

The same holds for the stores `ProcessIndices` makes: the interpreter reads `operand3` only for call instructions and for `JMP_TABLE` (its default target, written by the loader), never for a variable access, and a method is compiled by one thread (`StackMethod::ClaimJitCompile`).

## The inline caches of virtual and func-ref sites

A compiled `virtual` or func-ref call does not go through an instruction at all: its site has a `JitVirtualSite` (`core/vm/common.h`), up to four records of key (receiver class or func-ref word), native entry and target. This is a second reader/writer pair, separate from the one above, and it has no accepted race:

| Reader | Reads | Ordered after the writer by |
|---|---|---|
| compiled code, on a hit | `current`, then that record's key and entry | the release store of `current`, made after the record's three words; a record is never written twice |
| `JitResolveVirtualSite`, on a miss, without the site's lock | `used`, then `records[0, used)` | the release store of `used`, made after the record it admits is complete, and the acquire load before the scan |
| the same miss | `declaration`, `decl_cls_id`, `decl_mthd_id` (set once, by the constructor) | the constructor's final release store of `used` and the same acquire load |
| `FillSiteRecord` | everything | the site's `filling` flag, a spin lock |

Until 2026-11-27 `used` was a plain `int` and the miss scanned the records while a fill on another thread could be adding one. ThreadSanitizer reported it in the release soak. It could not dispatch to the wrong method, because the scan only decides whether to resolve again and the fill looks the key up a second time under the lock, but it was an unsynchronised read of a word being written. The third row matters because a thread can arrive in a method's compiled code without ever loading its `native_code` in C++: a compiled caller loads `native_entry` itself.

## What a compiler could do with the formal race

A compiler may assume a plain field does not change between two reads with nothing in between that could write it. The interpreter reads a site's `type` once to dispatch, and the handlers that read it again (`ProcessMethodCall`) accept either value at each read, as point 3 says, with calls into other translation units between the reads. No code path computes something from one read and relies on a second read matching it. GCC, Clang and MSVC, at the optimisation levels the VM is built with, load the word where the source reads it.

## If this changes

Revisit the decision, and the measurement, if a reader ever needs two fields of an instruction to be consistent, if a patch ever writes more than one word that must be seen together, or if an operand that a call needs becomes writable after load.

## Checks

- ThreadSanitizer. Build `obr` with `make CXX="g++ -fsanitize=thread -g -fno-omit-frame-pointer"` (compile and link both need the flag) and run `python3 programs/regression/run_vm_flag_tests.py <bin> --only jit_compile_race` with `TSAN_OPTIONS=exitcode=0`: section 39 generates 24 caller/callee pairs, has one thread compile each caller while another compiles and patches its callee, and fails on any report whose two access stacks are `PatchCallSites` and a backend or `HasLoop`. Before the shared lock: 24 such reports in each of three runs; after: none. Reports that pair `PatchCallSites` or `CheckAutoJit` with `ProcessMethodCall` or the dispatch loop are the accepted race above.
- `OBJECK_JIT_PATCH_VERIFY=1` makes every `PatchCallSites` call walk the program as well and stop the VM if the table and the walk disagree, or if any `DYN_MTHD_CALL` has a non-zero `operand3` (a dynamic call site was written to; `jit_dynamic_call_shared_site.obs` is the test that makes two threads try). The regression suite runs under it, at the default threshold and at `OBJECK_JIT_THRESHOLD=1`, before a release.
- `virtual_first_call_threads.obs`, `virtual_dispatch_threads.obs` and `query_url_cache_processes.obs` start many threads that compile and call the same methods at once.
- ARM64 is the weakly ordered target: the regression suite at `OBJECK_JIT_THRESHOLD=1` on macOS and Windows ARM64 is the run that exercises point 4.
