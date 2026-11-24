# Fault-injection results (H6)

Question: does a runtime that owns agents, sessions and tensors contain faults better than composing libraries?
Short answer from this data: **partly, and not where the thesis wording expects.** Session and tensor lifetime
under `Close` is a real, measurable strength. Process-level containment and hang bounding are weaker than the
Python baseline, and the agent loop is library code that owns nothing the runtime can interrupt. Details,
numbers and caveats follow; the raw rows are in `results.csv` (2119 runs), the generated tables in `summary.md`.

Environment: WSL2 Ubuntu 24.04 (kernel 6.6.87.2), AMD Ryzen 9 7950X3D, RTX 4090, Objeck 2026.11.3 built from this
branch (`obc`, `obr`, `libobjk_onnx` linked to ONNX Runtime 1.24.2 CUDA), Python 3.12.3 with `onnxruntime` 1.32.0
(CPU) and numpy 2.5.5. Model: the tracked tiny BERT classifier. Seeds 1000 + repetition. Commit: the branch head.
Load: the 1-minute load average in the CSV (`load1`) had median 4.5 and maximum 28 because other work shared the
machine; latencies are therefore coarse, while the pass/fail properties below do not depend on speed.

## Headline

| Finding | Objeck | Python baseline |
|---|---|---|
| A tool, model call or network fault in one agent leaves the other agents correct and on time (run level, n=25 per cell, every in-process, model-call and network class except thread faults and allocation failure) | 100% (Wilson lower bound 87%) | 100% (lower bound 87%) |
| A thread that fails outside any `try` takes the whole process down (`thread_fault`) | **dies in 25/25 agent runs and 25/25 inference runs; bystanders contained 0%** (upper bound 13%) | survives 10/10 agent and 25/25 inference runs; thread exception printed |
| Allocation failure (`oom_burst`) | **process aborts, 10/10** (`Unable to allocate memory`, exit 1); bystanders contained 0% | `MemoryError` caught in the hog thread, 10/10 contained (but the limit was reached at 0.7 GB, not 12.8 GB, see threats) |
| A tool that never returns | agent thread lost in 15/15 runs, **no mitigation exists** (a tool limit only measures time after the fact); bystanders unaffected | naive: same, 15/15; guarded (thread per call, join with timeout): 0/15 hung, at the price of leaked threads |
| A model server that accepts and never answers (`hold`) | gives up at a fixed ~30.3 s (30 of 32 injected units; the other 2 were cut by my 80 s deadline), no way to set it | naive: hung 8/8 runs; guarded (3 s timeout): 0/8 |
| A server that trickles one byte per 0.5 s | the call succeeded after **152.6 s**: the ~30 s limit is per read, there is no total deadline | not measured for Python (its `timeout` is also per read, so it would behave alike) |
| `Session->Close()` 25 ms into 4 busy workers sharing the session | **0 wrong results, 0 crashes in 25/25 runs on CPU and on CUDA**; plain `Run` refuses cleanly (7293 and 7446 refused calls), workers holding a `BoundSession` and tensors keep producing correct answers | no `close()` exists in the Python session; dropping the reference and collecting is also safe (0 wrong) but there is nothing to refuse and nothing is released early |
| Bad input shape on 50% of a victim's calls | 3752 of 3752 refused cleanly, bystanders correct (CPU and CUDA) | 3752 of 3752 refused (exception), bystanders correct |
| Device memory limit (`gpu_mem_limit` 256 MB) hit by a hog thread | allocation refused cleanly at block 31 of 8 MB, workers unaffected 10/10 | not run (no CUDA onnxruntime in the baseline environment) |
| Silently wrong results | 0 in all 2119 runs | 0 in all runs |
| SIGKILL / SIGTERM during a run | process ends at once, GPU memory back to its pre-run level within 1.5 s (median delta 0 MB, 10/10 each), restart correct 10/10; SIGTERM does not drain | same (no handler in either) |
| stdin closed on a program reading prompts | EOF **not detected** 0/10 (`ReadLine` returns an empty string forever) | detected 10/10 |
| Disk full (`/dev/full`) on result logs | **0 of 640 failed writes reported to the program** through `WriteBuffer`; `WriteString` and `Close` return nothing | every failure raised, 720 reports for 640 writes |
| Wall-clock step of +3600 s or -3600 s mid-run | no effect (n=10 each way): timeouts use the steady clock | no effect |

## Tables

All intervals are Wilson 95%. Run level = one seeded repetition is one trial. The unit-level columns pool the
injected units of all repetitions and are optimistic because units in a run are not independent.

### Agents, in-process faults (8 agents x 8 units, 2 victim agents at rate 0.5, scripted model)

Columns: contained = every bystander unit correct and not stalled; hung = some agent had not finished at the
harness deadline; detected = the loop reported a failed call or run for the injected unit; lost = the injected
unit never finished. The "limited" and "guarded" configurations add a 100 ms tool limit.

| fault | system/config | n | contained | hung | died | silent wrong | injected detected | injected lost | injected unit ms (median) |
|---|---|---|---|---|---|---|---|---|---|
| tool_exception | objeck default | 25 | 25/25 | 0 | 0 | 0 | 202/202 | 0 | 0 |
| tool_exception | python naive | 25 | 25/25 | 0 | 0 | 0 | 202/202 | 0 | 0 |
| tool_fault (divide by zero) | objeck default | 25 | 25/25 | 0 | 0 | 0 | 202/202 | 0 | 0 |
| tool_fault | python naive | 25 | 25/25 | 0 | 0 | 0 | 202/202 | 0 | 0 |
| tool_timeout (250 ms) | objeck default (no limit) | 25 | 25/25 | 0 | 0 | 0 | 0/202 | 0 | 250 |
| tool_timeout | objeck limited | 25 | 25/25 | 0 | 0 | 0 | 202/202 | 0 | 250 (returns after the tool, then reports) |
| tool_timeout | python guarded | 25 | 25/25 | 0 | 0 | 0 | 202/202 | 0 | 100 (abandons the tool at the limit) |
| tool_huge (16 MB) | objeck default | 25 | 25/25 | 0 | 0 | 0 | 0/202 (cut at 20000 characters, marked) | 0 | 38.5 (p95 55) |
| tool_huge | python naive | 25 | 25/25 | 0 | 0 | 0 | 0/202 | 0 | 2 |
| tool_hang | objeck default | 15 | 15/15 | **15/15** | 0 | 0 | 0/118 | **118/118** | - |
| tool_hang | objeck limited | 15 | 15/15 | **15/15** | 0 | 0 | 0/118 | **118/118** | - |
| tool_hang | python naive | 15 | 15/15 | 15/15 | 0 | 0 | 0/118 | 118/118 | - |
| tool_hang | python guarded | 15 | 15/15 | 0/15 | 0 | 0 | 118/118 | 0 | 100 |
| cancel_coop (flag polled) | objeck / python | 25 | 25/25 | 0 | 0 | 0 | 202/202 | 0 | 100 (cancel to stop: 0 to 1 ms) |
| cancel_uncoop (tool ignores flag) | objeck | 25 | 25/25 | 0 | 0 | 0 | 202/202 | 0 | **1000** (cancel to stop: 900 ms) |
| cancel_uncoop | python naive | 25 | 25/25 | 0 | 0 | 0 | 202/202 | 0 | 1000 |
| cancel_uncoop | python guarded | 25 | 25/25 | 0 | 0 | 0 | 202/202 | 0 | 100 |
| thread_fault | objeck (both configs) | 10 | **0/10** | 10/10 | **10/10** | 0 | - | - | - |
| thread_fault | python (both configs) | 10 | 10/10 | 0 | 0 | 0 | - | - | - |
| thread_exit | objeck / python | 10 | 0/10 | 10/10 | 10/10 | 0 | - | - | - |

Reading. The agent loop itself behaves well for faults the model can see: a raised error, a runtime fault such as a
divide by zero, an oversized result and a slow result all become an error or a cut result, the loop continues and
the unit finishes with the right answer (100% recovered, 0 silently wrong). Two caveats the data forces: with no
tool limit set, a slow tool is not a fault at all (0/202 reported), and the limit only reports after the tool
returns (250 ms against Python guarded's 100 ms). A hung tool is the one class where the agent loop offers nothing:
the thread is lost, and the measured 15/15 is a property of the design (the documentation says so), not a flake.
`thread_exit` is a tie because Python's equivalent, `os._exit`, also ends the process.

### Agents against the loopback model server

| fault | system/config | n | contained | hung | died | silent wrong | injected detected | injected unit ms (median) |
|---|---|---|---|---|---|---|---|---|
| http500, garbage, truncjson, wrongshape, drop, truncate, rst (each) | objeck default | 25 | 25/25 | 0 | 0 | 0 | 202/202 | 2 to 3 |
| the same seven | python naive and guarded | 25 | 25/25 | 0 | 0 | 0 | 202/202 | 2 to 4 |
| delay (2 s) | objeck / python | 15 | 15/15 | 0 | 0 | 0 | 0/118 (absorbed, correct answer) | 2002 |
| slowloris (1 byte per 10 ms, ~3.1 s) | objeck / python | 10 | 10/10 | 0 | 0 | 0 | 0/79 (absorbed) | 3104 |
| hold | objeck default | 8 | 8/8 | 2/8 (deadline) | 0 | 0 | 30/32 | **30323** |
| hold | python naive | 8 | 8/8 | **8/8** | 0 | 0 | 0/32 | - |
| hold | python guarded (3 s) | 8 | 8/8 | 0 | 0 | 0 | 32/32 | 3004 |

Reading. Every protocol fault (500, garbage, truncation, wrong shape, reset, drop) is converted to a clean failed
run in a few milliseconds, with a message, in both systems, and a failed model call ends only that unit. No result
is ever silently wrong: a truncated body is rejected, not parsed as a short answer. The differences are all about
time: Objeck's HTTP client has an internal ~30 s read limit that cannot be changed and no total deadline (a
trickling server held one call for 152.6 s), so a stalled model costs 30 s per call; the hand-written baseline needs
an explicit timeout to be better than that, and without one it is strictly worse (hangs forever). Neither
implementation retries, so every injected model failure is a failed unit.

### Inference (4 workers sharing one session, even workers `Session->Run`, odd workers `BoundSession`)

| fault | system | n | bystanders unaffected | hung | died | silently wrong | notes |
|---|---|---|---|---|---|---|---|
| none | objeck cpu / cuda, python cpu | 25 each | 25/25 | 0 | 0 | 0 | control |
| bad_shape | objeck cpu | 25 | 25/25 | 0 | 0 | 0 | 3752/3752 injected calls refused, none crashed |
| bad_shape | objeck cuda | 25 | 25/25 | 0 | 0 | 0 | 3752/3752 refused |
| bad_shape | python cpu | 25 | 25/25 | 0 | 0 | 0 | 3752/3752 raised |
| close_inflight | objeck cpu | 25 | n/a | 0 | 0 | 0 | 7293 plain calls refused after the close, 0 wrong; bound workers unaffected |
| close_inflight | objeck cuda | 25 | n/a | 0 | 0 | 0 | 7446 refused, 0 wrong; GPU memory delta 0 MB |
| close_inflight | python cpu | 25 | n/a | 0 | 0 | 0 | no close primitive; reference dropped, 0 refused, 0 wrong |
| gpu_oom (256 MB cap) | objeck cuda | 10 | 10/10 | 0 | 0 | 0 | hog refused at block 31 of 8 MB; workers fine |
| oom_burst (14 GB address-space limit) | objeck cpu | 10 | **0/10** | 10/10 | **10/10** | 0 | aborts after ~12.8 GB: "Unable to allocate memory of size: 67108912" |
| oom_burst | python cpu | 10 | 10/10 | 0 | 0 | 0 | `MemoryError` after ~0.7 GB, caught |
| thread_fault | objeck cpu / cuda | 25 each | **0/25** | 25/25 | **25/25** | 0 | |
| thread_fault | python cpu | 25 | 25/25 | 0 | 0 | 0 | |
| thread_exit | objeck / python | 25 | 0/25 | 25/25 | 25/25 | 0 | tie |

Reading. This is where the runtime-owned design shows: the session registry makes a close that races with plain
runs, bound runs and live device tensors safe on both providers (0 wrong, 0 crashes across 50 runs and 14,739
refused calls), and the failure is a refusal the caller can count. A device allocation past the memory limit is a
refusal, not a crash. But that is a strength of the `Session` and `DeviceTensor` classes specifically, and
CPython's reference counting gives the Python baseline the same memory safety by a different route. The benefit
that Objeck adds is a defined, observable close semantic; the benefit is not that Python crashes. Where the
runtime does not own the failure (an allocation the VM cannot satisfy, a thread fault) it ends the process
and takes every worker and every in-flight result with it.

### Process-level faults

| scenario | system | n | result |
|---|---|---|---|
| sigkill, agents | objeck / python | 10 each | dies immediately, restart correct 10/10, cold start 0.07 s both |
| sigkill, inference cpu | objeck / python | 10 each | restart correct 10/10, 0.75 s vs 0.28 s |
| sigkill, inference cuda | objeck | 10 | GPU memory delta 0 MB 1.5 s after the kill, restart correct 10/10, cold start 5.3 s |
| sigterm (all of the above) | both | 10 each | killed at once by the default action, no drain, `graceful` 0/10 everywhere |
| stdin closed (empty, and after two lines) | objeck | 10 + 10 | EOF detected 0/20 (empty line returned 1000 times until the harness cap); both lines served |
| stdin closed | python | 10 + 10 | EOF detected 20/20 |
| disk full | objeck | 10 | 0 of 640 failed writes reported by `WriteBuffer`; all 480 units correct |
| disk full | python | 10 | 720 failures reported for 640 writes (flush raises, plus 8 closes per run); all 480 units correct |
| clock step +3600 / -3600 | objeck limited, python guarded | 10 each | detection and unit times identical to the unstepped runs |

### Overhead of the mitigations (fault-free, coarse, includes thread start)

Agents, scripted model: Objeck default 985 units/s vs Objeck with a 100 ms tool limit 970 (about 2%, within noise);
Python naive 970 vs Python guarded (a thread per tool call) 552, a 43% cost. With the loopback server: Objeck 386,
Python naive 557, Python guarded 386. Inference: Objeck CPU 1021 calls/s, Python CPU 2765 calls/s (Python's
marshalling is cheaper; this is the plain/bound mix at batch 2 and says nothing about large batches), Objeck CUDA
179 calls/s at 4 threads. Peak RSS columns are only meaningful for runs over about 0.5 s (the sampler runs every
10 ms but sub-second runs are sampled a handful of times).

## Findings for the maintainer (none fixed: each changes semantics or an API, so none is a small safe change)

Minimal repros are in `repros/`, each verified on this branch.

1. **`Console->ReadLine()` cannot signal end of input** (`eof_readline.obs`): it returns `""` forever, so a loop
   `while(line <> Nil)` never ends and a service whose stdin closes spins. `ReadBuffer` returns 0 at EOF and is a
   workaround. A fix means returning Nil from `StdInString` at EOF, which changes existing programs that test for `""`.
2. **`FileWriter` cannot report a failed write** (`disk_full.obs`): `fwrite` into stdio succeeds, ENOSPC surfaces at
   flush or close, and `Close`/`WriteString` return `Nil`. Result logs on a full disk are silently lost. Needs a
   status from `Close` or a `Flush` method.
3. **A local declared without an initializer in a loop body keeps the previous iteration's value**
   (`stale_local.obs`). I hit it in this harness: a failed bound call was counted as a wrong answer because the
   result variable still held the previous iteration's array (the harness now assigns `Nil` explicitly). It is a
   silent-wrong-value hazard in exactly the failure-handling code H6 cares about. It may be intended (declarations
   hoisted) but I could not find it documented.
4. **Any uncaught fault, or `Runtime->Exit`, in any thread ends the process** (`thread_fault_kills_process.obs`). This
   is the largest containment gap against the baseline. Whether it is wanted is a design decision.
5. **Allocation failure aborts the process** (exit 1, `Unable to allocate memory of size: N`), including from a
   non-main thread, with no error the program can catch.
6. **`HttpClient`: fixed ~30 s per-read timeout, no total deadline, no setter** (measured 30.5 s for `hold` and
   152.6 s for a slow-loris response). An agent against a stalled provider loses 30 s per call and cannot bound it.
7. **`API.Agent` has no cancellation and its tool timeout is advisory** (documented): measured 900 ms from a cancel
   request to the run stopping when the tool does not cooperate, unbounded for a hung tool.

No hang, deadlock or leak was found inside the ONNX session registry, the agent loop or the VM's thread handling
beyond the above: 0 hangs, 0 leaked processes (`leftover_procs` 0 in every row), GPU memory returned to its pre-run
level after every normal exit, SIGKILL and `thread_fault` crash.

## Threats to validity

- **One machine, shared and loaded** (load1 median 4.5, max 28). Pass/fail properties are insensitive; every
  millisecond figure is not. No repetition count above 25, and several cells have 8 or 10 (Wilson upper bounds of
  about 30% for 0/10). Zero events are not zero rates.
- **The Python baseline was written by the same author for this comparison.** It copies the loop semantics of
  `API.Agent` and is deliberately minimal; a framework (LangChain, an asyncio client with retries) or a hardened
  service would do better on hangs and worse on overhead. "Guarded" is one reasonable set of mitigations, not the
  best. The baseline uses threads, not asyncio; asyncio could cancel awaiting calls but not a blocked thread.
- **Not the same limit for `oom_burst`.** Both ran under `RLIMIT_AS` 14 GB, but the Python runtime reached it at
  0.7 GB (virtual mappings of onnxruntime) while Objeck reached 12.8 GB, so the two did not hit the failure under
  equal conditions. The qualitative difference (abort vs catchable error) holds; the numbers do not compare.
- **Different onnxruntime builds**: Objeck links 1.24.2 (CUDA), the Python baseline uses the 1.32.0 CPU wheel.
  CPU results compare the harness loop plus the native call; they say nothing about kernel speed.
- **Synthetic faults.** A scripted model, a tiny model, loopback networking, and faults injected at points I chose.
  The distribution of real faults is unknown. Run-level definitions (victims are agents 0 and 4; bystander stall
  = more than 3x the control's 95th percentile + 100 ms) are mine.
- **The agent loop is not runtime-owned.** `API.Agent` is library code on ordinary threads. H6 as worded ("sessions,
  tensors and agents tracked by the runtime") is therefore tested only for sessions and tensors; the agent half
  measures library composition on both sides.
- **Not injected**: a hung GPU kernel, a driver fault, true device exhaustion (a cap stands in so as not to disturb
  other users of the shared GPU), a worker killed from outside (Objeck has no thread kill), TLS-related clock
  effects (the clock step only tests timeouts, which use the monotonic clock, not certificate validity).
- **Harness fixes during the run**, all before the rows used: a local-variable bug (finding 3) in the first inference
  harness; the `hold` scenario was re-run with an 80 s deadline because the first 12 s deadline cut off the library's own
  30 s give-up; the Python inference baseline's exit path was changed to `os._exit` after one run in ten aborted
  during interpreter shutdown while a daemon thread was printing, and `oom_burst` was re-run; one Objeck
  `thread_exit` row was lost to a torn output line in the parser (fixed, re-run). Replaced rows are not in
  `results.csv`.
- **Resource columns** (threads, fds, RSS) are sampled from `/proc` and are too coarse to call a leak; the only
  leak statements made here rest on `leftover_procs`, `stuck` and GPU memory after exit.

## What was not run

- Python baseline on CUDA (no CUDA `onnxruntime` wheel installed) and a JVM baseline: `gpu_oom` and the CUDA rows have
  no comparison.
- Windows and ARM64.
- A real model server (only the loopback fake) and a real LLM.
- Long soak runs for leaks; a 30,000-run GPU soak already exists in #1768.
