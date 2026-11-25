# Literature review: is an "AI-native managed runtime" a novel PhD contribution?

Compiled 2026-11-24. Scope: test the candidate thesis spine below against published work. This is a candid novelty check, not advocacy.

**Candidate spine.** A managed-language runtime that treats accelerator memory and inference sessions as collector-visible, scheduler-aware resources, so concurrent inference workloads (CNNs, small LMs, agents) meet latency, energy and memory budgets across CPU, GPU and NPU. A few language constructs sit on top: tensor types carrying shape/dtype/device, uncertainty/confidence-carrying values, capability/effect types for agents, declarative resource budgets.

**Verification legend** (used in every table and in the bibliography):

- **O** = I opened the primary page (arXiv abstract, USENIX page, vendor page) in this session and read title, authors, venue, year.
- **S** = confirmed only through a web-search result that listed the publisher, DOI, author or project page and its metadata; I did not open the paper itself. Treat the metadata as probably right but not checked line by line.
- **U** = could not be confirmed; listed under "Unverified leads" only.

Nothing is cited from memory alone. Search coverage is limited to what a general web search and page fetch could reach (ACM DL and several vendor pages returned 403 or were not indexed), so "I found no paper that does X" means "not found in this search", not "does not exist". The advisor should have a systematic search run in ACM DL, DBLP and Semantic Scholar before any novelty claim is written into a proposal.

---

## (a) Executive summary

**Plain verdict by part of the spine**

1. **"Treat accelerator memory and inference sessions as collector-visible, scheduler-aware resources" (the runtime core): partially covered in pieces, open as a combined, measured whole.**
   - Pieces that are well known: stop-the-world coordination around long native calls (Go syscall handoff, JNI critical regions and the GC locker, JEP 423 region pinning, .NET preemptive mode), off-heap device memory in JVM accelerator toolkits (TornadoVM, Babylon/HAT), finalizer pain freeing GPU memory from a collector (CUDA.jl issues, PyTorch GC-vs-OOM guidance).
   - What I did not find: a peer-reviewed study of how a managed runtime's collector and safepoint protocol interact with blocking accelerator inference calls and with accelerator-memory pressure, with a mechanism and tail-latency measurements. This is the only part of the spine where a gap looked real. It is also the part where the mechanism itself (parking a thread around a blocking call so the collector need not wait) is a known technique; the contribution would be the characterization, the accelerator-memory accounting and the end-to-end evidence, not the trick.
2. **"Scheduler-aware concurrent inference meeting latency budgets": heavily covered at the serving-system level, thin at the language-runtime level.** Clockwork, INFaaS, Nexus, Shepherd, AlpaServe, REEF, PipeSwitch, Salus, Orca, vLLM, Sarathi-Serve, and for mobile Band and CoDL already do SLO-aware scheduling, preemption and batching. They are separate serving systems or framework layers, none sits inside a language runtime's collector and thread scheduler. A reviewer will say "Python plus Clockwork/Triton does this". The defensible difference is single-process, in-runtime, allocation-heavy mixed workloads (an application that allocates and runs inference), not a serving cluster.
3. **"AI-native language" as a general pitch: crowded, and a direct competitor appeared eight days ago.** Jac/Jaseci ("Agent-Native by Design", arXiv 2611.21298, submitted 2026-11-16) claims the agent-native-language space for LLM calls, tools, workflow structure and speculative routing. It does not address accelerator memory, inference sessions, GC, tensor types, uncertainty types or energy/latency budgets (I read the first ~100k of ~121k characters; see Section c). Also crowded: LMQL, DSPy, SGLang, BAML (U), Mojo/MAX.
4. **Tensor types carrying shape/dtype/device: well covered by prior work.** Dex (typed index sets), Pythia/ECOOP 2020 static shape analysis, jaxtyping, Triton's statically-shaped tiles, an empirical fault study. A "device" component in the type is the only thin spot, and I could not verify Hasktorch's treatment (U). A new shape type system will not be novel; a shape/dtype/device check that is cheap, runtime-integrated and shown to remove a measured fault class might still be worth a small paper.
5. **Uncertainty/confidence-carrying values: well covered as a language idea (Uncertain<T>, probabilistic programming), open only in its link to scheduling.** Using confidence to decide escalation (small model, then large model, CPU to GPU) under a budget is what INFaaS and model cascades do at the system level. The language-level binding is not novel; a measured accuracy-per-joule or tail-latency benefit would be.
6. **Capability/effect types for agents: active and crowded in 2025-26.** CaMeL, Progent, "Design Patterns for Securing LLM Agents", "Towards Verifiably Safe Tool Use" (ICSE-NIER 2026). They are mostly libraries, policy languages or external interpreters, not a managed runtime with enforced effect types; Jac only has a compiler effect analysis. This is the part with the most competition and the least connection to the runtime thesis. Recommend dropping it from the spine or making it a side study.
7. **Declarative resource budgets: partially covered.** INFaaS (state latency/accuracy, system picks variant), Eon (energy states), EnerJ/Rely/Chisel (approximation and reliability in the type system), real-time Java. None combine budgets over inference latency, energy and accelerator memory in a collected-language runtime. Novelty depends entirely on showing budgets are enforced, not advisory.

**Bottom line.** Four of the five language constructs are individually well covered; they are not a thesis. The spine is only defensible if it is narrowed to the runtime-systems claim (item 1) with the language constructs demoted to the minimum needed to express it (probably a budget annotation and a device-resident tensor handle). Even then, a reviewer can fairly call it engineering unless the paper delivers a general lesson (a protocol, an accounting model, a characterization across runtimes) rather than "Objeck now does X". The author's preliminary numbers (Section d) are promising but are single-GPU, single-model and not yet compared against a Go/JVM/.NET/Python baseline, which is the comparison that decides whether the claim is novel.

---

## (b) Comparison table

Columns: what the system makes first-class, hardware, how (or whether) it touches runtime/GC, how it evaluates, source, verification status.

| System | First-class | Hardware | Runtime / GC integration | Evaluation metrics | Source | Ver. |
|---|---|---|---|---|---|---|
| Jac / Jaseci (MTP, Agent-Native) | `by llm()` typed model calls, object-spatial workflow, model-guided routing | LLM APIs (no accelerator model) | Python-superset compiler/runtime; no accelerator or GC story in the paper | LoC, builder tokens, LLM-judged quality, speculation latency | [arXiv 2611.21298](https://arxiv.org/abs/2611.21298), [2405.08965](https://arxiv.org/abs/2405.08965) | O |
| Mojo / MAX (Modular) | MLIR-based systems language, graph compiler, device hooks | NVIDIA, AMD, Qualcomm | Compiled language, not a collected runtime | Kernel performance vs CUDA/HIP (SC25 workshop paper) | [arXiv 2509.21039](https://arxiv.org/abs/2509.21039); [ModCon blog](https://modular.com/blog/modcon-announcements) | O (blog), S (paper) |
| LMQL | Constrained LLM decoding in a query language | GPU LLMs | Compiles to inference procedure, no GC/accelerator memory | Cost and accuracy | [arXiv 2212.06094](https://arxiv.org/abs/2212.06094) (PLDI'23) | O |
| DSPy | Declarative LM modules, metric-driven compiler | LLM APIs | Library | Task quality | [arXiv 2310.03714](https://arxiv.org/abs/2310.03714) | O |
| SGLang | LM program frontend + RadixAttention runtime | GPU | Runtime-owned KV cache | Throughput | [arXiv 2312.07104](https://arxiv.org/abs/2312.07104) | O |
| Uncertain<T> | Distribution-valued type, hypothesis-test conditionals | CPU | Library/language abstraction | Accuracy on sensor/ML apps | [MSR page](https://www.microsoft.com/en-us/research/publication/uncertaint-a-first-order-type-for-uncertain-data-2/) (ASPLOS'14) | S |
| Gen | Programmable-inference PPL | CPU/GPU (Julia) | Julia runtime | Inference speed/accuracy | [PLDI'19 page](https://pldi19.sigplan.org/details/pldi-2019-papers/45/Gen-A-General-Purpose-Probabilistic-Programming-System-with-Programmable-Inference) | S |
| Dex | Arrays as functions on typed index sets | CPU/GPU | Compiler | Performance and AD | [PACMPL 2021 / arXiv 2104.05372](https://arxiv.org/abs/2104.05372) | S |
| Futhark | Parallel array language, in-place updates | GPU | Compiler | Speed vs hand-tuned GPU code | [PLDI'17 PDF](https://www.futhark-lang.org/publications/pldi17.pdf) | S |
| Triton (Tillet et al.) | Statically shaped tiles | GPU | Compiler | Kernel speed | [MAPL'19 PDF](https://www.eecs.harvard.edu/~htk/publication/2019-mapl-tillet-kung-cox.pdf) | S |
| TVM | Graph + operator compiler, learned cost model | CPU/GPU/accel | Compiler | Speed vs vendor libs | [arXiv 1802.04799](https://arxiv.org/abs/1802.04799) | O |
| MLIR | Multi-level IR framework | Any | Compiler infrastructure | Use cases | [arXiv 2002.11054](https://arxiv.org/abs/2002.11054) | O |
| PyTorch 2 (torch.compile) | Dynamic bytecode capture, Inductor | GPU/CPU | CPython frame hook | Speedup on A100 | [ASPLOS'24 PDF](https://pytorch.org/assets/pytorch2-2.pdf) | S |
| Clockwork | Predictable per-inference latency, central scheduler | GPU cluster | None (serving system) | SLO attainment, p99.997 | [OSDI'20](https://www.usenix.org/conference/osdi20/presentation/gujarati) | O |
| INFaaS | Declared latency/accuracy, auto variant choice | CPU/GPU/accel | None | Throughput, SLO violations, cost | [ATC'21](https://www.usenix.org/conference/atc21/presentation/romero) | O |
| Nexus | Squishy bin-packing, coscheduling | GPU cluster | None | SLO violation %, utilization | [MSR page](https://www.microsoft.com/en-us/research/publication/nexus-a-gpu-cluster-engine-for-accelerating-dnn-based-video-analysis/) (SOSP'19) | S |
| Shepherd | Planning + preemptive serving | GPU | None | Goodput | [NSDI'23](https://www.usenix.org/conference/nsdi23/presentation/zhang-hong) | O |
| AlpaServe | Model parallelism for statistical multiplexing | GPU cluster | None | SLO attainment | [arXiv 2302.11665](https://arxiv.org/abs/2302.11665) (OSDI'23) | O |
| Orca | Iteration-level scheduling | GPU | None | Throughput at latency | [OSDI'22](https://www.usenix.org/conference/osdi22/presentation/yu) | O |
| vLLM / PagedAttention | Paged KV-cache memory manager | GPU | Runtime-owned allocator | Throughput | [arXiv 2309.06180](https://arxiv.org/abs/2309.06180) (SOSP'23) | O |
| Sarathi-Serve | Chunked prefill, stall-free batches | GPU | None | Capacity under tail-latency SLO | [arXiv 2403.02310](https://arxiv.org/abs/2403.02310) | O |
| Autellix | Program-level scheduling of LLM agent calls | GPU | None | Throughput at latency | [arXiv 2502.13965](https://arxiv.org/abs/2502.13965) | O |
| REEF | Microsecond kernel preemption (reset) | GPU (AMD, restricted NVIDIA) | None | RT latency, throughput | [OSDI'22](https://www.usenix.org/conference/osdi22/presentation/han) | O |
| PipeSwitch | Pipelined GPU context switch, unified memory mgmt | GPU | PyTorch integration | Switch overhead | [OSDI'20](https://www.usenix.org/conference/osdi20/presentation/bai) | O |
| Salus | Fast job switching, memory sharing | GPU | TensorFlow integration | Utilization | [arXiv 1902.04610](https://arxiv.org/abs/1902.04610) | O |
| Band | Multi-DNN scheduling over mobile processors | CPU/GPU/DSP/NPU | None | Latency SLO satisfaction | [project page](https://jeonchangmin.github.io/project/band/) (MobiSys'22) | S |
| CoDL | CPU-GPU co-execution on mobile | CPU/GPU | None | Latency, energy | [MSR page](https://www.microsoft.com/en-us/research/?p=959106) (MobiSys'22) | S |
| llm.npu | NPU prefill with out-of-order CPU/GPU/NPU scheduler | Mobile NPU | None | Prefill tokens/s, energy | [arXiv 2407.05858](https://arxiv.org/abs/2407.05858) (ASPLOS'25) | S |
| TornadoVM | Java to OpenCL/PTX/SPIR-V offload | GPU/FPGA | Offload from JVM, device memory outside GC | Speedup | [VEE'19 preprint](https://jjfumero.github.io/files/VEE2019_Fumero_Preprint.pdf) | S |
| Babylon / HAT | Code reflection, GPU kernels from Java | CUDA/OpenCL | Off-heap via Panama MemorySegment | Case studies | [openjdk.org/projects/babylon](https://openjdk.org/projects/babylon) | S |
| CaMeL | Control/data-flow extraction, capabilities | LLM | Custom interpreter | AgentDojo, provable security | [arXiv 2503.18813](https://arxiv.org/abs/2503.18813) | O |
| Progent | Deterministic privilege policies over tool calls | LLM | Policy layer | AgentDojo, ASB attack success | [arXiv 2504.11703](https://arxiv.org/abs/2504.11703) | O |
| EnerJ | Approximate/precise type qualifiers | Approx. hardware | Type system | Energy savings (simulated) | [PLDI'11 PDF](https://ready.cs.washington.edu/~luisceze//publications/Enerj-pldi2011.pdf) | S |
| Eon | Energy-state flows | Sensor nodes | Runtime | Study + deployment | [SenSys'07 PDF](https://www.cics.umass.edu/~emery/pubs/Eon-SenSys2007.pdf) | S |
| Rely / Chisel | Quantitative reliability specs | Unreliable hardware | Static analysis | Verified reliability | [Rely page](https://mcarbin.github.io/rely/) | S |
| Objeck (this work) | Resource-aware inference runtime (proposed) | CPU/GPU (CUDA, TensorRT, DirectML, CoreML) | Own VM, generational GC, parking around blocking calls | See Section d | PRs #1760, #1768 (merged; author's data) | author's data |

---

## (c) Per-topic sections

### C1. AI-native / LLM-oriented languages and runtimes

**Check of the three claims in the author's notes.**

- **Jac paper dated 2026-11-16: real.** "Agent-Native by Design: A Programming Language for Agentic Applications", Dantanarayana, Kashmira, Ma, Prabhashana, Tang, Mars; arXiv:2611.21298 v1, submitted 16 Nov 2026; cs.PL/cs.AI/cs.SE; no venue listed [O]. (The arXiv listing and an Abstract-page fetch both agreed.) Two earlier items from the same group are relevant: MTP (OOPSLA 2025, arXiv 2405.08965 [O]) and "Prompt Less, Smile More" (arXiv 2511.19427, v2 revised 2026-10-30 [O]). A Jaseci tutorial was given at ASPLOS'26 on 22 March 2026 [O].
- **"Agent-Native by Design": real, the same paper as above.** The title is exactly that. Note that "agent-native" is also a widely used practitioner term (Every's "agent-native architectures" guide and several blogs turn up first in a search), so a reviewer searching the phrase may find software-architecture essays rather than the Jac paper.
- **Modular open-sourcing MAX components in October 2026: substantially real, with a wording caveat.** Search coverage (Phoronix and several secondary write-ups) reports that MAX 26.7 on 22 Oct 2026 made the MAX graph compiler, runtime and device-extensibility hooks public, but under a new "MAX License" described as source-available rather than open source, with restrictions on commercial hosted inference services, AI-assisted reimplementation and distributing modified source [S; the Phoronix page returned 403 to my fetch and I did not read Modular's own announcement]. Separately, the Modular ModCon blog dated 18 Aug 2026 [O] states Mojo 1.0 was reached the week before, the whole Mojo language (compiler and tooling) was open-sourced under Apache 2.0, Modular joined Qualcomm, and MAX "will become source-available". So the October event is a follow-through on an August announcement, and "open-sourcing" overstates it.

**What each system makes first-class** (strongest first).

1. **Jac/Jaseci.** Agent-Native Programming makes model calls (`by llm()`), workflow structure and state (object-spatial programming) and model-guided routing (`visit ... by llm()`) language constructs that share one execution model. Evaluation: five benchmarks and five baselines (OpenAI SDK, LangGraph, DSPy, CrewAI, NOOA), reporting AI-integration lines of code (1.8x to 8.7x less than the SDK), builder tokens, LLM-judged quality, and 29-39% median routing-latency savings from speculation. It has a compiler effect analysis (read/write sets, external interactions, model requests) used to justify speculation and to restrict tools, but that is analysis, not an exposed effect or capability type system, and relies on "trusted declarations". It does not mention accelerators, inference sessions, GC, tensor types, uncertainty types or energy [O, 100k of 121k characters read]. Implication: it is the nearest competitor on "language for agents", and the cleanest contrast is that it treats the model as a remote service while the thesis here treats local inference as a runtime-managed resource.
2. **LMQL** (PLDI'23). Language Model Programming: constraints and scripting over decoding, compiled to reduce model calls (26-85% cost reduction reported) [O].
3. **SGLang.** Frontend language plus a runtime with RadixAttention KV reuse; up to 6.4x throughput reported [O]. The most runtime-integrated of the LM languages, but the runtime is a GPU serving engine, not a collected language runtime.
4. **DSPy.** Declarative LM modules compiled against a metric [O]. Library-level, no resource model.
5. **Mojo/MAX.** First language fully built on MLIR; the SC'25 workshop paper reports competitive memory-bound kernel performance against CUDA/HIP and gaps on atomics and fast-math compute-bound kernels [S]. Systems language with no tracing collector; relevant as a contrast (ownership, not GC) and as a competitor for "Python-like language for accelerators".
6. **Compiler stacks: TVM [O], MLIR [O], Triton (MAPL'19) [S], Futhark (PLDI'17) [S], PyTorch 2/torch.compile (ASPLOS'24) [S].** These make tensor programs, tiles or graphs first-class and optimize them. None is about runtime resource arbitration among concurrent inference sessions or collector interaction. The Jac group's own GraphMend (arXiv 2509.16248, graph breaks in PyTorch 2) is listed on the tutorial page [O] but I did not open it.
7. **Probabilistic languages: Uncertain<T> (ASPLOS'14) [S], Gen (PLDI'19) [S].** Uncertain<T> is the closest idea to "confidence-carrying values": a distribution-valued type with Bayesian-network semantics and hypothesis-test conditionals, implemented in C# with C++/Python prototypes.
8. **Not verified in this pass (U):** BAML, Pyro, Stan, Taichi, Halide, JAX/XLA, IREE, Dex's NeurIPS-workshop precursor details.

### C2. Real-time DNN inference serving, scheduling, GPU sharing, edge

Strongest papers:

- **Clockwork (OSDI'20).** Observes that individual DNN inference is nearly deterministic, builds a distributed system whose scheduler exploits that to meet 100 ms targets for 99.997% of requests on production-trace workloads [O]. The most important methodological precedent for "predictable tail latency" claims; the thesis should cite it as the standard to be compared against, and note that its predictability argument depends on controlling the whole execution path, which a runtime with a collector breaks unless the collector is made scheduler-aware.
- **INFaaS (ATC'21).** Users declare latency and accuracy requirements; system picks model variant and hardware. 1.3x throughput, 1.6x fewer violations on AWS [O]. This is the strongest prior art for "declarative budgets", at system level.
- **Nexus (SOSP'19) [S] and Shepherd (NSDI'23) [O].** Cluster-level scheduling with batching and (Shepherd) preemption for goodput under unpredictable load.
- **AlpaServe (OSDI'23) [O]; Orca (OSDI'22) [O]; vLLM (SOSP'23) [O]; Sarathi-Serve [O]; Autellix [O].** LM-era scheduling: iteration-level scheduling, paged KV memory (the closest conceptual neighbor to "accelerator memory as a managed resource", but managed by the serving engine, not by a language runtime), chunked prefill for tail latency, program-level scheduling of agent calls (Autellix: 4-15x throughput at the same latency versus vLLM).
- **GPU sharing: REEF (OSDI'22) [O], PipeSwitch (OSDI'20) [O], Salus (arXiv 2019) [O].** Kernel preemption by reset, pipelined context switching with unified memory management, and fine-grained memory sharing. MIG/MPS characterizations exist [S: arXiv 2209.06018, 2207.11428, 2301.00407, 2604.22430]. Important for the thesis: these show GPU-side contention is a first-order effect, so the author's parking result (a CPU-side mutator wait) must be shown to be separate from, and additive to, GPU-side interference.
- **Edge/mobile: Band (MobiSys'22) [S], CoDL (MobiSys'22) [S], HiTDL (IEEE TPDS 2022) [S], llm.npu (ASPLOS'25) [S].** Multi-DNN scheduling across CPU/GPU/NPU with fallback operators and online scheduling. These are the direct precedent for "across CPU, GPU and NPU", and they are in the TFLite/Android stack, not a general-purpose managed runtime. Correction to the brief: "DeepRT ECRTS 2018" did not resolve. The GPU soft-real-time scheduler named DeepRT is Yang, Nahrstedt, Guo, Zhou, SEC 2021 (arXiv 2105.01803) [S]; a different 2018 "DeepRT" by Kang and Chung is a journal paper (DOI 10.1007/s11241-018-9314-y) [S].
- **Energy-aware inference.** Zeus (NSDI'23) [O] is training-only; the inference-side literature surfaced is LLM-centric and recent (DynamoLLM, GreenLLM arXiv 2508.16449, PowerSlider, a 2026 MIG energy scheduler arXiv 2606.25082) [S, search listings only]. I found no dedicated study of power capping for classic CNN inference under latency SLOs, which is mild evidence of a gap, but my search was shallow here.
- **Triton Inference Server, TensorRT-LLM, Ray Serve.** Documented industrial baselines: instance groups plus dynamic batching [S, NVIDIA docs]; TensorRT-LLM in-flight batching via the Batch Manager [S]. Ray Serve scheduler documentation not retrieved (U).

### C3. Managed runtimes and accelerators (the closest-to-thesis area; thinnest literature)

- **JVM accelerators.** TornadoVM (VEE'19) offloads Java to OpenCL/PTX/SPIR-V and manages device memory between the Java heap and accelerator [S; I could not read the VEE paper's GC discussion, the preprint link redirected]. Project Babylon's HAT keeps data off-heap through Panama memory segments [S, from search summaries; the OpenJDK page itself returned 403]. Both treat accelerator memory as external to the collector. Neither is evaluated for collector interaction or latency tails of concurrent inference.
- **Safepoints and long native calls (the mechanism behind the author's parking result).** (1) JNI critical regions disabled GC in G1 until JEP 423 (JDK 22) replaced that with region pinning because stalls hurt latency and could cause spurious OOMs [S, search summaries of the JEP and JDK bugs; the JEP page returned 403]. (2) Go's runtime hands a processor away from a goroutine blocked in a syscall via sysmon retake (threshold discussed at about 10 ms in a Go review; value not verified), with open issues about cgo return (golang/go #57103, #36273) [S]. (3) .NET's preemptive mode is the same idea (U, not looked up here). These are the strongest evidence that the parking technique is established practice, so the novelty lies elsewhere.
- **GPU memory vs collectors in practice.** CUDA.jl issues #3310 and PR #3318 document finalizers that block on running kernels and freeing during graph capture [S]; PyTorch's own memory guide recommends explicit collection to avoid OOM caused by reference cycles holding GPU tensors [S]; cuDF issue #339 shows late release of device memory [S]. These are bug reports and tutorials, not studies, and they are the strongest argument that a real problem exists and has no systematic treatment in the literature I could reach.
- **GC on GPUs / GC cooperation.** The one hit, "Trash Talk" (arXiv 2012.06281), is about running the collector on integrated GPUs, which is the opposite problem [S, not opened].
- **Julia/CUDA.jl** (TPDS 2019, arXiv 1712.03112) is the best published account of a managed language targeting NVIDIA GPUs, focused on compilation rather than memory arbitration [S].
- **IBM GPU-enabled JVM, Aparapi, ILGPU, Go GPU bindings: not verified (U).**
- **Unified memory and GC implications: no source found (open lead).**

Net: on the specific claim "collector-visible accelerator memory with scheduler-aware blocking inference calls", I found evidence of the problem (issues, JEPs, docs), prior mechanisms (pinning, handoff) and adjacent systems (TornadoVM, HAT), but no paper that studies and measures the combined design. That is a gap in my search results, not a proof of absence.

### C4. Language constructs

- **Shape-typed tensors.** Dex (PACMPL 2021) makes arrays functions on typed index sets [S]. Pythia (ECOOP 2020) statically analyzes TensorFlow shapes, catching 11 of 14 shape bugs from a prior empirical study with no false positives, and notes most of the rest need dynamic information [S]. jaxtyping gives runtime and trace-time shape/dtype annotations (v0.3.9, Feb 2026) [S]. Wu, Shen, Chen (arXiv 2106.02887) built SFData, 146 crashing shape-fault programs in four categories, calling shape faults the most common fault from library misuse [O]. The title "What is wrong with my model?" in the brief did not resolve; I could not locate an ICSE paper by that name (U). Verdict: crowded. Hasktorch, torchtyping not verified (U).
- **Uncertainty values.** Uncertain<T> [S] and PPLs; see C1. Open angle: linking confidence to scheduling and escalation.
- **Effects, capabilities, tool permissions.** CaMeL (77% of AgentDojo tasks solved with provable security vs 84% undefended) [O]; "Design Patterns for Securing LLM Agents against Prompt Injections" [O]; Progent (privilege policies, SMT-checked narrowing vs expansion; v3 May 2026) [O]; "Towards Verifiably Safe Tool Use for LLM Agents" (ICSE-NIER 2026, capability-enhanced MCP labels) [O]. All four are policy or architecture contributions outside a language's type system; CaMeL is closest to a language-level treatment (a custom interpreter with capabilities). Jac has compiler effect analysis only.
- **Budgets/QoS/energy as language features.** EnerJ (PLDI'11, type qualifiers for approximate data) [S]; Eon (SenSys'07, abstract energy states) [S]; Rely (OOPSLA'13) and Chisel (OOPSLA'14) [S]. Real-time Java and QoS-aware languages: not retrieved (U).
- **Structured concurrency for agents.** I found no peer-reviewed language-runtime paper; the closest is an arXiv preprint "Stop Means Stop" (arXiv 2607.14166) on cancellation enforcement in agent frameworks, which notes structured-concurrency scopes make cancellation lexical [S, from a search summary]. Practitioner posts exist. This is a thin literature, but also the area where "Objeck already has TaskScope" is least likely to be a research contribution by itself.

### C5. Evaluation methodology

- **Latency tails.** The standard is percentile reporting (p99, p99.9), as in Clockwork (SLO attainment to 99.997%) [O], Sarathi-Serve (capacity under a tail-latency SLO) [O] and the tail-tolerance argument of "The Tail at Scale" (CACM 2013) [S]. A "max wait of an allocating thread" metric (the author's) is unusual and must be justified as a proxy for tail latency, and reported as distributions over many runs, not a single max.
- **Benchmarks.** MLPerf Inference (ISCA'20) [O] defines scenarios (single-stream, server, offline) and latency-bounded throughput; MLPerf Tiny/Edge not verified (U). Serving papers use production traces (Clockwork, Shepherd, AlpaServe) [O]. Agent work uses AgentDojo, ASB, SWE-bench [O via abstracts]. There is no standard benchmark for "application that allocates and runs inference concurrently", so the thesis must define one and release it.
- **Energy.** Zeus uses GPU energy measurement and power-cap tuning [O]; I did not verify the exact NVML counters, tegrastats, powermetrics or INA sensor practices in these papers (U). NVML exposes clock locking and power capping as the main levers (search summary, [S]).
- **Contention.** MIG/MPS comparisons [S] and REEF/PipeSwitch co-location experiments [O] are the models to follow: vary co-located load, report interference.
- **Venues** (as evidenced by the papers above): OSDI/SOSP/NSDI/ATC for serving and GPU sharing; ASPLOS and MobiSys for mobile and NPU; PLDI/OOPSLA for language constructs (LMQL PLDI'23, MTP OOPSLA'25, Gen PLDI'19, EnerJ PLDI'11, Rely OOPSLA'13); VEE and MPLR for managed-runtime acceleration (TornadoVM VEE'19); ICSE-NIER for agent safety (2026). ISMM, EuroSys, MLSys, Onward!, RTSS, EMSOFT: not checked.

---

## (d) Where the gap is

### Narrowest defensible research questions

**RQ1 (runtime core, most defensible).** In a managed runtime with a tracing collector, what is the tail-latency cost that blocking accelerator inference calls impose on allocating threads, and does a collector-aware protocol (parking/handoff around blocking calls plus accounting of device-resident buffers against collector pacing) remove it without costing inference throughput?

**RQ2.** Can accelerator memory held by session-owned buffers be made collector-visible (accounted, with finalization and pressure signals) so that device out-of-memory failures and collection-triggered stalls fall, compared with explicit manual release and with finalizer-based release as in Python/Julia?

**RQ3.** For concurrent inference sessions inside one process, does a runtime-level budget (latency target, memory cap, optional energy cap) enforced by the runtime's thread scheduler meet targets more often than an external server (Triton instance groups, Clockwork-style scheduler) or ad hoc threads?

**RQ4 (optional, secondary).** Does a minimal construct (budget annotation plus device-resident tensor handle) reduce measured faults or code size relative to library-level use?

Drop or defer: new shape type system, uncertainty type, agent capability types. They are individually prior-art-heavy.

### Measurable hypotheses

- **H1.** With parking, the 99.9th-percentile pause (or max wait) of allocating threads beside a batch-32 ResNet-34 run is at least 5x lower than without, with inference throughput change within 2%. Author's preliminary data (PR #1768, merged): longest wait 176.7 ms to 19.9 ms (8.9x). Needs repetition, distributions and a second model. Update 2026-11-25: a first cross-runtime baseline (draft PR #1786) did not reproduce that size with a paced allocator (worst stall 19.6 ms unparked to 5.6 ms parked on CUDA, 344 to 40 ms on the CPU provider) and puts parked Objeck level with .NET's concurrent workstation collector, behind ZGC (2.1 ms), Go (1.0 ms) and Python (1.3 ms); parking is a within-runtime improvement here, not an advantage over other runtimes.
- **H2.** IO binding with resident data reduces per-batch latency by at least 4x on ResNet-34 batch 32. Preliminary (PR #1760, merged): 48.9 ms to 10.1 ms (4.8x), 6.1 ms with resident data (8.0x). In the #1786 baseline, Objeck's bound call with host copies is 9.4 ms against 7.2 to 7.3 ms for C++, Java, .NET and Go, so most of the gain recovers a conversion cost the other bindings do not pay. Caveat: this is a property of ONNX Runtime IO binding, whose documentation states unbound runs copy inputs and outputs on each call and that these copies can dominate run time [O]. It is an engineering result, not a research claim; it is evidence that the runtime exposes the option and that accounting for resident data matters.
- **H3.** TensorRT fp16 plus CUDA graph reached 22,878 images/s versus 5,010 on plain CUDA at batch 32 (RTX 4090, ONNX Runtime GPU 1.24.2; author's measurement). This is an ONNX Runtime/TensorRT property, not a language-runtime result; use it only to show the runtime can drive state-of-the-art backends.
- **H4.** Under N concurrent sessions with mixed priorities, a runtime-level budget scheduler achieves at least 20% higher SLO attainment than unscheduled threads at equal throughput (choose the 20% after a pilot; do not promise it).
- **H5 (energy).** Joules per inference under a power cap or a budget policy are at least X% lower at equal p99 latency; X to be set from pilot data.

### Baselines and metrics reviewers will expect

- **Baselines for RQ1/RQ2:** the same ONNX Runtime workload driven from (a) Python threads (GIL, CPython GC, PyTorch caching allocator), (b) a JVM host (ORT Java, G1/ZGC, with JEP 423 pinning), (c) .NET (ORT C#), (d) Go via cgo (syscall handoff), (e) C++ threads with no collector. Without this, "parking cuts the wait" is not distinguishable from "any managed runtime can do this". This was the single most important missing experiment; a first pass over Java, .NET, Go, Python and C++ now exists as draft PR #1786 (one machine, 9 to 10 repetitions on a shared box; see the update under H1), and it shows the other runtimes do not show an inference-length stall.
- **Baselines for RQ3:** Triton Inference Server (instance groups, dynamic batching) [S], MPS/MIG partitioning [S], a Clockwork-style predictive scheduler (reimplemented, since Clockwork targets a cluster), and for LMs vLLM/SGLang [O].
- **Metrics:** p50/p99/p99.9 latency and SLO attainment; max/percentile pause of allocating threads; GC pause distribution; throughput; GPU utilization and memory high-water; device OOM count; energy in joules per inference (NVML energy counters on desktop GPU, tegrastats or INA sensors on Jetson, powermetrics on Apple silicon: reviewers will expect named sensors and sampling rates); accuracy when anything is approximated.
- **Methodology:** at least 5 to 30 repeated runs with confidence intervals, warm-up exclusion, fixed clocks or reported clock behavior, more than one GPU (the RTX 4090 alone is a single data point) and at least one NPU/edge platform for the "across CPU, GPU and NPU" claim (Jetson, Apple CoreML or Qualcomm). The word "NPU" in the thesis statement currently has no preliminary evidence behind it.

---

## (e) Risks: what a reviewer will say

1. **"This is engineering."** Likely. The parking and IO-binding results are applications of known mechanisms (Go syscall handoff, JNI region handling, ORT IO binding). Mitigation: contribute a generalizable protocol or accounting model, a cross-runtime characterization, and a released benchmark; frame Objeck as the vehicle, not the contribution.
2. **"Python plus a scheduler does this."** Triton Inference Server, Ray Serve and Clockwork-style systems already schedule concurrent inference; Python callers release the GIL in native calls. Mitigation: show a workload where the application itself allocates heavily beside inference (the case an external server hides) and where in-process budget enforcement beats the server on tail latency or memory.
3. **"Jac already claims AI-native language."** Mitigate by not claiming it; position against it explicitly (remote LLM calls vs local accelerator resources).
4. **"The language constructs are each known."** True for shape types, uncertainty types and capability systems (Section c4). Do not present them as the contribution.
5. **"Single GPU, single model, no statistics."** The current data fits this description. Fix before writing.
6. **"Why a new language and not a library/JVM/Go runtime?"** Needs an answer rooted in a measured thing the existing runtimes cannot do, or a concession that the contribution is a protocol others can adopt.
7. **"Language user study missing."** If any ergonomics claim is made (as Eon and MTP did), reviewers expect a study or at least LoC comparisons with baselines.
8. **Novelty-search risk.** My search was limited; a hit in ISMM, VEE, MPLR or an industry blog could weaken RQ1/RQ2. The advisor should run a systematic search.

---

## (f) Bibliography (verified links)

Status: O = opened, S = confirmed from search listing (publisher/author/project page), as defined at top.

**AI-native languages and compilers**
1. Dantanarayana, Kang, Sivasothynathan, Clarke, Li, Kashmira, Flautner, Tang, Mars. "MTP: A Meaning-Typed Language Abstraction for AI-Integrated Programming." OOPSLA 2025. https://arxiv.org/abs/2405.08965 [O]
2. Dantanarayana, Kashmira, Ma, Prabhashana, Tang, Mars. "Agent-Native by Design: A Programming Language for Agentic Applications." arXiv 2611.21298, 16 Nov 2026. https://arxiv.org/abs/2611.21298 [O]
3. Dantanarayana, Kashmira, Ma, Flautner, Tang, Mars. "Prompt Less, Smile More: Semantic Engineering in Lieu of Prompt Engineering." arXiv 2511.19427 (v2 30 Oct 2026). https://arxiv.org/abs/2511.19427 [O]
4. ASPLOS'26 Jaseci tutorial page (22 Mar 2026). https://jaseci-labs.github.io/jaseci-asplos-tutorial-2026/ [O]
5. Beurer-Kellner, Fischer, Vechev. "Prompting Is Programming: A Query Language for Large Language Models." PLDI 2023. https://arxiv.org/abs/2212.06094 [O]
6. Zheng et al. "SGLang: Efficient Execution of Structured Language Model Programs." arXiv 2312.07104. https://arxiv.org/abs/2312.07104 [O]
7. Khattab et al. "DSPy: Compiling Declarative Language Model Calls into Self-Improving Pipelines." arXiv 2310.03714. https://arxiv.org/abs/2310.03714 [O]
8. Chen et al. "TVM: An Automated End-to-End Optimizing Compiler for Deep Learning." arXiv 1802.04799 (venue not shown on the page I opened). https://arxiv.org/abs/1802.04799 [O]
9. Lattner et al. "MLIR: A Compiler Infrastructure for the End of Moore's Law." arXiv 2002.11054. https://arxiv.org/abs/2002.11054 [O]
10. Godoy et al. "Mojo: MLIR-Based Performance-Portable HPC Science Kernels on GPUs for the Python Ecosystem." SC'25 Workshops. https://arxiv.org/abs/2509.21039 [S]
11. Modular, "ModCon 2026 announcements," 18 Aug 2026. https://modular.com/blog/modcon-announcements [O]
12. Phoronix, "Modular's MAX Source Code Now Fully Opened, Under New MAX License" (Oct 2026). https://phoronix.com/news/Modular-MAX-Complete-Open [S, fetch returned 403]
13. Ansel et al. "PyTorch 2: Faster Machine Learning Through Dynamic Python Bytecode Transformation and Graph Compilation." ASPLOS 2024. https://pytorch.org/assets/pytorch2-2.pdf [S]
14. Tillet, Kung, Cox. "Triton: An Intermediate Language and Compiler for Tiled Neural Network Computations." MAPL 2019. https://www.eecs.harvard.edu/~htk/publication/2019-mapl-tillet-kung-cox.pdf [S]
15. Henriksen et al. "Futhark: Purely Functional GPU-Programming with Nested Parallelism and In-Place Array Updates." PLDI 2017. https://www.futhark-lang.org/publications/pldi17.pdf [S]
16. Paszke et al. "Getting to the Point: Index Sets and Parallelism-Preserving Autodiff for Pointful Array Programming" (Dex). PACMPL 2021. https://arxiv.org/abs/2104.05372 [S]
17. Bornholt, Mytkowicz, McKinley. "Uncertain<T>: A First-Order Type for Uncertain Data." ASPLOS 2014. https://www.microsoft.com/en-us/research/publication/uncertaint-a-first-order-type-for-uncertain-data-2/ [S]
18. Cusumano-Towner, Saad, Lew, Mansinghka. "Gen: A General-Purpose Probabilistic Programming System with Programmable Inference." PLDI 2019. https://pldi19.sigplan.org/details/pldi-2019-papers/45/Gen-A-General-Purpose-Probabilistic-Programming-System-with-Programmable-Inference [S]

**Serving, scheduling, sharing, edge, energy**
19. Gujarati et al. "Serving DNNs like Clockwork." OSDI 2020. https://www.usenix.org/conference/osdi20/presentation/gujarati [O]
20. Romero, Li, Yadwadkar, Kozyrakis. "INFaaS: Automated Model-less Inference Serving." USENIX ATC 2021. https://www.usenix.org/conference/atc21/presentation/romero [O]
21. Shen et al. "Nexus: A GPU Cluster Engine for Accelerating DNN-Based Video Analysis." SOSP 2019. https://www.microsoft.com/en-us/research/publication/nexus-a-gpu-cluster-engine-for-accelerating-dnn-based-video-analysis/ [S]
22. Zhang, Tang, Khandelwal, Stoica. "SHEPHERD: Serving DNNs in the Wild." NSDI 2023. https://www.usenix.org/conference/nsdi23/presentation/zhang-hong [O]
23. Li et al. "AlpaServe." OSDI 2023. https://arxiv.org/abs/2302.11665 [O]
24. Yu et al. "Orca." OSDI 2022. https://www.usenix.org/conference/osdi22/presentation/yu [O]
25. Kwon et al. "Efficient Memory Management for Large Language Model Serving with PagedAttention." SOSP 2023. https://arxiv.org/abs/2309.06180 [O]
26. Agrawal et al. "Taming Throughput-Latency Tradeoff in LLM Inference with Sarathi-Serve." arXiv 2403.02310 (OSDI 2024 per common citation, not confirmed on the page). https://arxiv.org/abs/2403.02310 [O]
27. Luo et al. "Autellix: An Efficient Serving Engine for LLM Agents as General Programs." arXiv 2502.13965. https://arxiv.org/abs/2502.13965 [O]
28. Han, Zhang, Chen, Chen. "Microsecond-scale Preemption for Concurrent GPU-accelerated DNN Inferences" (REEF). OSDI 2022. https://www.usenix.org/conference/osdi22/presentation/han [O]
29. Bai, Zhang, Jin, Zhu. "PipeSwitch." OSDI 2020. https://www.usenix.org/conference/osdi20/presentation/bai [O]
30. Yu, Chowdhury. "Salus: Fine-Grained GPU Sharing Primitives for Deep Learning Applications." arXiv 1902.04610. https://arxiv.org/abs/1902.04610 [O]
31. Jeong et al. "Band: Coordinated Multi-DNN Inference on Heterogeneous Mobile Processors." MobiSys 2022. https://jeonchangmin.github.io/project/band/ [S]
32. Jia, Zhang, Cao et al. "CoDL: Efficient CPU-GPU Co-execution for Deep Learning Inference on Mobile Devices." MobiSys 2022. https://www.microsoft.com/en-us/research/?p=959106 [S]
33. Wu, Wang, Pei, Cui, Liu et al. "HiTDL: High-Throughput Deep Learning Inference at the Hybrid Mobile Edge." IEEE TPDS 2022. https://fangmingliu.github.io/files/hitdl-full-paper-jing.pdf [S]
34. Yang, Nahrstedt, Guo, Zhou. "DeepRT: A Soft Real Time Scheduler for Computer Vision Applications on the Edge." arXiv 2105.01803 (SEC 2021). https://arxiv.org/abs/2105.01803 [S]
35. Xu et al. "Fast On-device LLM Inference with NPUs" (llm.npu). ASPLOS 2025. https://arxiv.org/abs/2407.05858 [S]
36. You, Chung, Chowdhury. "Zeus." NSDI 2023. https://arxiv.org/abs/2208.06102 [O]
37. "An Analysis of Collocation on GPUs for Deep Learning Training." arXiv 2209.06018. https://arxiv.org/abs/2209.06018 [S]; MISO https://arxiv.org/abs/2207.11428 [S]; MIGPerf https://arxiv.org/abs/2301.00407 [S]
38. NVIDIA Triton Inference Server, concurrent model execution. https://docs.nvidia.com/deeplearning/triton-inference-server/user-guide/docs/model_execution.html [S]
39. NVIDIA TensorRT-LLM Batch Manager. https://nvidia.github.io/TensorRT-LLM/advanced/batch-manager.html [S]
40. ONNX Runtime IO binding. https://onnxruntime.ai/docs/performance/tune-performance/iobinding.html [O]
41. Reddi et al. "MLPerf Inference Benchmark." ISCA 2020. https://arxiv.org/abs/1911.02549 [O]
42. Dean, Barroso. "The Tail at Scale." CACM 56(2), 2013. https://research.google/pubs/pub40801/ [S]

**Managed runtimes and accelerators**
43. Fumero et al. "Dynamic Application Reconfiguration on Heterogeneous Hardware" (TornadoVM). VEE 2019. https://jjfumero.github.io/files/VEE2019_Fumero_Preprint.pdf [S]
44. OpenJDK Project Babylon / HAT. https://openjdk.org/projects/babylon [S]
45. JEP 423: Region Pinning for G1. https://openjdk.org/jeps/423 [S]
46. Go issue #57103 (cgo return and P handoff). https://golang.org/issue/57103 [S]; #36273 https://golang.org/issue/36273 [S]
47. CUDA.jl PR #3318 and issue #3310 (finalizers and GPU memory). https://github.com/JuliaGPU/CUDA.jl/pull/3318 [S]
48. PyTorch blog, "Understanding GPU Memory 2: Finding and Removing Reference Cycles." https://pytorch.org/blog/understanding-gpu-memory-2/ [S]
49. Besard, Foket, De Sutter. "Effective Extensible Programming: Unleashing Julia on GPUs." IEEE TPDS 2019. https://arxiv.org/abs/1712.03112 [S]

**Language constructs: shapes, agents, budgets**
50. Wu, Shen, Chen. "An Empirical Study on Tensor Shape Faults in Deep Learning Systems." arXiv 2106.02887. https://arxiv.org/abs/2106.02887 [O]
51. Lagouvardos et al. "Static Analysis of Shape in TensorFlow Programs." ECOOP 2020. https://drops.dagstuhl.de/entities/document/10.4230/LIPIcs.ECOOP.2020.15 [S]
52. jaxtyping. https://docs.kidger.site/jaxtyping/ [S]
53. Debenedetti et al. "Defeating Prompt Injections by Design" (CaMeL). arXiv 2503.18813. https://arxiv.org/abs/2503.18813 [O]
54. Beurer-Kellner et al. "Design Patterns for Securing LLM Agents against Prompt Injections." arXiv 2506.08837. https://arxiv.org/abs/2506.08837 [O]
55. Shi et al. "Progent: Securing AI Agents with Privilege Control." arXiv 2504.11703. https://arxiv.org/abs/2504.11703 [O]
56. Doshi et al. "Towards Verifiably Safe Tool Use for LLM Agents." ICSE-NIER 2026. https://arxiv.org/abs/2601.08012 [O]
57. "Stop Means Stop: Measuring and Repairing the Enforcement Gap in Agent-Framework Control Primitives." arXiv 2607.14166. https://arxiv.org/pdf/2607.14166 [S]
58. Sampson et al. "EnerJ." PLDI 2011. https://ready.cs.washington.edu/~luisceze//publications/Enerj-pldi2011.pdf [S]
59. Sorber et al. "Eon: A Language and Runtime System for Perpetual Systems." SenSys 2007. https://www.cics.umass.edu/~emery/pubs/Eon-SenSys2007.pdf [S]
60. Carbin, Misailovic, Rinard. "Verifying Quantitative Reliability for Programs That Execute on Unreliable Hardware" (Rely). OOPSLA 2013. https://mcarbin.github.io/rely/ [S]; Misailovic et al. "Chisel." OOPSLA 2014. https://people.csail.mit.edu/rinard/paper/oopsla14.pdf [S]

**Counts:** 60 numbered entries. Roughly 32 are O (opened) and 30 or so S (a few entries group several S links). No entry is cited from memory alone.

---

## (g) Unverified leads and open questions for the advisor

**Unverified leads (could not confirm in this pass)**

- Halide (PLDI 2013), Taichi, JAX/XLA, IREE, BAML, Pyro, Stan: not searched successfully.
- IBM GPU-enabled JVM, Aparapi, ILGPU (.NET), Go GPU bindings: not searched.
- Hasktorch (typed tensors with device in type?) and torchtyping: not confirmed; the "device in the type" thin-spot claim depends on this.
- ICSE paper "What is wrong with my model?": the title did not resolve; only arXiv 2106.02887 and related shape-fault work were found.
- .NET preemptive mode documentation, real-time Java, QoS-aware languages, JNI specification text: not retrieved.
- MLPerf Tiny/Edge, DeathStarBench-style ML suites; exact energy-measurement practices (NVML, tegrastats, powermetrics, INA) in cited papers.
- Ray Serve scheduler docs; TensorRT-LLM scheduling details beyond the batch-manager page.
- AIOS, Parrot (mentioned only in a search summary), Continuum, HexAGenT, DynamoLLM, EnvPipe, Clover, "Speculative Actions" (cited by the Jac paper), SIGIL, GraphMend (arXiv 2509.16248), Jaseci 2305.09864: listed in search results or cited inside papers, not opened.
- The Jac paper's last ~21k characters and Modular's own MAX 26.7 announcement and license text were not read.
- 2026 arXiv items surfaced only as search listings (for example 2605.24569 "Energy-Aware Computing in the Year 2026", 2606.25082, 2607.14166, 2605.27435, 2610.35981) were not opened except where marked O above.
- The author's own PR numbers (#1760, #1768) and measurements are taken from the brief; I did not re-run or inspect them. The H1 and H2 attributions were corrected on 2026-11-25 (the longest-wait figure is from #1768 and the IO-binding figures from #1760, as in the thesis design note).

**Questions for the advisor**

1. Which contribution type is acceptable: a systems paper (protocol plus characterization), a language paper (constructs plus user study), or both? The spine mixes them, and each venue family wants different evidence.
2. Is a single-vehicle (Objeck-only) evaluation acceptable, or must the protocol be demonstrated on a second runtime (JVM or .NET) to count as general?
3. Can the thesis claim "NPU" without a platform that exposes NPU inference concurrently (Apple, Qualcomm, Intel)? Hardware budget?
4. Should agent capability/effect types be dropped given the CaMeL/Progent/Jac density, or kept as a separate chapter with its own evaluation (for example AgentDojo)?
5. Which venues: ISMM/VEE/MPLR for the runtime core, OOPSLA/PLDI/Onward! for constructs, EuroSys/ATC/MLSys for the scheduler?
6. How should the position against Jac/Jaseci (same-month paper, funded group, ASPLOS tutorial) be stated in the proposal?
7. Is a user-facing benchmark release (mixed allocate-and-infer workload suite) in scope as a contribution?
