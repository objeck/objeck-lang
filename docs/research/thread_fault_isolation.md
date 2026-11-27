# Thread-fault isolation

An error nothing catches used to end the whole process from whichever thread raised it. A runtime that
hosts many concurrent agents and tool calls needs the Python/JVM/.NET behaviour: the faulting thread dies,
its bystanders do not, and whoever joins it can find out. This documents what the VM does now, how, and
what is deliberately left alone.

## Behaviour

| Setting | An uncaught error in a spawned thread |
|---|---|
| default, `--thread-faults=exit` | the process ends (exit 1), as before |
| `--thread-faults=isolate`, env `OBJECK_THREAD_FAULTS=isolate`, or `Runtime->SetProperty("runtime.threads.isolation", "isolate")` | that thread ends; everything else carries on |

Under `isolate`, for the faulting thread:

- the error text and the `Unwinding local stack` listing go to stderr, exactly as the process-ending path
  prints them;
- its frames are released, its mutator registration and interpreter go through the ordinary thread teardown
  (so `runtime.threads.active` returns to its baseline and no collection ever waits for it);
- every lock it held through `critical()` is released (a line on stderr says how many);
- the fault is recorded. After `Thread->Join()`, `Runtime->GetProperty("runtime.thread.join.fault")` is the error
  text, or empty if the joined thread ended normally. It is per calling thread, so concurrent joiners do not see
  each other's answers. `runtime.threads.faulted` counts faulted threads and `runtime.threads.fault.last` is the
  latest text. `Join` itself still returns `Nil`: no signature changed, and `lang.obl` is untouched.

Never isolated, in either mode: the main thread, `Runtime->Exit` from any thread (it is a legitimate end of the
process), and the VM's own failures: a C++ exception escaping the interpreter (`bad_alloc`, an internal error), the
collector's out of memory exit, a corrupt program, a failed join, a call stack bounds failure with no catchable
frame. These stay fatal because the VM cannot vouch for its own state after them.

### Why opt-in

No test in the regression suite depends on a worker's error ending the process (the full suite passes in both
modes, see the test list), so the default could change. It does not, on purpose. An isolated worker that dies
silently can leave other threads waiting for it (a `Join` is fine, but a condition wait or a queue that the dead
thread would have fed is not), where fail-fast surfaces the bug immediately. A host that supervises its own
workers (an agent runtime) opts in once at start up; a script keeps fail-fast.

## Mechanism

The VM already had everything a thread needs to unwind itself: `Try()` regions. An error raised in the
interpreter, a guard stub, a JIT bridge callback or a native-call callback asks `TryErrorRecovery` whether a handler
is active; the compiled-code paths return a status outward one frame at a time; a bridge interpreter hands an error to
its compiled caller (`bridge_deferred`) when the handler lies beneath it. Isolation reuses all of it by giving each
spawned thread a **sentinel handler at the bottom of its handler stack**:

- `StackInterpreter::EnterIsolatedThread` (called by `ExecuteThreadBody` when the mode is on) pushes
  `THREAD_FAULT_IP` saved at call depth **-1**. Depth -1 is below every bridge, so a bridge always defers to it
  and the error travels out through compiled frames as a status exactly as it does for a `Try()` beneath them.
- `TryErrorRecovery`, when the sentinel is the only handler left, first reports the fault once
  (`ReportThreadFault`: the `>>> text <<<` line and the frame listing, while the frames are still there; guarded by
  `fault_reported` so the error is not printed again as it passes outward), then unwinds to the entry frame and marks
  the thread `faulted` instead of jumping to a handler address.
- The dispatch loop turns `recovery_ip == THREAD_FAULT_IP` into `halt`. A method compiled on entry returns early
  with the recovery pending; `ExecuteThreadBody` clears it either way.
- `ExecuteThreadBody` then calls `FinishThreadFault` (releases the entry frame, releases held locks) and records the
  fault under the key the VM gave the thread when it was started (a counter; `NewThreadFaultKey`), which the parent
  bound to the thread's handle before publishing the handle in the `Thread` object. `Thread->Join` (interpreter
  `ThreadJoin` and the JIT's `THREAD_JOIN` callback) claims the key by the handle it holds before it waits, and takes
  the record after. (Until v2026.11.4's second review the key was the OS thread id, `GetThreadId` of the handle or
  the `pthread_t` itself. An OS id is given out again once its thread can no longer be joined, which with pthreads
  is when `pthread_join` returns, before `Join` had read the record: a thread started in that gap could drop the
  record or leave its own. `thread_fault_join_own.obs`.)
- A real `Try()` inside the worker pushes above the sentinel and recovers as always; the thread is not faulted.

Because the sentinel counts as a handler, `ThreadHasTryHandler()` is true on such a thread, so the places that
print a diagnostic only when nothing will catch it (invalid cast in a compiled callback, a failing trap, an unhandled
`Runtime->Raise`) record the error instead, and `ReportThreadFault` prints it from the record. A raise names its
class: `>>> Unhandled BoomError: worker raised <<<`.

### Locks

`ThreadMutexLock`/`ThreadMutexUnlock` (the native side of `critical()` and `ThreadMutex`) keep a per-thread list of
the locks taken. A thread that faults inside `critical()` releases them in reverse order before it exits; without that
a Windows `CRITICAL_SECTION` stays owned by a thread that no longer exists and every other thread that wants it
blocks forever. Releasing is the JVM's choice (a monitor is released as the exception unwinds) and it can expose data
a half finished critical section left inconsistent. The alternative, ending the process when a thread dies holding a
lock, was rejected: it is exactly the containment failure being fixed.

### GC and the stop-the-world rendezvous

Nothing about the unwind allocates or blocks, and it runs on the faulting thread itself while that thread is a running
mutator, so a collection cannot be waiting for it at that moment; after the body returns the existing teardown
(`UnregisterMutator`, then `RemoveThread` and `delete intpr`, in the order `AsyncMethodCall` already documents) runs
unchanged. `thread_fault_isolation.obs` runs bystanders that allocate throughout, and the suite was run with
`--gc-threshold=64k`.

## Files and functions

| File | Change |
|---|---|
| `core/vm/interpreter.h` | `TryHandlerState` gains `isolated`, `fault_reported`, `faulted`, `fault_message`; `THREAD_FAULT_IP`; `TryErrorRecovery` reports once and unwinds to the sentinel; declarations for the registry |
| `core/vm/interpreter.cpp` | `ExecuteThreadBody(…, isolate)`; the loop's `THREAD_FAULT_IP` test; `EnterIsolatedThread`, `ReportThreadFault`, `FinishThreadFault`, the fault registry, mode switch (`OBJECK_THREAD_FAULTS`), `ThreadJoin` hook |
| `core/vm/arch/jit/jit_common.cpp` | the JIT's `THREAD_JOIN` callback takes the fault record |
| `core/vm/common.cpp`, `common.h` | per-thread lock list and `ThreadMutexReleaseAll`; the `runtime.thread.join.fault`, `runtime.threads.fault.last`, `runtime.threads.faulted`, `runtime.threads.isolation` properties; `Runtime->SetProperty("runtime.threads.isolation", …)` |
| `core/vm/vm_options.h` | `--thread-faults=exit\|isolate` |

## Tests

- `programs/regression/thread_fault_isolation.obs`: six error kinds (divide by zero, Nil receiver, index, invalid cast,
  unhandled raise, failed assert) from a callee the JIT has compiled, two allocating bystanders joined by main, Join
  reporting, counters, locks, a `?->` region in a worker, 1000 faulting workers one after another with the active-thread
  count checked at the end. Runs in the default, `--jit=off` and `OBJECK_JIT_THRESHOLD=1` modes of the runner.
- `programs/regression/run_vm_flag_tests.py` `check_thread_fault_isolation` with fixture `vm_thread_fault_modes.obs`:
  the default still ends the process; `--thread-faults=isolate` and the variable end only the worker (interpreter, default
  and `--jit=1`, interpreted and compiled error); the flag beats the variable; a main-thread error and
  `Runtime->Exit(7)` in a worker still end the process (status 7); a bad flag value is refused.

## Not done, and why

- **`Thread->Failed()` / a status from `Join`.** It needs `lang.obs`, and with it a rebuilt, committed `lang.obl`
  (and a conflict on that binary with the other open `lang.obs` change). The properties above carry the same
  information with no language change; a thin `Thread->JoinFault() ~ String` wrapper over
  `runtime.thread.join.fault` is the follow-up.
- **Allocation failure.** `bad_alloc` in a worker and the collector's out of memory exit still end the process. Both can
  leave collector or allocator state half updated, so continuing is not safe without a separate audit of every
  allocation site; the error is also not a `Try()`-catchable error today.
- **Sites that exit without asking `TryErrorRecovery`** (a few JIT callbacks such as `LOAD_ARY_SIZE` on Nil, the call
  stack underflow check in `PopFrame`) cannot be caught by a `Try()` either, and are fatal in both modes.
- **Signal handlers** (`RunSignalMethod`) keep the old behaviour.
- **Locks other than `critical()`** (a `Condition` waiter that never gets its signal because the signaller died) are
  not tracked; that is the "silent dead worker" hazard above.
- **Other platforms.** Written for POSIX as well (the registry key is the VM's own, found by the `pthread_t`); built and run here on Windows
  and compile checked under WSL, but ARM64 and macOS were not run.
