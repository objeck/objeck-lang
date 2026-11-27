# Halting a thread that runs compiled code

Status: design note, not implemented. Written 2026-11-26 with the v2026.11.4 fix for a crash at exit (`exit_with_compiled_thread_running.obs`). That fix makes the case safe; this note describes the change that would make it prompt.

## What happens today

When `Main` returns, `Execute` in `core/vm/vm.cpp` does three things in order:

1. `StackInterpreter::HaltAllExcept(intpr)` sets the `halt` flag (a `std::atomic<bool>`) of every registered interpreter except the main thread's.
2. `StackInterpreter::WaitForThreadsToDrain(intpr, 2000)` waits up to two seconds for every other thread to leave its entry method and deregister.
3. If they all did, the program is freed, the native libraries' `unload_lib` runs and `main` returns, so `exit()` runs the process teardown. If one did not, the program image is left in place (`Loader::Abandon`) and the process ends through `MemoryManager::EndProcess(SUCCESS, false)`: streams flushed, then `_Exit`, no teardown.

The debugger does the first two per run, in `Debugger::ClearProgram` (`core/debugger/debugger.cpp`), and the Debug Adapter Protocol's `disconnect`, `terminate` and `restart` call `HaltAllExcept(nullptr)` to stop a running program.

## Who sees the halt

| What the thread is doing | Sees the halt | How |
|---|---|---|
| interpreting bytecode | yes, at the next instruction | the dispatch loop in `StackInterpreter::Execute` is `do { ... } while(!halt)` |
| blocked in a system call (accept, recv, sleep, join) | no | it is not executing; it sees the halt when the call returns |
| running compiled code | no | compiled code never reads `halt` |

Compiled code is the third row. The JIT emits no load of the halt flag anywhere. A compiled method returns to the interpreter only when it returns, calls a method that is not compiled, or fails. An entry method with a loop is compiled when the thread starts (`JitEagerLoops` in `Execute`), so a thread whose `Run` is one long loop is in compiled code for its whole life.

Measured on Windows x64 with two such threads printing in a loop: the process takes 2.2 s to exit after `Main` returns, and the threads print throughout.

## What compiled loops do poll

Every loop header in compiled code polls the collector's stop-the-world flag:

- AMD64 (`JitAmd64::EmitJitSafePoint`, `core/vm/arch/jit/amd64/jit_amd_lp64.cpp`): `cmp byte ptr [r12], 0` and a `je` over the slow path. R12 holds `&MemoryManager::stw_active` for the whole method, loaded once in the prologue.
- ARM64 (`JitArm64::EmitJitSafePoint`, `core/vm/arch/jit/arm64/jit_arm_a64.cpp`): the same test, two instructions, on a register that caches the same address.

The poll is emitted at back-edge targets only (`safepoint_lbl_indices`). When the flag is set, the slow path spills pending values to collector-visible slots and calls `MemoryManager::SafePoint()`, which counts the thread as parked and sleeps on `stw_cv` until `stw_active` clears. An allocation that needs a collection is a safepoint as well: compiled code reaches the collector through `JitStackCallback`.

The interpreter polls the same function every 1024 instructions.

So a compiled loop already has one conditional branch per iteration that leaves compiled code when the VM asks. The halt can ride on it without a second load or branch.

## The change

Make "halt" raise the same flag the loops already test, and leave the halted threads parked.

1. A new entry in `MemoryManager`, called by `HaltAllExcept` at exit (and by the debugger's halt): take `stw_lock`, set a new `halt_requested` flag, set `stw_active`, and do not clear it.
2. `MemoryManager::SafePoint()` slow path: unchanged for a collection. When `halt_requested` is set the thread stays parked for good; it is counted in `parked_count` like any parked thread.
3. The exit path waits for "every other mutator is parked or blocked" (`parked_count >= mutator_count - 1`, the collector's own condition in `WaitForMutatorsToPark`) with the two-second bound it has now, instead of waiting only for threads to deregister.
4. Ending the process stays as it is after the v2026.11.4 fix: a thread that parked did not deregister, so the VM ends through `EndProcess(status, false)`, without teardown.

A parked thread cannot be unwound: its compiled frames hold no unwind information the VM could use, and it may be anywhere in a loop. It does not need to be. It holds no VM lock while parked (the park happens with `stw_lock` released inside the condition wait), and the process is about to end.

What this buys: the exit is prompt instead of two seconds late, and a compiled thread stops producing output and other side effects a few microseconds after `Main` returns. What it does not buy: a full teardown. That would need every thread unwound, which this does not do.

## Interaction with the collector's stop-the-world entry

This is where the risk is, and why the change was not made during the v2026.11.4 freeze.

**A collection may be running when `Main` returns.** `stw_active` is then already set by the collecting thread, which is inside `WaitForMutatorsToPark` and is waiting for the main thread among others. The main thread must let that collection finish before it raises the flag for good. The collector's own entry shows how: `BeginBlocking()`, take `marked_sweep_lock`, `EndBlocking()`. Holding `marked_sweep_lock` from then on also keeps any later collection from starting. A thread that needs one finds the lock taken, calls `BeginBlocking()` and waits, which counts it as parked.

**The main thread is a registered mutator that never parks after `Main` returns.** Today that has a side effect the exit path relies on without saying so: a thread that starts a collection after `Main` returned waits forever for the main thread, so every allocating thread stops by itself within one nursery's worth of allocation. This was seen in gdb on Linux x64: at the entry to `exit()` both allocating threads were inside `CollectAllMemory`, one waiting on `stw_cv`, the other on `marked_sweep_lock`. The change would replace that accident with an explicit stop. It must not turn it into a deadlock of the exit itself, so every wait the main thread makes on this path needs a bound.

**The interpreter may park before it sees the halt.** The dispatch loop tests `halt` once per instruction and polls `SafePoint()` once per 1024. With the flag raised for good, an interpreted thread that reaches the poll first parks and never deregisters. Today it would have drained, and the VM would then have run the full teardown, the native libraries' `unload_lib` included (#1849). Two ways to keep that: test `halt` in the interpreter's poll before calling `SafePoint()`, which is on the 1-in-1024 path and not per instruction; or raise the flag only after a short grace period in which interpreted threads drain as they do now.

**A thread inside `BeginBlocking` / `EndBlocking` counts as parked but may be running native code.** A blocking socket read counts as parked while the native call is still executing. For a collection that is correct, because the thread's VM state is frozen. For process teardown it is not: the native code may use statics that `exit()` destroys. So "all parked" must not be read as "safe to tear down". This is why step 4 keeps `_Exit`.

**`OBJECK_STW_WATCHDOG_MS`.** The watchdog reports a stop-the-world wait that exceeds its limit. A halt that parks threads for good is a wait that never ends, by design. The exit path must either not arm the watchdog or end the process before it fires.

**The debugger.** `obd` runs one program after another in a process. A flag left set at the end of one run stops the next run's threads at their first poll. `StackInterpreter::Initialize` already clears the "halt went out" record for this reason; the new flag, and `stw_active` with it, need the same reset, and it must happen only once no thread of the previous run can still be parked on it. A thread parked for good from an earlier run is never released, so `mutator_count` stays one too high and every later collection in that process waits for a thread that will not park. The debugger case therefore needs the parked threads released, or ended, when a run is cleared. This part is not designed yet.

## Evidence a change would need

No instruction is added to compiled loops, so this is not a performance change, and benchmark numbers are not the evidence. The evidence is that stop-the-world is still correct and that exits do not hang:

- The full regression suite with the default JIT and with `OBJECK_JIT_THRESHOLD=1`, on Windows x64 and Linux x64.
- `tools/cicd/stress_probe.py`, every cell: `default`, `jit1`, `nursery256k` and `verify256k`. The small-nursery cells are the ones that collect often enough to meet a halt in mid-collection.
- The thread tests under oversubscription, as for #861: more runnable threads than processors, since an exit racing a collection needs a thread descheduled inside the window.
- `OBJECK_STW_WATCHDOG_MS` set low (50) across the thread and exit tests: no report at exit, and no report in a run that does not halt.
- `OBJECK_THREAD_FAULTS=isolate` across the regression suite: an isolated thread fault ends one thread while others run, and must not leave the flag raised.
- Exit loops, 500 runs or more per cell, pre-change control included: `exit_with_compiled_thread_running.obs` (printing threads), allocating threads, a thread parked in `accept` (`thread_accept_exit_test.obs`), threads just started (`thread_exit_before_start.obs`), and `Runtime->Exit` with busy threads (`exit_with_live_threads.obs`). The measure is the exit status and the time to exit.
- The debugger and Debug Adapter Protocol suites, with a restart after a run that left a compiled thread running.
- ARM64, on the CI legs and the ARM64 gate machines: the poll sequence is different code there (`JitArm64::EmitJitSafePoint`), and the memory ordering between raising the flag and reading `mutator_count` matters more on a weakly ordered processor (see the comment in `MemoryManager::RegisterMutator`).

## Until then

A program that returns from `Main` with a thread busy in compiled code takes about two seconds to exit, and that thread keeps running, printing included, for those two seconds. The exit status is 0 and the program's output is flushed. A program that needs its threads to finish joins them; one that needs them to stop promptly gives them a flag of its own to test.
