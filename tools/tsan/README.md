# ThreadSanitizer runs of the VM

`run_tsan.sh` builds `obr` with `-fsanitize=thread` and runs the regression tests that start threads under it. It is a local tool: no workflow runs it yet (see `docs/TESTING_AND_ASSURANCE.md`, "What nothing here covers").

```
tools/cicd/local_linux.sh --skip-regression --skip-libs     # or any tree deploy_posix.sh has built
tools/tsan/run_tsan.sh ~/objeck-ci/<sha>
```

It needs Linux x64 with GCC and python3, takes about five minutes on a quiet 32-thread machine (half of it the build), and fails if a test fails or a report against the VM is left. `logs/by_test.tsv` under the work directory names the test for every report. The header of the script has the options and the layout of what it writes.

## The four files

| File | What it holds | When to add to it |
|---|---|---|
| `run_tsan.sh` | The build and the run. `--raw` ignores the two lists below. | |
| `tsan_reports.py` | Sorts what is left into reports against the VM, which fail the run, and races between two of the program's own field accesses, which are listed and not counted. | |
| `tsan.supp` | Reports that are understood and are not faults of the VM: the accepted call-site patching race, program threads nobody joined, glibc's and ONNX Runtime's internals. | Only with the reason in the file, and what would make the entry wrong. |
| `racy_tests.txt` | Tests left out because all a run of them can say is a known report that neither of the above can express. | Only with what races named. |

A new report is first a question about the VM. The order to ask in:

1. Is it VM state (the collector's tables, a method's compile state, a socket context, a static in C++)? Then it is a fault until shown otherwise: fix it, or make the access what it really is (an atomic, a lock, a one-time initialization).
2. Is it a word of an Objeck object or of class memory, with the interpreter's field instructions (`StackInterpreter::*ClsInst*Var`) on BOTH sides? Then the program, or a library written in Objeck, races on its own data, which the language allows. `tsan_reports.py` sets these aside by that shape. With a field instruction on one side only, the other side is the VM, and it is case 1. To name the field, a debug build that logs the storing and loading threads per class and field index does it in a few minutes.
3. Is one side inside a library that is not built with the sanitizer? Its locks are invisible. Suppress the library, not the VM function that called it.

## What the tool cannot see

- **Compiled code.** The JIT's output is not instrumented. A field access, an allocation or a call made by compiled code is invisible, both as a race and as a synchronization. A report whose two stacks meet only through `<null>` frames may be ordered by something the tool could not see: `JitVirtualSite` (`core/vm/common.h`) is an example, and its comment says how the order was made visible.
- **`std::atomic_thread_fence`.** GCC warns that the sanitizer does not model it; the fences in `PushFrame`/`PopFrame` and the collector's frame walks order nothing as far as the tool knows.
- **Stop-the-world.** That a thread is parked is a fact the collector establishes with a counter and a condition variable. The tool sees those, so a collector access against a parked mutator is normally ordered, but a lock-order report between the mark threads and the sweep cannot use "they never overlap" as an argument. The collector's locks have one order for that reason (`core/vm/arch/memory.h`).

## History

2026-11-27, v2026.11.4 release soak. 181 tests; 109 had at least one report before the fixes. What each class turned out to be:

| Report | Verdict |
|---|---|
| lock-order-inversion in the collector (`CheckPdaRoots`, `CheckPendingThreadRoots`, `CheckObject` against `FixupRoots`) | No deadlock was reachable; the sweep now takes the root-set locks before `allocated_lock`, as the mark threads do. |
| `MarkMemory` plain read against its own compare-and-swap | Benign by design; the read is a relaxed atomic load. |
| `JitResolveVirtualSite` against a site's construction | The record count is atomic and is what publishes the site. |
| `IPSecureSocket::DefaultRecvTimeoutMs` | A real fault (an override could be ignored for one socket): a one-time initialization. |
| static destruction in `MemoryManager::Exit` against `RemovePendingThreadRoot` | A real fault (#877 with an ending thread): teardown waits for the spawned-thread count. |
| `StackInstr::SetType` / `SetOperand3` | Accepted, `docs/design/JIT_CALL_SITE_PATCHING.md`; in `tsan.supp`. |
| thread leak at `ProcessAsyncMethodCall` | Program threads never joined; in `tsan.supp`. |
| `StorClsInstIntVar` against `LoadClsInstIntVar` / `StorClsInstIntVar` (10 tests) | Programs and libraries racing on their own fields: polled flags, lazily created statics, a redundant store of an unchanged mutex. Set aside by `tsan_reports.py`. |
| `SharedLibraryCall` reading a library name; `SockTcpAccept` / `SockTcpClose`; `SocketCtxDetach` | A string published through an unsynchronized static, and sockets closed under their users: four tests in `racy_tests.txt`. |
| `tzset_internal`, `libonnxruntime` | Uninstrumented libraries; in `tsan.supp`. |
