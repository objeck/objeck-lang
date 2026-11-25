# Thesis design note: an AI-native managed runtime

Status: DRAFT for advisor review, 2026-11-24. Author: the student, drafted with Claude Code from the repository and pull-request text. Companion file: `docs/research/literature_review.md` (written in parallel on branch `docs/phd-literature-review`; this note does not repeat it and cites no paper it has not opened, see section 8).

Conventions used throughout. **Measured** means a number that appears in the cited PR body or repository document, taken on the machine named there. **Target** means a goal that nobody has measured. Every PR cited from #1737 to #1769 was still **open** (not merged into `master`) when this note was first drafted on 2026-11-24; by 2026-11-25 all of them are merged (checked with `gh pr view`), but the numbers quoted from them are still the PR authors' own, branch-level measurements, not re-run for this note. Section 5 lists what is on `master`; its status text was brought up to date on 2026-11-25. The one new measurement used here is the cross-runtime baseline of draft PR #1786 (`tools/research/baselines/RESULTS.md`), which is a single-machine, 9 to 10 repetition result on a shared box and is cited as such.

---

## 0. Origins: where the runtime comes from

Dates are author dates of commits on `master`, listed in the table at the end of this section. This section is history, not evidence for H1 to H6.

**Starting point.** Objeck began as a computer science master's project. The aim was a grid-based language that would make distributed computing easier for developers outside the Java ecosystem, at a time when SOAP web services were the usual way to connect machines.

**Supporting components.** The HTTP/S client, the XML and JSON parsers and the web server were the supporting components that matured the language toward distributed use. They belong to that goal and were not a detour from it. SOAP support was added in August 2011 and WSDL support in December 2011. Effort then moved to JSON, which won out over SOAP and WSDL; the first JSON support dates from August 2012. Service discovery, a program finding and calling another program's capabilities, did not catch on in that period. It has caught on with MCP, which is why an MCP library appears in the agent work of this thesis.

**Bottom-up design.** The language was built from the bottom. The instruction set, the JIT compiler and the runtime came first, and the compiler was rewritten to match them. The commit history is consistent with that order. Thread support (March 2010) was followed within days by concurrent garbage marking and concurrent JIT compilation. A JIT for IA-32 was ported to AMD64 later that month, and an x64 JIT followed in May 2010. A collector that stops all execution threads is dated May 2012. The working view is that the backend, meaning the JIT and the collector, is the hard part, and the compiler front end is a fraction of the work. This is why the spine of the thesis is a runtime-systems contribution and not a language-design one (section 2).

**Lessons that shaped the runtime.**

- *Unicode.* Work on Unicode strings started in June 2012 and was merged from a feature branch in April 2013. The difficulty was concrete: `wchar_t` is 16 bits on Windows and 32 bits on POSIX, and a character is not a byte. Fixes in this area are still arriving: #1769 (merged 2026-11-24) corrects `String->New(Byte[])` reading past the end of an exact-length array.
- *32-bit to 64-bit.* A 64-bit-first refactor of the JIT began in February 2018, and 32-bit support was dropped in May 2022.
- *Security.* TLS certificate validation was added in November 2016.
- *New targets.* An ARM JIT arrived in November 2019, a first machine-learning routine (linear regression) in December 2023, and ONNX model support in July 2025.

**How this leads to the thesis.** ONNX support puts blocking native inference calls inside a runtime whose collector stops every thread, which is the setting of H1 and H2: a collector-cooperative inference runtime. The original goal is revisited rather than dropped. Grid and HPC use, through the Alderaan cluster work (#1773, merged 2026-11-25), returns to distributed computing as a setting for the same runtime. Multi-node operation is a stretch result: no hypothesis depends on it.

| Commit | Date | Subject |
|---|---|---|
| `fc3639dbfc` | 2010-03-04 | wip - thread support |
| `641efef026` | 2010-03-12 | added code so that marking of garbage is concurrent (3 threads) |
| `53e4ff5999` | 2010-03-12 | support for concurrent jit compiling |
| `1cfa8a5521` | 2010-03-25 | porting JIT compiler from IA32 to AMD64 |
| `1ef7775d04` | 2010-05-06 | x64 JIT support |
| `9beefd62f6` | 2011-08-13 | soap support |
| `7f819a8a62` | 2011-12-20 | wsdl support |
| `83b98bf741` | 2012-05-22 | added code to stop all execution threads during garbage collection |
| `3f13c02a91` | 2012-06-24 | adding support for unicode strings... |
| `1e1182ad91` | 2012-08-08 | adding json support |
| `3009cbb205` | 2013-04-30 | merged changes from the unicode feature branch into main |
| `a3d31cee30` | 2016-11-19 | ssl connection now validates certs |
| `cd8b2bbad2` | 2018-02-01 | inital jit refactor working on posix 64-bit |
| `a37ef4f069` | 2019-11-26 | arm jit support |
| `07be454838` | 2022-05-06 | dropping 32-bit support and improving 64-bit performance |
| `f64bdc5521` | 2023-12-13 | ml linear regression |
| `fcd682fd42` | 2025-07-21 | adding onnx support |

---

## 1. Working title and research question

**Working title:** *A Managed Runtime for Concurrent Inference: Collector-Aware Accelerator Management, Resource-Aware Scheduling and Safer Language Constructs for AI Programs.*

**Research question as given:**
Can a managed language runtime that treats accelerator memory, inference sessions and agents as collector- and scheduler-visible resources deliver better tail latency, energy and reliability for concurrent inference workloads than library-based approaches, and which language constructs (shape/device-typed tensors, uncertainty-carrying values, capability types for agents, declarative budgets) make such programs safer to write?

**Refinement.** The question is two questions joined by "and", with three independent variables (latency, energy, reliability) and four candidate constructs. A committee will ask which part is the thesis. Proposed sharper form:

> *RQ.* When inference sessions, device-resident tensors and agent tool calls are first-class runtime resources, known to the garbage collector, the safepoint protocol and the scheduler, how much of the tail-latency and reliability cost that library-based serving stacks pay under contention is recoverable, and what is the smallest set of static constructs that prevents the remaining misuse?

Decomposed:

- **RQ1 (runtime).** Does collector- and safepoint-cooperative management of accelerator resources reduce stop-the-world interference and host-side overhead for co-running threads, compared with calling a native inference library from an unaware runtime? (H1, H2, H6)
- **RQ2 (scheduling).** Given the runtime's own knowledge of sessions, allocation pressure and collector phase, can it place and pace inference to meet declared latency and energy budgets under contention better than static placement? (H3)
- **RQ3 (language).** Which constructs catch a measurable share of real misuse at compile time, at acceptable annotation cost? (H4, H5)

Energy appears only in RQ2 and only as a measured secondary metric, because energy is where the thesis is least supported today (no instrumentation exists, section 5). If the advisor prefers a narrower thesis, RQ1 plus RQ3's agent-capability half is the most defensible core; RQ2 can shrink to "a policy built on the telemetry RQ1 needs".

---

## 2. Positioning

The spine is a **runtime-systems contribution**; the other two layers depend on it and are deliberately smaller.

| Layer | Contribution | Why it is a research claim | Main risk |
|---|---|---|---|
| 1. Runtime (spine) | GC- and safepoint-cooperative management of accelerator memory, inference sessions and blocking native calls in a managed VM | Managed runtimes pin, park and finalize native resources ad hoc. Here the collector, the allocator and the native-call boundary share one protocol, and the effect on other threads' tail latency is measurable | Engineering novelty only if the mechanism is "parking", which is a known technique; the contribution must be the resource-lifetime protocol and its measured effect |
| 2. Scheduling | Resource-aware placement and pacing of inference using telemetry the runtime already owns (collector phase, per-session queues, device memory, power) | The runtime sees what an external serving system cannot: allocation pressure, safepoint latency, collector pauses | Real-time DNN scheduling is a crowded area (GPU/DNN schedulers, predictable-latency serving, edge placement). Novelty must come from **integration with the managed runtime**, not from a better scheduling algorithm. A reviewer will compare against the best serving schedulers, not against FastAPI |
| 3. Language | A small set of constructs: shape/device-typed tensors, uncertainty-carrying values, capability types for agents, declarative budgets | Static checking of resources the runtime manages, so misuse becomes a compile error instead of a tail-latency or safety incident | Easy to over-build. Each construct needs its own evidence (H4, H5) or it is dropped |

**Deliberately not claimed:**

- *"AI writes the software."* Objeck's recent library growth was produced with heavy assistance from coding agents. That is a development-process fact, not a result, and nothing in the thesis depends on it. If the advisor wants it studied, it is a separate empirical software-engineering question with its own threats (contamination, who reviewed what), not part of this note.
- *"Yet another LLM agent framework."* `API.Agent` exists (section 5) and is a test subject for H5, not a contribution. The claim is about what a runtime and type system can enforce around an agent, not about agent capability, prompting or orchestration.
- *"Faster than Python."* Objeck calls the same ONNX Runtime as a Python program would. Wins, if any, come from what surrounds the native call.
- *A new inference engine or kernels.* Out of scope.

---

## 3. Hypotheses

Each hypothesis has a metric, a baseline, a falsification condition and an honest strength rating. "Preliminary" numbers are quoted from PR bodies and are single-machine, mostly single-run (the PR text itself notes the box was shared).

### H1. Parking blocking inference calls removes an Objeck-specific collector stall; relative to other runtimes it restores parity, not an advantage

*Claim.* Parking the calling thread around a blocking native inference call (so a stop-the-world collection need not wait for it) reduces the worst-case allocation stall of other threads in the same runtime, compared with the same runtime making the same call unparked. The claim is **not** that parked Objeck beats other managed runtimes: the baseline data (below) show it does not. Where parking leaves Objeck relative to the field is reported as a position, not as a win.

*Metric.* Longest wait and p99/p99.9 of per-round allocation latency of an allocating thread running beside an inference thread, over a fixed duration, repeated at least 30 times with confidence intervals.

*Baselines.* (a) the same native call unparked (the control inside the same VM); (b) the same workload on a JVM (G1, Parallel, ZGC), .NET (workstation and server, concurrent and not), Go (cgo), Python and a C++ control with no collector, to show whether the effect is specific to Objeck's stop-the-world design or general. A first pass of (a) and (b) exists in draft PR #1786 (below); (c) a Python process with a thread doing ORT inference beside a thread allocating (GIL behaviour makes this a different phenomenon; report, do not over-interpret).

*Preliminary (PR #1768, RTX 4090, WSL2, ORT-GPU 1.24.2, CUDA 12.6, ResNet-34, `bench_gc_stall.obs`, 10 s each).* Longest wait of an allocating thread, `Session->Run`, batch 32, `ep=cuda`: **176.7 ms to 19.9 ms** (8.9x). Batch 8, `ep=cpu`: **137.9 ms to 53.9 ms** (2.6x). p99 round: batch 32 cuda **4.5 ms to 4.6 ms** (no change); batch 8 cpu **88.8 ms to 10.9 ms** (8.1x).

*Cross-runtime baseline (draft PR #1786, `tools/research/baselines/RESULTS.md`; RTX 4090, WSL2, ResNet-34 batch 32, ORT 1.24.2, paced allocator thread, 60 s runs, 9 to 10 quiet repetitions on a shared machine; median worst allocator stall).* CUDA: C++ control 1.3 ms; Python 1.3 ms; Go 1.0 ms; Java ZGC 2.1 ms; .NET workstation concurrent 5.4 ms; **Objeck parked 5.6 ms**; .NET server concurrent 10.5 ms; .NET non-concurrent 30 to 33 ms; Java Parallel 48 ms; Java G1 81 ms; **Objeck unparked 19.6 ms** (range 4.3 to 54 ms, so the parked and unparked ranges overlap). CPU provider (5 repetitions): Objeck unparked 344 ms, parked 40 ms; the other managed hosts 24 to 123 ms. In the same runs the p99 and p99.9 of the allocator stall did not separate parked from unparked (CUDA p99.9 109 us parked, 92 us unparked); the difference is only in the worst case. Objeck's own longest collection pause fell from 157 ms to 5.7 ms (the pause is measured from the request to stop threads, so it includes the wait for the call).

*Candid reading of the baseline.* What parking buys, relative to the same runtime without it, is a worst-case stall 3.5x smaller on CUDA (19.6 to 5.6 ms, ranges overlapping) and 8.6x smaller on the CPU provider (344 to 40 ms, ranges not overlapping). Relative to the field, parked Objeck is **level with .NET's concurrent workstation collector, not ahead of it**; ZGC, Go, Python and the C++ control are lower, and G1, Parallel and the non-concurrent .NET collectors are higher. The other runtimes already treat a thread in a native call as stopped for collection purposes (JNI, P/Invoke, cgo), so none of them shows an inference-length stall; parking restores that behaviour to Objeck (this is the explanation given in #1786's write-up; it was not tested separately). The H1 result that can be claimed is therefore a defect removed, not a performance advantage. The PR #1768 figure below (176.7 to 19.9 ms) was **not reproduced** in size by this paced workload; it may still hold for that PR's as-fast-as-possible allocator, which has not been re-measured.

*Candid reading of the PR figures.* The 8.9x figure is a **maximum**, not a p99, and the cuda p99 did not move. As the hypothesis was first worded ("p99 allocation stall >= 5x") it is *not supported* on the GPU case, and the baseline above confirms that p99 and p99.9 do not move on CUDA; the worst-case reduction is supported on the CPU provider (8.6x) and smaller on CUDA (3.5x, overlapping ranges). The PR also states the residual ~20 ms is the collection itself (a major collection of large arrays), so the floor is the collector, which argues for a second step: shorter major pauses (or concurrent marking) is a different thesis. Wording to adopt: "reduces the worst-case allocation stall of co-running threads relative to the same runtime without parking; p99 and p99.9 are reported, not claimed; no claim of an advantage over other runtimes". *Strength: medium for the within-runtime effect, none for a comparative advantage.* The effect is real and mechanistic but the mechanism (parking) is known; the paper must show the protocol and its failure modes (what runs inside a parked scope, which objects may move), not the speedup alone.

*Falsified if* the parked/unparked ratio of the worst-case stall falls below 2x across the contention matrix (the first CUDA pass gives 3.5x with overlapping ranges, so this is open on the GPU and met on the CPU). A comparative claim (parked Objeck below the other runtimes) is **already falsified** by the first baseline pass: ZGC, Go and Python are lower and .NET's concurrent workstation collector is level.

### H2. Runtime-managed device-resident tensors remove host/device copy and allocation overhead

*Claim.* Keeping tensors in accelerator memory under runtime management (allocated once, reused, freed with the collector's knowledge) removes the per-call conversion, copy and allocation cost of a plain `Run`.

*Metric.* Median and p95 latency and images/s per batch size for three modes: plain run, bound with host copies, bound resident; plus host and device memory over a long soak.

*Baselines.* Plain `Session->Run`; **ONNX Runtime's own Python IO binding** (the essential baseline: if Python+ORT with IO binding matches Objeck bound/resident, H2 is an engineering parity result, not a research claim); Triton or ORT-server with shared CUDA memory where applicable.

*Preliminary (PR #1760, same machine, ResNet-34, `ep=cuda`).* Batch 32: plain run **48.92 ms** (p95 53.78, 654 images/s), bound with the same host traffic **10.14 ms** (3,155/s), resident **6.15 ms** (5,205/s). Batch 8: 14.74 / 4.22 / 3.03 ms. Batch 1: 4.94 / 3.15 / 2.78 ms. Bound results were bit-identical to `Session->Run` on CPU and CUDA. A 30,000-run soak showed flat GPU memory (stated in #1768 as covered by #1760).

*Cross-runtime check (draft PR #1786, same workload shape as H1).* Per-call inference p50 at batch 32 on CUDA: Objeck `Session->Run` 58.2 ms; Objeck `BoundSession` with host copies 9.4 ms; C++, Java, .NET and Go 7.2 to 7.3 ms (plain calls, no IO binding in those hosts); Python 12.4 ms with the default GIL switch interval. So most of what H2 removes is a conversion cost that the other bindings do not pay: bound Objeck closes the gap to those hosts (it does not pass them, 9.4 against 7.3 ms), and the H2 comparison against Python with ORT IO binding, the essential baseline, has not been run.

*Candid reading.* The plain-run overhead is dominated by converting Objeck arrays and by allocation, not only by PCIe copies (the `bound_copy` row moves the same bytes and is already 4.8x faster). So "removes the copy" is not the right explanation; an ablation is needed. The gain shrinks to 1.8x at batch 1. This is the **weakest hypothesis as a research claim**, because a Python user can obtain much of it with ORT IO binding. Its research value is as the substrate for H1 and H6 (lifetime and failure behaviour), not as a headline. *Strength: weak as a claim, necessary as infrastructure.*

*Falsified if* Python+ORT with IO binding is within 15% of Objeck bound/resident across batches and the Objeck advantage appears only against plain `Run`.

### H3. Profile-driven placement meets declared latency and energy budgets more often than static placement under contention

*Claim.* A policy that places sessions across devices (CPU, GPU, NPU where present) and paces requests from measured profiles meets a declared per-request deadline and energy budget in a higher fraction of requests than a fixed assignment, when N models or agents contend.

*Metric.* Deadline-hit rate; budget-violation rate; p99 latency; joules per inference; across N = 1, 2, 4, 8, 16 concurrent models with mixed batch sizes and arrival patterns (closed loop, Poisson, bursty).

*Baselines.* Static placement (everything on the GPU; everything on the CPU; hand-tuned partition); a stock serving stack with its default scheduler (Triton dynamic batching, Ray Serve); a published predictable-latency GPU scheduler as an upper bound (unverified, to be named from the literature review).

*Preliminary.* None. No telemetry, profile store or policy exists (section 5). Available only as indirect motivation: #1765 shows a 2.5x to 3x spread between provider/option settings (e.g. `cudnn_conv_algo_search=DEFAULT` 8.34 / 9.32 / 15.55 ms against 2.98 / 3.26 / 6.39 ms for the default at batch 1 / 8 / 32), and #1768's `bench_threads.obs` shows a shared session scaling differently per path (`Session->Run` batch 8: 612 / 1,108 / 1,867 / 2,222 images/s at 1 / 2 / 4 / 8 threads; bound: 2,750 / 2,958 / 3,147 / 2,881). Those show that configuration matters and that contention changes the right choice; they do not show a policy wins.

*Strength: weak until a baseline scheduler is chosen.* This is the crowded part of the map. To be a thesis claim the scheduler must use runtime-internal signals (collector phase, allocation rate, safepoint wait) that an external scheduler lacks, and the experiment must show those signals change decisions. If they do not, H3 collapses to "a competent profile-based scheduler works", which is not novel.

*Falsified if* a static partition tuned once on the same workload matches the policy's deadline-hit rate within noise, or a policy without runtime-internal signals matches one with them.

### H4. Shape- and device-typed tensors catch a measurable share of tensor-misuse bugs at compile time

*Claim.* A type system tracking tensor rank/shape (at least symbolic) and device placement rejects at compile time a measurable share of the shape, dtype and device-mismatch bugs that otherwise surface at run time.

*Metric.* Share of a bug corpus caught statically; false-rejection rate on correct programs; annotation burden (lines or tokens added); compile-time cost.

*Baselines.* Objeck without the types (run-time errors, which `API.Onnx` already refuses with explicit messages, e.g. wrong element count, -1 dimension, closed handle, per #1760); Python with mypy plus a shape-checking tool for tensor code (unverified, to be named); Rust-style typed-tensor libraries (unverified).

*Preliminary.* None. **A bug corpus does not exist.** Candidate sources: GitHub issues of ONNX Runtime/PyTorch-adjacent projects (shape and device mismatch), the student's own Objeck ML libraries' history, and the Objeck issue tracker. Building the corpus (with labelling rules and inter-rater agreement) is itself a deliverable.

*Strength: unknown, potentially low.* Most shapes in ONNX serving come from a model file at run time, so static shapes may cover only the code around the model (pre- and post-processing, batching), not the model I/O. Dynamic `-1` dimensions are common. If the corpus shows most bugs are in dynamic shapes, H4 should be reported as a negative or narrow result.

*Falsified if* fewer than 20% of corpus bugs are expressible in the type system, or the annotation burden exceeds a pre-registered bound (target: under 10% added lines in sample programs).

### H5. Capability types for agent tools prevent a measurable class of unsafe tool actions at low overhead

*Claim.* Making tool authority a type-level capability (a tool handler can only be built with, and can only exercise, the capabilities it declares; dangerous capabilities need a witness from an approver) prevents a defined class of unsafe actions that run-time allow/deny checks alone leave possible, with overhead under a stated bound.

*Metric.* Number of attack/misuse cases from a catalogue blocked statically vs dynamically vs not at all; per-call overhead; false rejection of legitimate programs.

*Baselines.* The current dynamic policy in `API.Agent`; the allow/deny and approval model of an established agent framework or of MCP (unverified, specify per-framework); prompt-level instructions only (the weakest and most common baseline).

*Preliminary (PR #1753, a run-time baseline, not a type result).* `API.Agent` gates every tool call by allow and deny list, validates arguments against the tool's JSON Schema before the handler runs, requires an `AgentApprover` for dangerous tools (fails closed with none), turns every failure (denied, unknown tool, invalid arguments, timeout, step limit, declined approval, raising handler) into an error result the model can read, masks API keys in errors and transcripts, and **refuses to send an API key while TLS certificate checking is off** (same rule in #1744, merged). The built-in file reader is confined to one directory. These are tested against a scripted backend (`agent_loop_test`, `agent_backend_test`, `agent_http_test`); nothing was run against live services. The VM trust model (`docs/SECURITY_MODEL.md`) states that `obr` is not a sandbox: a program can run shell commands and load native code. **So a capability type is only meaningful if the threat model is "a tool author or an LLM-directed call chain stays inside the library API", not "adversarial Objeck code"**; the note must say so (section 7).

*Strength: medium for the engineering, weak for a general safety claim.* A capability discipline that the type checker enforces for code compiled together is checkable and publishable (compare with object-capability languages; unverified, see literature review). Everything is bypassable by a native call unless the VM restricts it, which the current VM does not.

*Falsified if* every attack in the catalogue that the capability types block is already blocked by the run-time allow/deny list at equal cost (then types add only earlier error reporting).

### H6 (proposed addition). Runtime-managed resources improve reliability under failure

*Claim.* With sessions, tensors and agents tracked by the runtime, injected failures (device out-of-memory, a hung kernel, a crashed provider, a thread killed mid-call, a slow tool) produce bounded, recoverable errors instead of leaks, hangs or crashes.

*Metric.* Fraction of injected faults leaving the process healthy; leaked device memory after recovery; time to recover; whether other models' tail latency is affected.

*Baseline.* Same faults against Python+ORT and a JVM binding.

*Preliminary.* Only structural: a session closed while tensors or bindings are attached is released after the last of them (PR #1760, `session_registry.h`), and the VM has a stop-the-world watchdog that reports which threads a collection is waiting for (`OBJECK_STW_WATCHDOG_MS`, `core/vm/arch/memory.cpp`). There is no fault-injection harness. *Strength: untested, likely the cheapest way to turn a robust engineering result into evidence.*

**Summary of strength.** H1 medium for the within-runtime effect and none for a comparative advantage (mechanism proven; the baseline puts parked Objeck level with .NET concurrent workstation and behind ZGC and Go), H2 weak as a claim (it recovers a cost other bindings do not pay), H3 weak (no data, crowded field), H4 unknown (no corpus), H5 medium-weak (needs a threat model and the type work), H6 untested. The thesis must not depend on H3 or H4 alone.

---

## 4. Baselines, metrics and testbed

### 4.1 Baselines

| Purpose | Baseline | Notes |
|---|---|---|
| Single-model serving | Python + ONNX Runtime (CPU, CUDA, TensorRT EPs), with and without IO binding | Same model files, same ORT version, same batch sizes. ORT IO binding is the control for H2 |
| Serving stacks | FastAPI wrapper; Ray Serve; NVIDIA Triton | For end-to-end request latency under N concurrent clients. Configure each with its documented batching and report configuration |
| Managed-runtime behaviour | Java on a JVM (default collector, and a low-pause one) calling the same native library | For H1 and H6: separates "stop-the-world design" from "native calls are hard in any managed runtime". The ORT Java binding or a JNI/Panama shim; state which. A first pass over Java, .NET, Go, Python and C++ exists in draft PR #1786 |
| Agents | The same task with `API.Agent`, with a minimal hand-written loop, and with an established framework | For H5 only, on safety and overhead; not on task quality |
| Static placement | Everything-on-GPU, everything-on-CPU, hand-tuned split | H3 lower bounds |

### 4.2 Metrics

Latency P50/P95/P99/P99.9 and maximum, per model and per co-running thread; throughput; **allocation-stall p99 and longest wait** (the metric of `bench_gc_stall.obs`); collector pause by phase (`OBJECK_GC_STATS=1` already reports what a run did); GPU and host memory over a soak (flat memory over many thousands of runs is the leak test); **energy per inference** (NVML power counters on the RTX 4090; `powermetrics` on Apple Silicon; INA-class or `tegrastats` readings if an edge board is added; host RAPL where available; report which counters, their sampling rate and their known inaccuracy); contention scenarios with N concurrent models or agents; failure injection (H6); code size and annotation burden for the language constructs (H4, H5).

Statistics: warm-up discarded; at least 30 independent runs for a tail claim; report bootstrap confidence intervals. The repository already has a paired-bootstrap comparison in `llm_eval` (PR #1756, `compare.obs`), usable for paired latency comparisons across identical request streams. Single-run or shared-machine numbers (as in all current PR tables) must be replaced by quiet-machine, repeated runs before any paper.

### 4.3 Testbed

**Exists today (confirmed by repository history and PR text):**

- RTX 4090 24 GB desktop; Windows host and WSL2 Ubuntu 24.04; **the GPU measurements in #1760, #1761, #1765 and #1768 were taken on WSL2** (driver 621.24, ONNX Runtime GPU 1.24.2, CUDA 12.6.77, cuDNN 9.25.1, TensorRT 10.14.1). Those PRs state that the Windows host had no CUDA 12, cuDNN 9 or TensorRT 10 runtime installed at the time and that the Windows GPU path was **built but not run**. Whether Windows-native CUDA can be run should be settled before any claim covers "Windows".
- Apple M4 Max Mac (CoreML and CPU providers exist in the library; no GPU-inference numbers in the PRs). Energy via `powermetrics`.
- Windows ARM64 (Snapdragon X) machine (QNN provider exists in the library; no measurements yet).
- x64 CI (GitHub-hosted) and ARM64 CI; useful for correctness, **not for latency or energy** (shared, virtualized).

**To acquire or confirm with the advisor (not confirmed to exist):** an NVIDIA Jetson board; a Raspberry Pi with or without an accelerator; a Hailo accelerator; any INA-class power monitor. **Experiments are designed to stand without them:** H1, H2, H6 need only the RTX 4090 and CPU; H3 can be demonstrated on a CPU+GPU+NPU mix across the three machines at the cost of cross-machine comparability (so run policies per machine, compare policy vs baseline within a machine); H4, H5 need no accelerator. If an edge board is added, it strengthens H3 and the energy story and is the main reason to acquire one.

---

## 5. What exists and what is missing

### 5.1 In the repository now (on `master`)

- **Generational collector with safepoints.** 128 MB nursery plus old generation, write barrier, minor and major collections, all stop-the-world and cooperative (other threads park at safepoints: dispatch loop, allocation, blocking calls); arrays are allocated directly in the old generation; three parallel mark threads. `core/vm/arch/memory.{h,cpp}`, `docs/ARCHITECTURE_MAP.md` (GC section), `docs/GC_ROOTS_DESIGN.md`, `core/vm/arch/README.md`. A heap verifier (`OBJECK_GC_VERIFY`) and `OBJECK_GC_STATS`.
- **STW watchdog.** `OBJECK_STW_WATCHDOG_MS` makes a stuck stop-the-world wait report which threads it is waiting on (`core/vm/arch/memory.cpp`).
- **Native-library park API.** `APITools_Parked` in `core/vm/lib_api.h`: a native library parks its thread for the length of blocking work so a collection does not wait.
- **Affinity- and cgroup-aware CPU count.** `UsableCpuCount()` in `core/shared/cpu_count.h` (CPU affinity mask, cgroup v1/v2 quota, `OBJECK_CPU_COUNT` override); used for `runtime.cpu.count` and the CNN/ONNX thread pools. Relevant because a scheduler that over-subscribes cores is not a fair baseline comparison.
- **ONNX provider selection.** `ep` per session: `cpu`, `cuda`, `dml`, `qnn`, `vitisai`, `coreml`; a requested provider that is not loaded is refused, no silent fallback (`core/lib/onnx/eq/onnx.cpp`, `docs/ONNX_CUDA_PLAN.md`). The shipped Linux runtime is CPU-only; Windows x64 ships DirectML; no release ships CUDA.
- **Trust model.** `docs/SECURITY_MODEL.md`: `obr` is not a sandbox. API clients refuse an API key while TLS verification is off (#1744, merged).
- **Design background.** `docs/AI_FOUNDATION_DESIGN.md` (tensor class, native tensors and the GC), `docs/INFERENCE_ENGINES_PLAN.md`.

### 5.2 Merged pull requests (all on `master` as of 2026-11-25; the evidence quoted is still branch-level, from the PR text)

| Piece | PR | What it provides | Maps to |
|---|---|---|---|
| `DeviceTensor`, `BoundSession` (IO binding) | #1760 | Provider-resident tensors (`cuda`, `tensorrt`, host memory for `cpu`), reuse without per-call allocation, ORT-allocated output, a `session_registry.h` that defers closing a session until attached tensors and bindings are gone, 30,000-run flat-memory soak; `onnx_iobinding.obs` runs on CPU everywhere, `_gpu` variant skips without a GPU | H2, H6 |
| Exact fp16/bfloat16, 8-bit helpers, FP16/INT8 tooling | #1761 | All 65,536 half patterns round-tripped; model-conversion scripts; drift measurements (FP16 max abs 0.034, top-1 16/16; INT8 calibrated on random data is a speed fixture, top-1 5/16) | accuracy axis for H3 |
| `ep=tensorrt`, session options, CUDA graphs | #1765 | TensorRT provider, documented CUDA options, measured table (batch 32 resident: FP32 cuda 6.39 ms; FP32 tensorrt 5.17; fp16 tensorrt 1.88; fp16 + CUDA graph 1.40; INT8 QDQ + fp16 + graph 1.28 ms) | profile space for H3 |
| Parked `Session->Run`, shared sessions across threads | #1768 | Parking around plain runs and upload/download; `bench_gc_stall.obs`, `bench_threads.obs`; concurrency tests run under `--gc-threshold=64k`, `OBJECK_GC_VERIFY=1`, `--jit=off`. Its 176.7 to 19.9 ms claim was not reproduced by the cross-runtime baseline (H1) | H1 |
| `API.Agent` | #1753 | Provider-neutral tool-use loop: allow/deny lists, JSON-Schema argument validation, approval gate (fails closed), key redaction, observer, transcript | H5 baseline |
| `API.MCP` | #1757 | Model Context Protocol server, client and bridge, with a stdio interoperability test driven by an independent Python client | H5 surface |
| `llm_eval` | #1756 | Evaluation harness with 40-task suite, pass@k, paired-bootstrap comparison | statistics, agent evaluation |
| `llm_util` | #1758 | Prompt templates, token estimation, JSON Schema validation, structured-output calls with retry | agent substrate |
| RAG kit | #1754 | Chunker, in-memory vector index, embedder/generator interfaces, offline stub, adapters | workload for contention scenarios |
| Compiler/optimizer work (LICM and dead-store on the CFG) | #1737, #1738 | Not part of the thesis; relevant only as "the compiler can host analyses" | H4 feasibility |

Caveats stated in the PR text and repeated here: the GPU numbers are WSL2 only; the box was shared with other work (differences under about 10% are noise); the per-family sessions (`YoloSession`, `ResNetSession`, `Phi3Session`) still run inference **unparked**; hosted-provider adapters were never run against live services; the library was built with a native-write-barrier discipline (`tools/cicd/check_native_write_barrier.py`) that any new native resource type must follow.

### 5.3 Missing (nothing exists)

This list was written on 2026-11-24. Since then, pull requests merged into `master` include a telemetry library (#1772, which bears on item 1; the profile store and any energy instrumentation, items 2 and 9, are not claimed here), agent run control (#1785), HTTP client deadlines (#1782) and an opt-in VM mode that ends only the faulting thread on an uncaught error (`--thread-faults=isolate`, #1783; the default is unchanged). Draft PRs #1776 (a fault-injection harness, item 8 in part) and #1786 (a JVM, .NET, Go and Python comparison workload, item 11 in part) exist but are not merged. Items 1 to 11 below are otherwise left as first written and should be re-read against those before they are quoted.

1. **Telemetry.** No unified per-session/per-device counters (queue depth, collector phase seen by a request, device memory, power). Needed for H3 and for every energy claim.
2. **Profile store.** No persistent record of (model, provider, options, batch, device) to measured latency/energy.
3. **Placement and pacing policy.** No scheduler; sessions are placed by program text (`ep=...`).
4. **Declarative budgets.** No syntax or runtime object for "this call has 50 ms and 2 J".
5. **Tensor types.** `Tensor` is one class with dtype as a value (`AI_FOUNDATION_DESIGN.md`); shape and device are run-time facts.
6. **Uncertainty types.** No type carries confidence/calibration; nothing consumes one.
7. **Capability types.** Agent authority is enforced dynamically; the compiler knows nothing of it.
8. **Fault-injection harness** and a **quiet-machine benchmark protocol**; the existing numbers are single runs.
9. **Energy instrumentation** on any machine.
10. **A bug corpus** (H4) and an **attack catalogue** (H5).
11. **A JVM comparison workload.**

---

## 6. Phased plan

Quarters are counted from the start of dedicated thesis work (Q1 = the first quarter after advisor sign-off). The plan assumes one student part-time with Claude Code assistance for implementation, so implementation estimates are optimistic and measurement/writing are the bottleneck; add 30% slack to anything with a deadline.

| Phase | Quarters | Deliverables | Exit criterion |
|---|---|---|---|
| 0. Foundation | Q1 | Merge the open ONNX/agent PRs (done by 2026-11-25) or consciously drop them; quiet-machine benchmark protocol; fix the testbed (Windows CUDA runtime or declare WSL2 the platform); decide the JVM baseline | Reproducible H1/H2 runs with confidence intervals, scripted end to end |
| 1. Runtime paper | Q1-Q3 | Collector-aware resource protocol (what may run inside a parked scope, lifetime rules, finalization order), parking extended to per-family sessions, fault-injection harness (H6), H1/H2 evaluation with JVM and Python baselines | Draft submitted. Target venues: ISMM, VEE or MPLR; EuroSys/ATC as a poster or short paper; MLSys if the story is framed around serving |
| 2. Telemetry and profile store | Q3-Q4 | Counters, profile store, energy instrumentation on the 4090 (NVML) and Apple Silicon (`powermetrics`); decision whether to buy an edge board | A day of mixed-load traces collected and replayable |
| 3. Scheduling paper | Q4-Q6 | Placement/pacing policy using runtime-internal signals; N-model contention experiments; H3 against static and a published scheduler | Submitted (systems venue, or MLSys). Kill criterion: if runtime-internal signals do not change decisions, fold this phase into the runtime paper as a "case study" and move on |
| 4. Language constructs | Q6-Q9 | Bug corpus and attack catalogue first (Q6); capability types for agents (H5); device/shape types, narrow version (H4); declarative budgets as library types if full syntax is not justified; uncertainty types only if a consumer emerges | Submitted. Target venues: PLDI, OOPSLA or Onward! (type-system / design focus), ICSE-SEIP (corpus and engineering focus) |
| 5. Dissertation | Q9-Q12 | Integration chapter: one end-to-end application (for example concurrent vision and agent workloads on one node) run on the full system and on baselines; limitations; writing | Defence |

**How the papers combine.** Paper 1 is the spine and the thesis stands on it. Paper 2 (scheduling) and paper 3 (constructs) each reuse paper 1's runtime and telemetry. The dissertation argues one thesis: *runtime-visible resources enable both better dynamic behaviour (papers 1 and 2) and better static guarantees (paper 3)*. If paper 2 fails its kill criterion, the dissertation is paper 1 plus paper 3 with a shorter scheduling chapter; it is still coherent. If paper 3's corpus shows low static coverage, the negative result is reported and the agent-capability work (H5) carries the language half.

---

## 7. Threats to validity, expected reviewer objections, ethics

### 7.1 Threats to validity

- **Single machine, shared box.** All current GPU numbers come from one desktop, partly measured while it ran other work (load 11 to 14 in #1765). Remedy: quiet-machine protocol, repeated runs, second GPU if obtainable.
- **WSL2 is not bare metal.** Its clock behaviour and GPU path (paravirtualized) may affect tails. The student's earlier test flakes on this machine were traced to WSL2 clock steps (maintainer notes, not a repository document). Remedy: validate the headline H1 result on native Linux or native Windows CUDA.
- **Implementation maturity.** A research VM with one primary developer; JIT and collector bugs have occurred (the repository's history is open about it). A reviewer will ask whether differences reflect design or defects. Remedy: heap-verifier runs for every measured configuration, and report them.
- **Benchmark scope.** ResNet-34 only so far. Add a transformer encoder, a detector and a decoder with KV cache before generalizing.
- **Baseline fairness.** The Python and JVM baselines must be tuned with equal effort and use the same ORT build; the ORT Python IO binding in particular must be used. Report configuration.
- **Construct validity of energy.** NVML board power is coarse and lags; report sampling and calibrate against a wall-power meter if one is available.
- **Statistics.** The numbers above are medians of 40-60 runs; they say nothing about p99.9.
- **Compile-time claims depend on corpus quality** (H4, H5): selection bias, labelling by one person.

### 7.2 What reviewers will say, and answers

| Objection | Answer |
|---|---|
| "Parking threads around native calls is standard (JNI critical sections, safepoint-aware native transitions)." | True for the mechanism. The claim is the lifetime and resource protocol for device memory and sessions, and its measured effect under contention against a cross-runtime comparison. The first pass (draft PR #1786) already shows the other runtimes have no inference-length stall, so the paper frames parking as removing an Objeck-specific stall and reports where Objeck then sits (level with .NET concurrent workstation, behind ZGC and Go), not as an advantage |
| "Python with ORT IO binding already avoids the copies." | Agreed; H2 is infrastructure, not the contribution, and the paper says so |
| "Real-time DNN scheduling is well studied; why another scheduler?" | Not a new algorithm: the claim is that runtime-internal signals change decisions. Kill criterion in section 6 if they do not |
| "Why a new language rather than a library or an MLIR/compiler approach?" | The research needs a runtime we can modify (collector, safepoints, native-call boundary). Any other runtime (JVM, CLR, V8) would serve the runtime half; Objeck is chosen for access and for the value of one coherent VM. Stated as a limitation: external validity to production runtimes is argued, not shown, unless the H1 experiment is replicated in a JVM prototype |
| "Shape types exist in other languages and libraries." | See the literature review; the claim is narrow (device and shape types integrated with the runtime's resource model) and is tested on a corpus; a low yield is reported |
| "Agent safety by types is bypassable: the VM is not a sandbox." | Correct (`docs/SECURITY_MODEL.md`). The threat model is an LLM-directed call chain and tool authors inside the library API, not hostile Objeck code. A VM restriction on native loads and shell calls under a capability mode is future work and is called out |
| "Results come from the system's author." | Yes; mitigations are open scripts, an artifact package, pre-registered thresholds for H1 to H5, and, if possible, external reproduction by a second person |
| "Uncertainty-carrying values: where is the evidence?" | Currently none. They are dropped from the claims unless a consumer (a budget policy that trades accuracy for latency) is built in phase 3 or 4 |

### 7.3 Ethics and safety for agent capabilities

- Agents that act (files, shell, network, money) can cause harm through model error or prompt injection; the experiments must run in a sandbox (container or throwaway VM) with synthetic data, no real credentials and an outbound allow-list. Approval gates must fail closed (as `API.Agent` already does).
- Evaluate injection robustness only on locally authored documents and tools; do not publish working exploit prompts against named third-party products.
- Hosted-model experiments spend money and send data to providers: use synthetic inputs, record provider terms, and keep keys out of repositories, logs and transcripts (the redactor and the TLS-off refusal are the existing controls; their test coverage is the evidence).
- Energy and hardware claims should state the grid/measurement boundary and avoid overstated "green" language.
- A human-subjects study (for example programmers writing against the types) needs institutional review before recruiting; the corpus-based design of H4 avoids this by default.
- The note's own authorship: the code and this draft were produced with an AI coding assistant. The thesis should disclose that, and the student must be able to defend every claim without it.

---

## 8. Open questions for the advisor, and reading

### 8.1 Questions

1. **Scope.** Is "runtime paper plus agent-capability types" an acceptable minimal thesis, with scheduling (H3) optional and language constructs H4/uncertainty types dropped if evidence is thin?
2. **Platform.** May the thesis fix the GPU platform as Linux (WSL2 today, native Linux later) and treat Windows and Apple Silicon as portability evidence, rather than requiring Windows-native CUDA?
3. **Comparison runtime.** Is a JVM comparison sufficient for external validity, or will committee members expect a second managed runtime (CLR, V8) or a Python free-threaded build?
4. **Hardware.** Is there funding or lab access for an edge board (Jetson-class) and a wall-power meter? Which would the advisor prefer for an energy claim?
5. **Venue strategy.** Systems first (ISMM/VEE/MPLR) or serving first (MLSys)? Which is the advisor's strength for review?
6. **Ownership of Objeck.** The language has an existing maintainer and release process; which changes to the VM are acceptable to publish as research prototypes versus upstream, and is the project name under review for double-blind submissions?
7. **Evaluation depth for agents.** Is a catalogue-and-corpus evaluation of safety enough, or does the committee expect a user study?
8. **Energy.** Is energy a first-class claim (needs instrumentation phase) or a secondary metric reported opportunistically?
9. **AI-assisted development.** How should the thesis disclose and, if at all, study the use of coding agents in building the platform?

### 8.2 Reading

The survey lives in `docs/research/literature_review.md` (branch `docs/phd-literature-review`); read it for the related-work map and cite from it. This note cites no external paper by number. Systems named in passing (Triton, Ray Serve, ONNX Runtime, the JVM, TensorRT) are products, not citations; any published scheduler or type-system work referred to above as "unverified" must be taken from that review after its entries are checked against their sources. Repository reading, in order: `docs/ARCHITECTURE_MAP.md` (GC and safepoints), `docs/GC_ROOTS_DESIGN.md`, `core/vm/lib_api.h` (`APITools_Parked`), `docs/ai_sections/onnx_gpu.md` (added in #1760, #1761, #1765, #1768), `docs/SECURITY_MODEL.md`, `docs/AI_FOUNDATION_DESIGN.md`, `docs/ai_sections/agent.md` (#1753), and the bodies of PRs #1753, #1756, #1757, #1758, #1760, #1761, #1765, #1768.

---

## Appendix: data quoted from pull requests

Provenance for the numbers above; all on RTX 4090 24 GB, driver 621.24, WSL2 Ubuntu 24.04, ONNX Runtime GPU 1.24.2, CUDA 12.6.77, cuDNN 9.25.1, ResNet-34, shared box.

- #1760, `ep=cuda`, median (p95) ms and images/s. Batch 1: run 4.94 (7.26), 202/s; bound_copy 3.15 (5.21), 317/s; resident 2.78 (4.66), 359/s. Batch 8: 14.74 (18.23), 543/s; 4.22 (6.47), 1,894/s; 3.03 (6.43), 2,637/s. Batch 32: 48.92 (53.78), 654/s; 10.14 (10.96), 3,155/s; 6.15 (6.99), 5,205/s.
- #1768, `bench_gc_stall.obs`: longest wait before/after, p99 round before/after, as in H1; `bench_threads.obs` as in H3.
- Draft PR #1786 (the only measurement in this note not taken from a merged PR's text): `tools/research/baselines/RESULTS.md`, quoted in H1 and H2.
- #1765 (TensorRT 10.14.1, resident, median ms at batch 1 / 8 / 32): FP32 cuda 2.98 / 3.26 / 6.39; FP32 cuda graph 0.89 / 2.34 / 5.87; FP32 tensorrt 1.14 / 4.75 / 5.17; fp16 tensorrt 0.99 / 2.34 / 1.88; fp16 + graph 0.86 / 0.99 / 1.40; INT8 QDQ + fp16 + graph 0.57 / 0.71 / 1.28.
- #1761: FP32 run vs resident at batch 32: 44.64 vs 5.81 ms; FP16 resident 4.05 ms; INT8 QDQ resident 10.07 ms (a speed fixture; not a classifier).

No number in this note comes from a run made for the note.
