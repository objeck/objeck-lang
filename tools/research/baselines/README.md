# Cross-runtime baselines: collector stalls beside a blocking inference call

Question (thesis note, hypothesis H1): Objeck PR #1768 parks the calling thread around a blocking
`Session->Run`, and the longest wait of an allocating thread beside a ResNet-34 batch-32 inference falls
from 176.7 ms to 19.9 ms. Is that an Objeck property, or what any managed runtime does? This directory
holds the harness that runs the **same workload** on six hosts and the results. Results and their
reading are in [RESULTS.md](RESULTS.md); raw numbers are in [results/](results/).

Nothing here changes Objeck sources. Models, toolchains and binaries live outside the repository
(`~/baselines` in WSL); only harness sources, scripts and results are committed.

## The workload (identical on every host)

Two threads in one process, both running for a 20 s warm-up and then a 60 s measured window
(statistics are reset at the start of the window):

**Inference thread.** One ONNX Runtime session on `resnet34.onnx` (ONNX model zoo export, 87 MB, input
`data` FP32 `[batch,3,224,224]`, output FP32 `[batch,1000]`). `batch = 32`. Provider `cuda` (CUDA EP with
default options) or `cpu` (CPU EP with default options). Input element `i` is
`float32(((i * 7919) mod 1000) / 1000)`. The thread calls the host's ordinary high-level `Run` back to
back; the time around that call is one latency sample. Whatever conversion or copying the host's API does
per call is part of the host's number (see Threats in RESULTS.md: Objeck's `Session->Run` converts a
64-bit `Float[]` per call and is slower per run than the others for that reason).

**Allocator thread.** A deterministic pseudo-random stream (`s = (s * 1103515245 + 12345) mod 2^31`,
`s0 = 12345`, block size `1024 + ((s / 256) mod 64513)` bytes, so 1 KB to 64 KB, mean about 32.8 KB):

* live set: 8192 blocks allocated up front, about 268 MB, kept reachable the whole run;
* iteration `k`: wait (busy spin on the host's monotonic clock) until `target = previous_end + 100 us`,
  allocate and zero-fill one block, write one byte at each end, and every 16th iteration replace a random
  live-set slot with it (so a steady trickle of survivors reaches the old generation); otherwise the
  block is dropped;
* **stall** = `end_of_iteration - target`, in microseconds (about 1-3 us when nothing interferes).
  A collector pause or a thread-suspension delay lands in exactly one sample. The next iteration's
  target is `end + 100 us` (no catch-up burst, so one pause is one sample, not hundreds).

Reported: stall p50/p95/p99/p99.9/max (index `floor((n-1)p)` of the sorted samples), inference latency
p50/p95/p99/max and throughput (`runs * batch / measured_seconds`), collector counts and pause totals
where the host exposes them, GPU memory (peak `nvidia-smi memory.used` sampled every second, minus the
value before the process started) and a checksum (sum, argmax of sample 0 and the first three logits of
the first run) compared across hosts.

## Hosts and versions

| host | runtime | ORT binding | ORT version | collector configurations measured |
|---|---|---|---|---|
| Objeck | PR #1768 (`feat/onnx-gpu-concurrency`) VM, interpreter+JIT default | `API.Onnx` (`libobjk_onnx.so` built from the branch) | 1.24.2 (NuGet GPU build) | **parked** (the PR) and **unparked** (the same source with the two `APITools_Parked` scopes of `Session->Run` removed; this is the pre-PR behaviour of that call) |
| Java | OpenJDK 21.0.13 | `com.microsoft.onnxruntime:onnxruntime_gpu:1.24.2` (Maven Central) | 1.24.2 | G1 (default), ZGC (default), Parallel; tuned: G1 `MaxGCPauseMillis=10`, ZGC generational, both `-Xms4g -Xmx4g -XX:+AlwaysPreTouch` |
| .NET | .NET 10.0.4 | `Microsoft.ML.OnnxRuntime.Gpu` 1.24.2 (NuGet) | 1.24.2 | Workstation and Server GC, each with concurrent GC on and off; tuned: Server concurrent with `GCgen0size=256 MB` |
| Go | go 1.27.4 | `github.com/yalue/onnxruntime_go` v1.26.0 (cgo) | 1.24.2 shared library | default `GOGC=100`; tuned: `GOGC=400 GOMEMLIMIT=8GiB` |
| Python | CPython 3.12.3 | `onnxruntime-gpu` 1.24.1 (PyPI has no 1.24.2) | 1.24.1 | default (GIL switch interval 5 ms); tuned: 0.5 ms |
| C++ control | g++ 13, `-O2` | ORT C++ API (1.23.2 headers) | 1.24.2 shared library | none: `new[]`/`delete[]`, glibc malloc, no collector |

Machine: see the table in RESULTS.md (WSL2 Ubuntu 24.04, RTX 4090, driver, CUDA 12.6, cuDNN 9). The
Objeck, Go and C++ hosts load the same `libonnxruntime.so` 1.24.2 GPU build
(`~/ortgpu-1.24.2/lib`, from the `Microsoft.ML.OnnxRuntime.Gpu.Linux` 1.24.2 package); Java and .NET
load the copy inside their own package (same version).

### Tuning rule

Every host first runs with its defaults. Then one documented "best practice for latency" setting per
host (the `*-tuned` rows above) is measured; nothing else was tuned and no host got a second round.
The Objeck VM has no equivalent knob that was tried (its nursery is the default 128 MB).

## Reproduce

All commands run in WSL2 Ubuntu. `BL=$HOME/baselines` holds everything that is not in the repository.

```sh
export BL=$HOME/baselines MODEL=/path/to/resnet34.onnx ORT=$HOME/ortgpu-1.24.2/lib   # ORT = dir with libonnxruntime.so (GPU build)
export REPO=/path/to/objeck-lang CUDA_LIB=/usr/local/cuda-12/lib64
mkdir -p $BL/{dl,java,dotnet,go,python,cpp,objeck}
cd $BL/dl
```

1. **Java.** JDK 21 (`java --list-modules` must list `jdk.compiler`).
   `curl -LO https://repo1.maven.org/maven2/com/microsoft/onnxruntime/onnxruntime_gpu/1.24.2/onnxruntime_gpu-1.24.2.jar`
   then `cd $BL/java && mkdir out && java -m jdk.compiler/com.sun.tools.javac.Main -cp $BL/dl/onnxruntime_gpu-1.24.2.jar -d out $REPO/tools/research/baselines/java/Baseline.java`.
2. **.NET.** `curl -sL https://dot.net/v1/dotnet-install.sh -o dotnet-install.sh && bash dotnet-install.sh --channel 10.0 --install-dir $BL/dotnet/sdk`;
   `export DOTNET_ROOT=$BL/dotnet/sdk PATH=$BL/dotnet/sdk:$PATH`;
   `mkdir $BL/dotnet/build && cp $REPO/tools/research/baselines/dotnet/{Baseline.cs,baseline.csproj} $BL/dotnet/build && cd $BL/dotnet/build && dotnet publish -c Release -o out`.
3. **Go.** `curl -LO https://go.dev/dl/go1.27.4.linux-amd64.tar.gz && tar -C $BL/go -xzf go1.27.4.linux-amd64.tar.gz`;
   `mkdir $BL/go/build && cp $REPO/tools/research/baselines/go/main.go $BL/go/build && cd $BL/go/build && $BL/go/go/bin/go mod init baseline && $BL/go/go/bin/go get github.com/yalue/onnxruntime_go@v1.26.0 && $BL/go/go/bin/go build -o baseline .`
   (v1.38.0, the latest, asks for ORT API 31 and refuses to start against 1.24.2: "requested API version [31] is not available".)
4. **Python.** `python3 -m venv $BL/python/venv && $BL/python/venv/bin/pip install onnxruntime-gpu==1.24.1 numpy`.
5. **C++.** `g++ -O2 -std=c++17 -I/usr/local/onnxruntime/include $REPO/tools/research/baselines/cpp/baseline.cpp -L$ORT -lonnxruntime -lpthread -Wl,-rpath,$ORT -o $BL/cpp/baseline`
   (headers 1.23.2, runtime 1.24.2: a newer runtime serves an older API version).
6. **Objeck.** Take a tree at `origin/feat/onnx-gpu-concurrency` with a built VM and compiler (`core/vm/obr`,
   `core/compiler/obc`, deploy `lib/`; `lib_api.h`, `common.h` and `device_tensor.h` of the tree were verified
   identical to the branch tip). Build the library twice against the same ORT 1.24.2:

   ```sh
   # parked: the branch as is; unparked: delete the two scopes in Session::Run's path
   for v in parked unparked; do
     mkdir -p $BL/objeck/src_$v/core/lib $BL/objeck/src_$v/core/vm $BL/objeck/src_$v/core/shared
     cp -r $TREE/core/lib/onnx $BL/objeck/src_$v/core/lib/; cp $TREE/core/vm/*.h $BL/objeck/src_$v/core/vm/; cp $TREE/core/shared/*.h $BL/objeck/src_$v/core/shared/
   done
   sed -i 's/APITools_Parked parked(context);/\/\/ unparked (baseline)/' $BL/objeck/src_unparked/core/lib/onnx/eq/common.h   # 2 occurrences, both in the Session::Run path
   for v in parked unparked; do (cd $BL/objeck/src_$v/core/lib/onnx/eq && ORT_ROOT=$ORT ./build.sh cpu); done
   # two deploy lib directories that differ only in native/libobjk_onnx.so
   #   lib_A = parked, lib_B = unparked  (symlink every .obl from $TREE/core/release/deploy/lib)
   OBJECK_LIB_PATH=$BL/objeck/lib_A $TREE/core/compiler/obc -src $REPO/tools/research/baselines/objeck/baseline.obs \
       -lib models,opencv,cipher,json,onnx,gen_collect,concurrent -dest $BL/objeck/baseline.obe
   ```

   The `unparked` build leaves the `device_tensor.h` scopes (the `BoundSession` path) alone: this workload
   only calls `Session->Run`.
7. **Run.** Everything is driven by `run_matrix.py` (round-robin over configurations within each repetition,
   so drift is spread evenly; resumable):

   ```sh
   cd $REPO/tools/research/baselines
   MODEL=$MODEL python3 run_matrix.py --ep cuda --reps 10 --warm 20 --measure 60
   MODEL=$MODEL python3 run_matrix.py --ep cpu  --reps 10 --warm 20 --measure 60
   python3 aggregate.py            # results/results.csv, results/summary.csv, results/summary.md
   ```

   The machine is shared with other agents (load average 4 to 30 for hours at a time), so a strictly quiet
   machine was not available for the whole matrix. Before every run the script waits (up to 20 minutes) until
   the CPU used by other processes over an 8 s window is below 3 cores and, for CUDA runs, the 1-minute load
   average is below 6 (for CPU runs our own previous 32-thread ORT run keeps the load average high for about
   two minutes, so only the instantaneous rule is used); after 20 minutes it runs anyway. During each run it
   subtracts the process tree's own CPU time (`getrusage`) from the machine's busy time to obtain the CPU the
   other tenants used while the run was going (`others_cores_during`). A run is labelled **quiet** only if
   others used at most 3 cores during it (under 10% of the 32 hardware threads) and, for CUDA, the 1-minute
   load average at its start was below 4; every other run is labelled `noisy`. `aggregate.py` reports both
   "all runs" and "quiet runs only"; RESULTS.md quotes the quiet subset. Noisy repetitions were re-run in a
   later pass where time allowed (the last attempt that is quiet wins).
   Repetitions: CUDA, 10 per primary configuration and 5 per tuned/bound variant; CPU, 5 per primary
   configuration (not 10: the shared machine made the full matrix take over 12 hours). It also records `nvidia-smi` clocks, pstate, power,
   temperature and utilization before and after each run, and the full `nvidia-smi -q -d CLOCK` at the
   start of each matrix (`results/clocks_*.txt`).

## Files

| file | what |
|---|---|
| `objeck/baseline.obs`, `java/Baseline.java`, `dotnet/Baseline.cs`, `dotnet/baseline.csproj`, `go/main.go`, `python/baseline.py`, `cpp/baseline.cpp` | the harness for each host; each prints one `RESULT {json}` line |
| `run_matrix.py` | the runner (configurations, quiet gating, GPU/CPU recording) |
| `aggregate.py` | raw JSON lines to CSV and Markdown tables |
| `results/raw.jsonl` | every run, with the full command line, environment-visible load and GPU state |
| `results/results.csv` | one row per run (flattened) |
| `results/summary.csv`, `results/summary.md` | one row per host/configuration: median and min-max across repetitions |
| `RESULTS.md` | the reading, the tables, threats to validity, next measurements |
