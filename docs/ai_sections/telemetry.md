## Inference telemetry (System.Telemetry)

`System.Telemetry` is the measurement base for resource-aware inference: it times a call, keeps latency statistics per (model, device, batch) in bounded memory, samples the resources around the call, estimates energy from sampled power, saves the result as a profile and answers `Predict` from it. It is pure Objeck, in its own library `telemetry.obl`, and has no dependency on `onnx.obl`: a program (or a later change) wraps its own session calls with it.

Compile with `-lib telemetry,gen_collect,json`, or `-lib @telemetry`. Every latency is in **milliseconds**, as a `Float`.

### The clock

**Every duration here comes from `System.Time.Timer`, which reads the C++ `steady_clock`: a monotonic clock that never jumps with the wall clock, with nanosecond ticks.** `Timer` hands the interval back as seconds in a `Float`, so a latency is `GetElapsedTime() * 1000.0` and keeps nanosecond resolution (a double holds 15 digits). Measured on Linux (WSL2, Ryzen 9 7950X3D): an empty `Start` plus `End` reads 19 ns at the minimum, 29 ns at the median and 39 ns at the 99th percentile (`Telemetry->MeasureOverhead(n)` measures it on your machine). On Windows `steady_clock` is the performance counter, whose tick is 100 ns, so an interval under a few hundred nanoseconds is coarse there; that is what the standard library does, and this was not measured on Windows hardware beyond running the regression tests.

What the runtime does **not** expose is an absolute monotonic timestamp in nanoseconds. A `Timer` measures an interval and cannot be read as "now"; `System.Time.Date` is the wall clock at one-second resolution. Anything that has to place an event on a timeline (a sampler reading, the energy attributed to a call) uses `Telemetry->NowMs()`, the `runtime.uptime_ms` property, which is the same monotonic clock at **one-millisecond** resolution. No latency in this library ever comes from the wall clock.

The recorder does its work after the clock has stopped: `Stop` ends the `Timer` first, then reads memory and updates the series. That work still costs time between calls. Measured on the same machine, one `Start` and `Stop` through a `TimingRecorder` costs about 11 microseconds with memory capture off and 17 with it on (the process memory read is a string property parse), and `SeriesStats->Add` alone about 80 ns.

### Timing a call

```objeck
use System.Telemetry;

# a handle: Stop it when the call returns
probe := Telemetry->Start("resnet34", "cuda", 32, "run");      # model, device, batch, phase tag
outputs := session->Run(inputs);
ms := probe->Stop();                                           # elapsed milliseconds, recorded

# a failed call should not enter the latency: drop it
if(outputs = Nil) { probe->Cancel(); };

# or time a function reference
ms := Telemetry->Time("resnet34", "cuda", 32, "run", MyClass->Work() ~ Nil);
```

`Telemetry->Start(model, device, tag)` is the batch-1 form. A handle that is never stopped records nothing; a second `Stop` returns the same interval and records nothing more. Names may contain any character, including `|`: the series key escapes `%` and `|` (as `%25` and `%7C`), so two different names never share a cell. The static calls use a process-wide recorder (`Telemetry->Default()`, emptied by `Telemetry->Reset()`); make your own with `TimingRecorder->New()` to keep profiles apart. A recorder may be shared by threads: it records under a lock.

The **tag** names a phase ("pre", "infer", "post"): each (model, device, batch, tag) is its own series. `Predict(model, device, batch)` takes the series with an empty tag, or the only tagged one if there is no untagged one; ask for one explicitly with `PredictTag`.

### Statistics

`SeriesStats` is the streaming statistics of one series: count, minimum, maximum, mean and the sample standard deviation (divisor n - 1) by Welford's update, so a large offset does not destroy the variance (a test checks a 1e9 offset); quantiles; the warm-up discard; a ring of the last raw samples.

```objeck
s := SeriesStats->New(10);              # discard the first 10 samples
s->Add(ms);
"n={$s->GetCount()} mean={$s->GetMean()} sd={$s->GetStdDev()} p50={$s->GetP50()} p99={$s->GetP99()}"->PrintLine();
last := s->GetRing()->ToArray();        # the last 256 raw samples, oldest first
```

**Quantiles.** The quantile is by nearest rank: the sample at rank `ceil(q * n)` in sorted order, so p50 of the eight values 2 4 4 4 5 5 7 9 is 4 and p95 of 20 samples is the 19th. Nearest rank never invents a value that was not observed and is the usual definition for latency tails. Two paths give it:

- **Exact, while the series is small.** Up to 512 samples (`exact_limit`) every sample is kept and the quantile is exact. `IsExact()` says which path answered; `GetRelativeError()` is 0 on this path.
- **From a sketch, beyond that.** `LatencySketch` is a log-bucketed histogram (the DDSketch construction, the same idea as an HDR histogram with a fixed relative error). A value x falls in bucket `ceil(log(x / 1 ns) / log(gamma))` with `gamma = (1 + a) / (1 - a)` for the relative error `a`; a bucket is reported by its geometric centre `2 * upper / (gamma + 1)`, which is within `a` of every value in the bucket. The estimate is clamped into the exact minimum and maximum the sketch also keeps, so p0 and p100 are exact.

**The stated error is relative and on the value, not on the rank:** the estimate of the order statistic at rank `ceil(q * n)` is within a relative error `a` of that order statistic. The default is `a` = 1 percent, with 1,498 counters (12 KB) covering 1 ns to 1e7 ms (under three hours); 0.5 percent takes 2,995 and 0.1 percent 14,968. A value below 1 ns falls in the lowest bucket (an absolute error under a nanosecond) and one above 1e7 ms in the highest, and `GetClamped()` counts both. NaN, infinite and negative values are rejected and counted by `GetRejected()`; they never reach a statistic. The memory of a series is fixed: the sketch, the 512 exact samples and the 256-sample ring come to about 18 KB whatever the count.

**Measured accuracy** (`programs/regression/telemetry_sketch.obs`: 20,000 samples per distribution from a fixed-seed generator, error against the exact sorted value at the same rank, quantiles 0, 0.5, 0.9, 0.95, 0.99, 0.999 and 1):

| data | stated error | worst measured error |
|------|--------------|----------------------|
| uniform 0.1 to 10 ms | 1 % | 0.66 % |
| uniform, sketch at 0.5 % | 0.5 % | 0.49 % |
| lognormal (median 2 ms, sigma 0.8) | 1 % | 0.87 % |
| bimodal (90 % near 2 ms, 10 % near 80 ms) | 1 % | 0.99 % |
| constant 3.7 ms | 1 % | 0 (exact: the clamp to min and max) |
| log-uniform over twelve decades (1e-5 to 1e7 ms), 30,000 samples | 1 % | 0.65 % |
| 1 to 9 samples, forced through the sketch | 1 % | 0.55 % at most |

The worst case in each row is below the bound by construction and the test fails if any single quantile exceeds it; the 0.99 % row is the sketch working at its limit, which is what a relative-error sketch does on a heavy upper tail. Merging two sketches gives bucket counts identical to the sketch of the union, so the merged quantiles are identical, not just close. Mean and standard deviation are not sketch estimates: they are exact over every sample.

**What the numbers do not tell you.** There are no confidence intervals on a quantile: a p99 from 200 samples is the 198th value and carries the sampling noise of 2 samples above it, and a p99.9 needs at least 1,000 samples to mean anything (below that `GetP999()` is the maximum, by the nearest-rank rule). Samples from one process are not independent (caches, frequency scaling, the collector), so a standard error from the standard deviation understates the uncertainty. **Warm-up is a number you choose**: there is no steady-state detection, and `SeriesStats->New(n)` discards the first n samples and nothing smarter. Compare configurations by running both in the same process, interleaved, as `programs/examples/telemetry_demo.obs` does.

### Resources

A `ResourceSampler` returns a `ResourceReading` (a time on the `NowMs` clock and named `Float` values, NaN for one that could not be read).

| sampler | values | source |
|---------|--------|--------|
| `RuntimeSampler` | `rss_bytes`, `rss_peak_bytes`, `heap_allocated_bytes`, `cpu_time_ms`, `cpu_percent`, `cpu_count`, `threads_active`, `threads_parked`, `gc_minor_count`, `gc_major_count`, `gc_pause_last_us`, `gc_pause_max_us`, `gc_pause_avg_us`, `gc_pause_total_us`, `gc_old_gen_bytes` | the runtime's own `runtime.*` properties (`Runtime->GetProperty`, `core/vm/common.cpp`) |
| `GpuSampler` | `gpu_util_percent`, `gpu_mem_used_mb`, `gpu_power_w`, `gpu_temp_c` | `nvidia-smi --query-gpu=utilization.gpu,memory.used,power.draw,temperature.gpu --format=csv,noheader,nounits` |
| `PowermetricsSampler` | none: interface only, always unavailable | see below |

`cpu_percent` is process CPU time over wall time since the previous reading (100 is one core; a multi-threaded process can exceed it) and is NaN on the first reading. On a machine with no GPU, no driver or no `nvidia-smi`, `GpuSampler` is not an error: `IsAvailable()` is false after the first attempt, `Sample()` returns `Nil` without starting another process, `GetError()` says why, and `Reset()` allows a retry. The command is the same on Windows and Linux (`nvidia-smi.exe` is found through the path like any program); the runtime's command call merges standard error into the output, so a driver warning in front of the data is skipped by the parser and a failing command is told by its status. A board that prints `[N/A]` for a field gives NaN for that field. With several GPUs the constructor's index picks the line. Each `Sample()` starts a process, which takes tens of milliseconds: sample from a `SamplerLoop` once a second, not once per inference.

```objeck
gpu := GpuSampler->New();
estimator := EnergyEstimator->New();
looper := SamplerLoop->New(gpu, 1000);               # every second, on its own thread
looper->SetEnergy(estimator, "gpu_power_w");
recorder := TimingRecorder->New();
if(looper->Start()) {                                # false, and nothing runs, with no GPU
  recorder->AttachSampler(looper);                   # energy and GPU memory per call
};
# ... timed calls ...
looper->Stop();
```

#### Is there a program-visible GC statistics API?

**Yes, partly.** `Runtime->GetProperty("runtime.gc.minor")`, `runtime.gc.major`, `runtime.gc.pause.last_us`, `runtime.gc.pause.max_us`, `runtime.gc.pause.avg_us`, `runtime.gc.promoted.*`, `runtime.gc.old.bytes`, `runtime.gc.nursery.*`, `runtime.gc.stw`, `runtime.alloc.since_gc`, `runtime.threads.active`, `runtime.threads.parked` and the memory and CPU properties are read by `RuntimeSampler` and checked in `telemetry_sampler.obs`. A run that allocates 12 million objects shows 1 minor and 4 major collections, a last pause of 8.9 ms and an average of 9.3 ms.

**The gaps, and the smallest addition for each (none made here):**

1. **No total pause time.** The VM keeps `gc_pause_total_us` (it is how the average is computed, `core/vm/arch/memory.cpp`, `RecordPause`) but does not export it. `RuntimeSampler` derives it as average times count, which is off by up to one microsecond per collection (the average is an integer). The smallest change is one accessor, `GetPauseTotalUs()` in `memory.h`, and one row, `runtime.gc.pause.total_us`, in the `kStats` table of `GetRuntimeStat` in `core/vm/common.cpp`: four lines.
2. **No pause distribution.** The percentiles exist only behind `OBJECK_GC_STATS=1`, printed to stderr at exit, from the first 1,048,576 pauses. A program cannot ask for the p99 pause. The smallest change is a fixed array of 64 atomic counters, one per power-of-two microsecond bucket, incremented in `RecordPause` beside the `fetch_add` of the total, and rows `runtime.gc.pause.p50_us`, `p99_us` and `p999_us` computed from it on read (bucket resolution, a factor of 2; a finer histogram is the same code with more buckets). This is the one the thesis's tail-latency claims need.
3. **No safepoint wait.** The stop-the-world watchdog (`OBJECK_STW_WATCHDOG_MS`) knows which mutators a collection is waiting for, but the wait itself is not a property. A `runtime.gc.stw_wait_us` (the time from the collector owning the lock to the last mutator parking) is one more `steady_clock` read in the same function.
4. **A pause is measured from the moment the collector owns the lock**, so it includes the stop-the-world handshake; there is no split between the handshake and the collection.
5. **No per-thread CPU time and no accelerator memory.** `runtime.cpu.time` is for the process, and the VM does not account device memory, so GPU memory comes from `nvidia-smi` alone.

Collector counters belong in a profile as before and after readings around a run, not as a series: take a `RuntimeSampler` reading before and after and subtract.

#### Energy

`EnergyEstimator` integrates sampled power with the trapezoid rule: `AddSample(seconds, watts)`, `GetJoules()`, `JoulesBetween(t_from, t_to)`, `GetMeanWatts()`. **It is an estimate.** The input is itself a sensor reading (`power.draw` is the board power, averaged by the driver over a window of about a second on many boards, which the estimator cannot see), the samples are discrete and the power between two of them is assumed linear. Two samples may share a time, which is a step in the power: an interval that starts on the step starts at the power after it, and one that ends there ends at the power before it. A workload much shorter than the sampling interval has no resolution at all, and the energy attributed to one call by `AttachSampler` is the average board power over the call (the last reading is held past the last sample) times its duration: a share of the board's draw, which includes whatever else runs on the GPU, not a measurement of the call. For a CPU run, GPU power says nothing and the example does not sample it. Tests check the integration against traces with known integrals (constant 100 W for 10 s is 1,000 J, a linear ramp is exact, a trapezoid trace, partial intervals, a gap longer than `SetMaxGap` skipped and counted, refused out-of-order samples); they cannot check the sensor.

**Apple Silicon.** `powermetrics` (`sudo powermetrics --samplers cpu_power,gpu_power -i 1000`) needs root, and a library that asks for root behind the program's back is not acceptable, so `PowermetricsSampler` is the interface only and reports itself unavailable everywhere. A program that runs as root can implement `ResourceSampler` itself, parse the CPU, GPU and ANE power lines (milliwatts) and feed an `EnergyEstimator`. Nothing of that is tested here: no Apple hardware was used.

### Profiles

`ProfileStore` aggregates by model, device, batch and tag. `TimingRecorder->GetStore()` is the one a recorder fills; `ProfileStore->New()` makes one you fill with `Record`.

```objeck
store := recorder->GetStore();
store->Save("resnet34.profile.json");                 # lossless
merged := ProfileStore->Load("resnet34.profile.json");
merged->Merge(other_machines_profile);                # cell by cell, as if the samples were one series
store->SaveCsv("resnet34.profile.csv");               # a summary, for a spreadsheet

p := merged->Predict("resnet34", "cuda", 8);
if(p = Nil) {
  merged->GetError()->PrintLine();     # "... has no profile at batch 8 (observed batches: 1, 32)"
}
else {
  "p50 {$p->GetP50()} ms, p95 {$p->GetP95()} ms, {$p->GetHostMemory()} bytes, {$p->GetJoulesPerRun()} J"->PrintLine();
};
```

`Predict` returns **what was observed** in that cell: the p50, p95 and p99 latency, mean, extremes, the largest process memory seen when a run finished, the largest GPU memory reading, and the mean energy per run when a power source was attached, plus the count and whether the quantiles are exact. A model, device or batch never measured is a `Nil` with the reason in `GetError()` listing what does exist; **a batch is never interpolated**, because latency against batch is not linear (a dynamic-shape model can change kernels between two batch sizes) and an invented number is worse than a miss. A cell that holds only warm-up samples is a miss too.

**JSON is lossless**: the moments, the exact samples of a small series and the whole sketch, so a loaded profile gives the same quantiles and two profiles merge into the profile of the union (`telemetry_profile.obs` checks that the JSON of a loaded profile equals the original's). **CSV is a summary**: count, discarded, min, max, mean, standard deviation, p50, p95, p99, p99.9, memory and energy. A profile loaded from CSV is summary-only: `Predict` works on it, but quantiles of a union cannot be computed from quantiles, so merging a summary-only cell with another gives the exact count, mean and extremes and **NaN quantiles** rather than an invented number. Save JSON if you will merge.

Profiles from different machines or builds are different populations: `Merge` adds them without asking. Put the machine in the model or device name ("cuda:4090") if that matters.

### Contention

`ContentionRunner` runs K threads that each call a function reference N times and records every call.

```objeck
function : Work(worker : Int, call : Int) ~ Nil { ... }     # must not raise

report := ContentionRunner->New(4, 200, MyClass->Work(Int, Int) ~ Nil)->Run();
"{$report->GetThroughput()} calls/s, spread {$report->GetMedianSpread()}"->PrintLine();
all := report->GetCombined();                                # every call of every worker
slow := report->GetWorker(3);                                # one worker's series
```

Workers wait for each other before the first call, record into their own series (no lock in the timed path) and are merged after they all finish; `SetWarmup(n)` discards n calls per worker. `GetMedianSpread()` is the slowest worker's median over the fastest worker's: near 1, the workers saw the same latency; large, the contention was unfair. Run K = 1 first as the uncontended baseline. A lock that is not first come first served makes one worker fast and the others slow, which shows in the spread and in the combined maximum more than in the combined median.

### Verification

Five regression programs (`telemetry_sketch`, `telemetry_stats`, `telemetry_profile`, `telemetry_sampler`, `telemetry_contention`) cover the above with no network and no GPU: sketch error against exact sorting on uniform, lognormal, bimodal, constant and tiny data and over twelve decades; hand-computed statistics, the ring, the warm-up and the exact-to-sketch switch; Predict hits and misses, JSON, CSV and merge; the `nvidia-smi` parser on canned output, the sampler through a real process (`echo`) and its absence; energy integration on traces with known integrals; scripted contention workers. Float comparisons carry a tolerance; the timing tests check lower bounds only (a sleep never returns early), so a busy machine cannot fail them.

`programs/examples/telemetry_demo.obs` runs it offline on a CPU workload. `programs/examples/telemetry_onnx.obs` profiles an ONNX model through the existing `API.Onnx.Session`; on an RTX 4090 machine with ONNX Runtime CPU it measured ResNet-34 at 23 ms (batch 1) and 59 ms (batch 4) at the median over 8 runs after 2 warm-ups, which is a check that it runs, not a benchmark.

### What this is not

It is the host-side latency of a call: for an asynchronous GPU launch it measures the call, not the kernel; it does not use CUDA events, and does not pin threads or lock clocks. It does not decide anything: the policy that uses a profile (placement, pacing) is the next layer. It adds no VM code: the gaps above are the list of what a VM change would give.
