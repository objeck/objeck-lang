# Structured Concurrency + Runtime Monitoring — plan

A two-track effort: (A) a **structured-concurrency** facility (`TaskScope`/nursery) and
(B) a tiered **runtime-monitoring** API, meeting at a live-tuning Boing Ball demo. Derived
from a fan-out investigation of the Thread model, the GC/stop-the-world lifecycle, the
monitor surface, and a runtime-metrics catalog.

## Status — ALL PHASES SHIPPED

- **Phase 0** — MUST-have monitor keys (threads/STW/nursery/overhead). `feat(vm): Phase 0`.
- **Phase 1/2** — `TaskScope` validated + self-checking regression (`programs/regression/task_scope.obs`). `test(concurrency)`.
- **Phase 3** — NICE metrics (pause/promotion/alloc-rate/contention/uptime). `feat(vm): Phase 3`.
  Plus a table-driven `GetRuntimeStat` refactor.
- **Phase 1 (lib)** — `System.Concurrency.{Task,TaskScope,Monitor}` shipped as a new
  self-contained `concurrent.obl` (no `lang.obl`/`gen_collect.obl` change; wired into
  `update_version{,_arm}.sh` + `ci-build.yml`). `feat(lib)`.
- **Phase 4** — `programs/examples/boing_ball_mt.obs`, the multitasking tuning console.
  `feat(examples): Boing Ball MT`.

Outcome matched the feasibility verdict: **zero VM change required for concurrency**; the
only C++ was the additive monitors. Deferred: typed `Runtime` accessors *in lang.obs* (the
`Monitor` class covers this from the new lib without a `lang.obl` rebuild); mid-work
(non-boundary) cancellation; a bounded worker pool.

## Feasibility verdict

**Structured concurrency needs ZERO VM changes.** It is 100% `lib_src`. The existing
`System.Concurrency.Thread` already provides async spawn (`ASYNC_MTHD_CALL` via `Execute`)
and join (`THREAD_JOIN` via `Join`, already `BeginBlocking`-bracketed so the joining parent
counts as parked for GC). Combined with first-class lambdas, `ThreadMutex`/`critical{}`, and
the `leaving{}` finally block, that is enough to build a nursery as pure library code. Because
each worker **is** a real `Thread`, it inherits the correct
`RegisterMutator`/`SafePoint`/`UnregisterMutator` machinery — the PR #574 stop-the-world
invariants hold with no new C++ collection-adjacent path.

The **only** VM change is additive and exclusively for **monitoring**: public getters on
`MemoryManager` + new `runtime.*` keys in `GetRuntimeStat`. Non-load-bearing for correctness;
lock-free reads that cannot race the collector.

> Note: Objeck currently ships only `Thread` + `ThreadMutex` + `critical{}` — no pool, future,
> channel, or concurrent collections. This is the first higher-level concurrency layer.
> Note: Objeck has **no `try/catch`**; error propagation uses a **return-value / sentinel
> convention** (`leaving{}` is its finally), so `Task` captures the lambda's return value and
> records failures as a value in `@error` rather than unwinding an exception.

> **Prototype validated** (`programs/regression/task_scope.obs`, PASS): fan-out runs closures
> as real concurrent workers (`runtime.threads.active` 1→4→1), `leaving{ JoinAll(); }` joins on
> both normal exit and early return, boundary cancellation skips a task's body, and the active
> count returns to baseline — clean mutator register/unregister, no VM change.
> **Two closure constraints discovered:** (1) task bodies must use the explicit
> `FuncRef->New(\() ~ R : () => {})<R>` form (assigning a bare lambda to a typed `FuncRef`
> field trips a FuncRef/function-reference error); (2) a lambda may call **static functions but
> not instance methods**, so a running task body cannot poll `scope->IsCancelled()` from inside
> the closure — **cancellation is boundary-only** (`Task.Run`, an instance method, checks before
> invoking the body). Mid-work cancellation would need a static-callable poll helper or a
> closure enhancement; treat it as a later refinement.

## Track A — structured concurrency (`bundle System.Concurrency`, pure lib)

- **`Task from Thread`** — wraps a captured `\(Base)~Base` lambda; stores result/error in
  instance fields; polls a cooperative cancel flag.
- **`TaskScope`** (nursery) — `Spawn(closure[, arg])` mints one child `Thread`; `JoinAll()`,
  `Cancel()`, `IsCancelled()`, `FirstError()`. Mutex-guarded child list.
- **Invariant via `leaving{}`** — the scope body runs under `leaving { scope->JoinAll(); }`, so
  every exit path (normal, early return, error unwind) joins all children before the scope
  returns. "No child outlives the scope."
- **Cancellation is cooperative** — `Cancel()` sets a flag long-running `Run` loops must poll;
  the VM has no preemptive interrupt.
- **Hard rule:** each `Task` = exactly one VM `Thread` ⇒ exactly one register/unregister on the
  worker's own thread. The library never registers mutators itself and uses **only**
  `THREAD_JOIN`/`Sleep` for blocking (never a raw native blocking call that skips
  `BeginBlocking`) — otherwise it recreates the #574 STW bug.

```
scope := System.Concurrency.TaskScope->New();
leaving { scope->JoinAll(); }
a := scope->Spawn(\(x:System.Base)~System.Base => { return Work(x); }, in1);
b := scope->Spawn(\(x:System.Base)~System.Base => { return Work(x); }, in2);
# all children joined here even on error; then a->GetResult() / scope->FirstError()
```

## Track B — runtime monitoring (additive VM patch)

Tiered so we expose signal, not noise.

| tier | keys | backing |
|---|---|---|
| **MUST** | `runtime.threads.active` / `.parked` / `.running` | `mutator_count`, `parked_count`, `max(0,active−parked)` (atomics) |
| **MUST** | `runtime.gc.stw` | `stw_active` (acquire load) |
| **MUST** | `runtime.gc.nursery.used` / `.occupancy_permille` | `young_offset`, `×1000/young_region_size` |
| **MUST** | `runtime.gc.remembered` | `dirty_count` (resets each minor GC) |
| **MUST** | `runtime.memory.overhead` | `RSS − allocated` — surfaces the unbounded-RSS/fragmentation finding |
| **MUST** | `runtime.gc.*`, `runtime.memory.*`, `runtime.cpu.*` | already shipped |
| **NICE** | `runtime.gc.pause.last_us` / `.max_us` / `.avg_us` | new atomics: `steady_clock` delta around `CollectMemory` |
| **NICE** | `runtime.gc.promoted.last` / `.bytes` | hoist already-computed sweep locals into atomics |
| **NICE** | `runtime.alloc.since_gc` / `.rate_bps`, `runtime.uptime_ms` | snapshot `allocation_size` + `start_time` |
| **NICE** | `runtime.gc.old.bytes` / `.objects` | **atomic mirrors** (never read the live `unordered_set`) |
| **NICE** | `runtime.jit.methods` / `.bytes`, `runtime.gc.contention` | new atomics at Compile / trylock-failed branch |
| **SKIP** | `gc.mode`, `free_memory_cache_size`, raw parked/stw gauges, lifetime object counters, per-thread CPU | racy / noise / out of scope for a getter-only change |

## Phases

- **Phase 0 — Monitor getters + MUST keys** (additive C++, VM-only). `memory.h` getters
  (`GetMutatorCount`/`GetParkedCount`/`IsStwActive`/`GetNurseryUsed`/`GetNurseryCapacity`/
  `GetRememberedCount`) + `GetRuntimeStat` MUST keys. No new locks; reuse existing atomics.
  *Deliverable:* built VM where `Runtime->GetProperty('runtime.threads.active')` etc. return
  live values; a smoke test prints them. No GC behavior change.
- **Phase 1 — `Task` + `TaskScope` in `lib_src`** → rebuild `lang.obl` (WSL `sys_obc`, see
  [version-bump-lang-obl]) + commit. *Deliverable:* sample spawns N closures, auto-joins via
  `leaving{}`, reads results — structured spawn/join/error/cancel, no VM change.
- **Phase 2 — Regression + STW stress** (`programs/regression/task_scope.obs`): force early
  return and error unwind through `leaving{}`; assert all children join (`parked_count` returns
  to baseline), cancellation stops long loops, errors propagate; pair with
  `core_thread_gc_stress`. *Deliverable:* green entries proving STW invariants + counter
  conservation on every exit path.
- **Phase 3 — NICE metrics** (pause/promotion/rates/JIT + atomic old-gen mirrors) + typed
  `Runtime->IsCollecting()`/`GetNurseryOccupancy()`/… accessors.
- **Phase 4 — Boing Ball "terminal-while-ball"**: render worker + background churn worker under
  one `TaskScope`; HUD polls the new thread/STW/pause/nursery stats each frame. The tuning
  console — add workers and watch parked-count and GC-pause pressure rise live.

## Risks

- **One OS thread per Task doesn't scale** — naive spawn-per-item floods the STW barrier (a
  collection waits for *all* to park). Document `Task` as coarse-grained; defer a bounded pool.
- **STW counter conservation under unwind** — library must use only `THREAD_JOIN`/`Sleep`;
  Phase 2 stress test asserts `active`/`parked` return to baseline on every exit path.
- **Cooperative cancel only** — a tight non-polling loop or a blocked syscall ignores `Cancel()`.
- **Racy old-gen reads** — `old_allocation_size`/`old_generation.size()` are mutated under lock
  and during STW; back `gc.old.*` with atomic mirrors, never a live set read.
- **`lang.obl` rebuild discipline** — Phase 1 must rebuild+commit `lang.obl` or windows-x64 CI
  fails.
- **`_GC_SERIAL` build** — thread/STW metrics are trivially constant there; document.

## Demo tie-in

Boing Ball "terminal-while-ball": one `TaskScope` owns a render worker (the ball keeps
spinning), a CPU/alloc-bound background worker (drives old-gen churn → `CollectMinor`/
`CollectMajor` under real multi-mutator STW), and a HUD polling `runtime.threads.*`,
`runtime.gc.stw`, `runtime.gc.minor/major`, `runtime.gc.nursery.occupancy_permille`,
`runtime.gc.pause.last_us`, `runtime.memory.overhead`. Spawn/cancel workers live and watch the
GC pressure respond — and on quit, `leaving{ scope->JoinAll(); }` joins everything cleanly
(active count → 1) with no corruption: the #574 lesson, end to end.

---
*Generated from a six-investigator fan-out + synthesis workflow; verified against source
(no `try/catch`; `ASYNC_MTHD_CALL`/`THREAD_JOIN` confirmed in `lang.obs`).*
