# Results: collector stalls beside a blocking inference call, six hosts

Date 2026-11-24. Harness, workload definition, exact commands and versions: [README.md](README.md). Every number
below is a median across repetitions (min-max in brackets) of the **quiet** runs (other tenants used at most 3 of 32
hardware threads during the run, and for CUDA the 1-minute load average at the start was below 4). All runs, the
noisy ones included, are in `results/summary.md`; they tell the same story with wider ranges. The full per-run
data is `results/results.csv` and `results/raw.jsonl`.

Status of the claims: **measured** here, one machine, one model, one batch size, one allocation pattern, one
quiet-ish day. Treat anything finer than "factor of 3" as unproven.

## Setup in one table

| item | value |
|---|---|
| machine | AMD Ryzen 9 7950X3D (32 threads), 62 GB, WSL2 Ubuntu 24.04.4 (kernel 6.6.87.2-microsoft-standard-WSL2) on Windows 11 |
| GPU | RTX 4090, driver 621.24, CUDA 12.6 toolkit, cuDNN 9.25.1; SM clock, pstate, power and temperature before and after every run are in `results.csv`/`raw.jsonl`, full `nvidia-smi -q -d CLOCK` per matrix in `results/clocks_*.txt` |
| ORT | 1.24.2 (Objeck, Java, .NET, Go, C++), 1.24.1 (Python: PyPI has no 1.24.2) |
| model, batch | ResNet-34 FP32, batch 32, CUDA EP and CPU EP with default options |
| per run | 20 s warm-up, 60 s measured; allocator 1 block of 1-64 KB per 100 us, live set 268 MB |
| repetitions | CUDA: 10 per primary configuration (quiet reps 9-10), 5 per tuned/bound variant; CPU: **5** per primary configuration (not 10: the shared machine made 10 take over 12 h) |
| correctness | every host produces the same output on the same input: sum 2.574867, argmax 904, first three logits identical to 6 decimals; largest difference to the C++ control 4.4e-7 (Python, ORT 1.24.1), 0 elsewhere (tolerance asked: 1e-4) |
| other tenants | the machine was shared with other agents (load average 4-30 for long stretches). The quiet subset's median "other tenants' cores" is 0.0 to 0.06 per configuration; the CPU matrix ran with nobody else on the machine (0.0 cores) |

## 1. CUDA: allocator stall (microseconds)

| host | config | p50 | p95 | p99 | p99.9 | max |
|---|---|---:|---:|---:|---:|---:|
| C++ (no collector) | control | 2 | 7 | 14 | 46 | **1,309** (419-9,085) |
| Objeck | **parked** (PR #1768) | 27 | 30 | 40 | 109 | **5,638** (5,086-7,866) |
| Objeck | unparked (pre-PR behaviour) | 27 | 30 | 39 | 92 | **19,615** (4,318-54,021) |
| Objeck | bound-copy, parked | 27 | 33 | 40 | 129 | 7,433 (6,844-29,828) |
| Objeck | bound-copy, unparked | 27 | 32 | 39 | 83 | 10,392 (9,637-13,027) |
| Java | G1 | 1 | 5 | 12 | 55 | **80,627** (30,158-97,232) |
| Java | Parallel | 1 | 5 | 15 | 95 | **47,953** (25,363-80,964) |
| Java | ZGC | 1 | 18 | 52 | 342 | **2,117** (963-4,803) |
| Java | G1 tuned (`MaxGCPauseMillis=10`, 4 GB pretouched) | 1 | 5 | 15 | 62 | 9,079 (8,239-15,791) |
| Java | ZGC generational tuned | 1 | 6 | 17 | 58 | 1,246 (593-1,757) |
| .NET | Workstation, concurrent | 2 | 6 | 22 | 97 | **5,417** (3,484-7,102) |
| .NET | Workstation, non-concurrent | 1 | 6 | 13 | 54 | **32,961** (26,601-41,841) |
| .NET | Server, concurrent | 2 | 6 | 12 | 60 | **10,529** (9,431-30,008) |
| .NET | Server, non-concurrent | 2 | 6 | 14 | 54 | **30,379** (26,631-33,858) |
| .NET | Server concurrent, gen0 budget 256 MB | 1 | 5 | 10 | 44 | 20,708 (18,047-27,316) |
| Go | default (`GOGC=100`) | 2 | 8 | 20 | 69 | **998** (475-15,071) |
| Go | `GOGC=400 GOMEMLIMIT=8GiB` | 2 | 8 | 21 | 59 | 1,299 (550-2,331) |
| Python | default | 3 | 11 | 30 | 104 | **1,331** (690-8,541) |
| Python | GIL switch interval 0.5 ms | 3 | 14 | 42 | 102 | 1,669 (1,098-10,323) |

Reading the columns: p50-p99 are all below 60 us everywhere, and Objeck's p50 of 27 us is its own per-iteration
overhead (four timer traps and a byte-array allocation in the interpreter), not a stall. The hosts separate
only in the **max** (the single worst iteration in 60 s) and, for ZGC, in p99.9. The C++ control's own max of
0.4-9 ms is the floor of this machine (WSL2 scheduling), so differences of a few ms between hosts are inside the
noise; differences of 10-100 ms are not.

## 2. CUDA: inference thread and collector

| host | config | infer p50 ms | p99 ms | images/s | collections in 60 s | collector pause, total ms | max pause ms |
|---|---|---:|---:|---:|---:|---:|---:|
| C++ | control | 7.3 | 8.6 | 4,352 | 0 | | |
| Objeck | parked, `Session->Run` | 58.2 | 77.8 | 538 | 202 | n/a | 5.7 |
| Objeck | unparked, `Session->Run` | 59.1 | 88.0 | 526 | 196 | n/a | **157.0** |
| Objeck | parked, `BoundSession` copy | 9.4 | 11.6 | 3,371 | 64 | n/a | 10.0 |
| Objeck | unparked, `BoundSession` copy | 9.2 | 12.2 | 3,436 | 64 | n/a | 10.6 |
| Java | G1 | 7.3 | 8.5 | 4,300 | 12 | 433 | 81 |
| Java | Parallel | 7.3 | 8.5 | 4,283 | 14 | 480 | 48 |
| Java | ZGC | 7.3 | 8.5 | 4,325 | 44 | 272 | 58 (a whole cycle, not a pause) |
| .NET | Workstation conc / non-conc | 7.3 | 8.4 | 4,354 / 4,313 | 392 / 146 | 455 / 495 | |
| .NET | Server conc / non-conc | 7.3 | 8.4 / 8.3 | 4,322 / 4,327 | 106 / 82 | 313 / 524 | |
| Go | default | 7.2 | 8.1 | 4,410 | 68 | 22 | 0.5 |
| Python | default | 12.4 | 13.5 | 2,558 | 0 (the cyclic collector never ran: blocks are freed by reference counting) | 0 | 0 |
| Python | switch interval 0.5 ms | 7.8 | 8.7 | 4,046 | 0 | 0 | 0 |

Notes. Objeck's max pause is `runtime.gc.pause.max_us`, cumulative since process start (it includes
warm-up) and measured from when the collector asks the threads to stop, so for the unparked build it contains
the time spent waiting for the inference call. Java's ZGC and .NET pause figures use each runtime's own
accounting and are not comparable to each other or to Objeck's to better than a factor of two. GPU memory:
all hosts add 930-980 MiB over the idle card (Objeck 1,270 MiB), flat across the run. Objeck's
`Session->Run` is 8x slower per call than the other hosts because it converts a 64-bit `Float[]` of 4.8 million
elements into native memory per call (the thesis note and PR #1760 say so); the `BoundSession` rows remove
that and match the other hosts' host traffic, at 9.3 ms.

## 3. CPU provider (5 repetitions, nobody else on the machine, ORT using all cores)

| host | config | stall p99 us | p99.9 us | max us | infer p50 ms | images/s |
|---|---|---:|---:|---:|---:|---:|
| C++ control | | 71 | 10,005 | 20,452 | 348 | 93 |
| Objeck | **parked** | 88 | 1,281 | **40,025** (35,529-48,795) | 420 | 75 |
| Objeck | unparked | 89 | 1,239 | **343,737** (287,740-362,475) | 425 | 75 |
| Java | G1 / Parallel / ZGC | 201 / 246 / 236 | 10,097 / 10,072 / 10,066 | 109,093 / 122,529 / 25,106 | 312 / 365 / 323 | 98 / 86 / 98 |
| .NET | Workstation conc / non-conc | 91 / 80 | 10,006 / 10,000 | 24,396 / 75,108 | 334 / 381 | 98 / 82 |
| .NET | Server conc / non-conc | 277 / 178 | 10,155 / 10,084 | 51,059 / 82,882 | 318 / 363 | 99 / 90 |
| Go | default | 405 | 10,263 | 26,646 | 293 | 107 |
| Python | default | 58 | 1,532 | 20,242 | 383 | 83 |

The 10 ms p99.9 shared by C++, Java, .NET and Go is the Linux scheduler: with ORT's worker threads saturating
all 16 cores the spinning allocator thread loses its core for one 10 ms slice now and then. It is not a
collector effect (the C++ control has it). Objeck and Python do not show it (p99.9 1.2-1.5 ms): their
allocator thread is the one descheduled less often; the cause was not investigated, so do not read it as
a benefit of either runtime.
The CPU result worth keeping is the max: unparked Objeck waits **344 ms** for one inference; parked 40 ms.

## 4. What it shows

**Does Objeck's parking beat Java, .NET, Go and Python on the allocator stall?** No. It removes a stall that
Objeck had and the others did not, which brings Objeck level with .NET's concurrent workstation collector, not
ahead of it; ZGC, Go, Python and the C++ control are lower. With the call parked, Objeck's worst stall on CUDA is
5.6 ms: below G1 (81 ms), Parallel (48 ms), non-concurrent .NET (30-33 ms) and Server-concurrent .NET (10.5 ms);
level with workstation-concurrent .NET (5.4 ms); above ZGC (2.1 ms), Go (1.0 ms), Python (1.3 ms) and the C++
control (1.3 ms). On the CPU provider Objeck parked (40 ms) is in the same range as the other managed hosts (24 to
123 ms). The sample is 9-10 runs, max is an extreme-value statistic, the Objeck allocator also executes 27 us of
interpreter work per iteration, and the machine floor is a few ms, so "level with" is the strongest honest
statement for 5 ms against 1-10 ms.

**Is the effect specific to Objeck or generic?** Generic. The effect of parking inside Objeck is real and large
where the inference call is long: worst stall 19.6 to 5.6 ms on CUDA (3.5x, ranges overlap: unparked 4.3-54 ms),
Objeck's own max collection pause 157 to 5.7 ms (27x), and on the CPU provider 344 to 40 ms (8.6x, ranges do
not overlap). That is the same direction as PR #1768 reported (176.7 to 19.9 ms, 8.9x), but that size was not
reproduced: it is smaller here on CUDA. A likely reason, not tested, is that this workload allocates at a fixed
pace instead of as fast as possible (the PR's allocator thread collected garbage-heavy rounds back to back). But every other managed host in this table was already doing the
equivalent without being asked: the JVM treats a thread in JNI as at a safepoint, .NET switches a thread in P/Invoke to
preemptive mode, and Go counts a goroutine in cgo as not running. None of them lets a 7 ms (or 400 ms)
inference hold up a collection, which is why none of them shows an inference-length stall (their large maxima
are their own stop-the-world collection work on the 268 MB live set: G1 and Parallel copy it, non-concurrent .NET
marks it). So the claim that fits the data is: **the stall that parking removes was an Objeck-specific
defect of the unparked call, and parking restores what the mainstream runtimes' native-call protocols already
give.** It does not show that collector-cooperative inference management beats a library-based approach in
another runtime. What remains for the thesis is the residual 5 ms (Objeck's collection itself) and everything
that is not parking (device memory lifetime, session ownership, scheduling), which this experiment does not
test.

**Other observations worth keeping.**

1. The p99 of the allocator does not discriminate: every host is within 10-52 us on CUDA. Tail behaviour is in
   the max and, for ZGC (342 us), in p99.9.
2. ZGC has the lowest max of the Java collectors and the highest p99.9: its load barriers cost a few hundred
   microseconds in the tail but it never stops the allocator for tens of milliseconds. Concurrent GC helps
   .NET Workstation (5.4 vs 33 ms) and not much on Server (10.5 vs 30 ms).
3. The "tuned" settings helped where they target the stall (G1 `MaxGCPauseMillis=10` 81 to 9 ms with a
   pretouched 4 GB heap; generational ZGC 2.1 to 1.2 ms; Go `GOGC=400` cut collections 68 to 17) and not where
   they do not (.NET gen0size 20.7 ms; Python switch interval changed throughput of the inference thread, 2,558
   to 4,046 images/s, because the GIL hand-off after ORT's `Run` returns takes up to the 5 ms switch interval,
   not the stall).
4. Python: ORT releases the GIL inside `Run`, so the allocator thread runs freely; the cost shows up as the
   inference thread waiting for the GIL after each run (12.4 ms vs 7.3 ms median) because the spinning
   allocator holds it until the switch interval expires. Python's allocator never meets a tracing collector
   here (reference counting frees the blocks), which makes its low stall a property of this workload, not of
   Python's collector.
5. Objeck's `BoundSession` rows (9.4 vs 9.2 ms, stall max 7.4 vs 10.4 ms) show no benefit from parking: the
   calls are 9 ms, shorter than the interval between collections, so few collections overlap one.

## 5. Threats to validity

* **Shared machine.** Other agents ran builds and test suites throughout; the quiet subset removes runs with
  more than 3 cores of foreign load but cannot remove effects of WSL2 scheduling and the Windows host. The
  C++ control's max ranges from 0.4 to 9 ms. All-runs tables are in `results/summary.md` (`noisy` reps 0-3 of 10).
* **Workload is not the PR's.** The paced allocator (1 block per 100 us, 268 MB live, every 16th survives)
  differs from `bench_gc_stall.obs` (as-fast-as-possible rounds of 300 strings). Objeck's number for the PR's
  own benchmark was not re-measured; the PR's 176.7 ms is not reproduced here (our unparked CUDA worst case
  has median 19.6 ms, max 54 ms).
* **Objeck inference is a different shape.** `Session->Run` is 58 ms against 7 ms elsewhere because of
  per-call conversion, so its inference duty cycle and garbage production differ (202 collections per minute
  against 12-110). The `BoundSession` rows give a like-for-like call (9.4 ms) but only 5 reps.
* **Objeck's allocator measures itself differently.** Its timer API has no absolute clock, so pacing uses a
  hand-off between two timers and the spin target was stretched (124 us for a realised 100 us period,
  checked by the sample count: about 585,000 per 60 s on the other hosts, about 622,000 in Objeck, i.e. 6% more allocations). Its stall includes
  27 us of interpreter overhead per iteration. Stall is clamped at 0. Objeck data is live-set Block objects (an extra
  object per block) while the other hosts hold arrays directly.
* **"Unparked" is a rebuilt library, not a released one.** The same branch source with the two
  `APITools_Parked` scopes of `Session::Run` removed (and, for the bound rows, those of `device_tensor.h`).
* **Not identical conditions across hosts.** Python's ORT is 1.24.1; Java and .NET use the ORT copy inside
  their package (1.24.2) rather than the shared GPU build; CUDA provider options are defaults everywhere
  but each language binding sets some differently; the Go binding is compiled against 1.23 headers; the JVM
  default heap is 1/4 of RAM (15.6 GB) and .NET's heap hard limit is unset.
* **Collector statistics are not comparable across hosts.** Pause accounting differs (see section 2).
* **Repetition counts.** CUDA primary 9-10 quiet reps; tuned and bound variants 5; CPU 5. No confidence
  intervals are computed; ranges are min-max.
* **One model, one batch, one GPU, one allocation size distribution.** Nothing here generalises to long
  inference calls with many concurrent allocators, to LLM decode loops, or to heaps of different size.
* **Wall clock.** A run is 60 s; a rare 100 ms event could be missed or caught by chance (max statistics).

## 6. What to measure next

1. **Run the PR's own benchmark** (`bench_gc_stall.obs`, 10 s) on the same machine with this unparked/parked pair, and sweep allocation rate
   (1, 10, 100 k allocations per second) and live-set size (64 MB to 2 GB) to find where parking stops mattering.
2. **Long native calls**: inference of 50 and 400 ms on the GPU (batch 128-256) for every host, to separate "the
   runtime waits for the call" from "the runtime's own pause". On the JVM also test JNI with a critical array
   (`GetPrimitiveArrayCritical`) and Panama downcalls without `critical`, where a collection can be blocked.
3. **Multiple inference threads and multiple allocating threads**, and a model thread that holds a pinned buffer
   across the call, which is where managed hosts do pay (JVM critical sections, .NET pinned handles, Go
   `runtime.LockOSThread`).
4. **Inference-latency side**: the effect on the inference thread's tail (GIL hand-off, pinned memory) with
   IO binding on every host (ORT Java and .NET also have it), to test H2's "Python+IO binding" baseline.
5. **A quiet machine**: re-run the matrix with the box otherwise idle, 30 repetitions as in H1, with confidence
   intervals, and `isolcpus`/CPU pinning for the two measured threads.
6. **Native Windows and a second GPU** to check the WSL2 scheduling floor.
