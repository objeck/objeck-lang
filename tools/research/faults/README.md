# Fault-injection reliability harness for concurrent agents and inference

Research infrastructure for hypothesis H6 of the thesis design note ("runtime-managed resources improve
reliability under failure"). It injects seeded faults into N concurrent `API.Agent` agents and M concurrent
ONNX inference workers, measures detection, recovery, containment, hangs, leaks and silent wrongness, and
runs the same scenarios against a minimal Python equivalent. Results and their reading are in
[RESULTS.md](RESULTS.md); raw per-repetition rows are in [results.csv](results.csv).

Nothing here edits a library. No model or binary is committed: the ONNX model is the tiny BERT classifier
already tracked at `programs/regression/data/bert/classifier/bert_one_label/model.onnx`.

## Files

| File | Role |
|---|---|
| `fault_agents.obs` | N agent threads, R units each (`Agent->Run` over a 3-call script), in-process faults |
| `fault_infer.obs` | M worker threads sharing one `Session` (plain `Run` and `BoundSession`), inference faults |
| `fake_llm.py` | loopback Ollama-shaped server with seeded protocol and network faults (500, garbage, truncation, delay, hold, drop, RST, slow-loris) |
| `faultlib.py` | the seeded schedule (MINSTD hash of seed, agent, run), shared with the Objeck programs |
| `baseline_agents.py`, `baseline_infer.py` | Python equivalents (threads, `onnxruntime`), same arguments and output lines |
| `clockshim.c` | `LD_PRELOAD` shim that steps the REALTIME clock on SIGUSR1 (no change to the machine clock) |
| `run_faults.py` | the driver: spawns children in their own process group under a watchdog, samples `/proc`, applies process-level faults, writes CSV |
| `analyze.py` | CSV to Wilson-interval tables |

## Fault classes and their definitions

Only every 4th agent or worker (`id % 4 == 0`) is a fault target ("victim"); the others are bystanders whose
behaviour measures containment. A victim unit is faulted with probability `rate` (0.5 by default), decided by
`faultlib.faulted(seed, agent, run, rate)`; the same function is implemented in Objeck, so the schedule does
not depend on thread interleaving.

| Class (`fault`) | Precise definition | Where |
|---|---|---|
| `http500` | the unit's first model call gets HTTP 500 with a JSON error body | agents, server |
| `garbage` | 200 with a non-JSON body | agents, server |
| `truncjson` | 200 with the first half of a valid body, clean close | agents, server |
| `wrongshape` | 200 with valid JSON lacking `message` | agents, server |
| `delay` | the correct answer after 2 s | agents, server |
| `hold` | request read, no answer, connection held open | agents, server |
| `drop` | connection closed with no bytes written | agents, server |
| `truncate` | headers and half the body written (Content-Length promises more), then close | agents, server |
| `rst` | TCP RST (`SO_LINGER` 0) instead of an answer | agents, server |
| `slowloris` | the correct answer, one byte per 10 ms | agents, server |
| `tool_exception` | the unit's first tool call raises an error | agents |
| `tool_fault` | the handler divides by zero (a runtime fault, not a raised error) | agents |
| `tool_timeout` | the handler takes 250 ms against a 100 ms limit (limit set only in the `limited`/`guarded` configs) | agents |
| `tool_huge` | the handler returns 16 MB of text (the loop caps results at 20000 characters) | agents |
| `tool_hang` | the handler never returns | agents |
| `cancel_coop` | a cancel request fires 100 ms into a tool call; the tool polls the flag | agents |
| `cancel_uncoop` | same, but the tool sleeps 1 s without polling; a wrapper backend refuses the next model call | agents |
| `thread_fault` | a separate thread divides by zero outside any `try` while agents run (Python: `ZeroDivisionError` in a thread) | agents, infer |
| `thread_exit` | a separate thread calls `Runtime->Exit(7)` (Python: `os._exit(7)`) | agents, infer |
| `bad_shape` | an input of the wrong shape or length (`[2,5]` ids against `[2,6]` mask; a 5-element upload into a 12-element device tensor) | infer |
| `close_inflight` | `Session->Close()` 25 ms in, while workers run plain and bound calls and hold live `DeviceTensor`/`BoundSession` objects | infer |
| `oom_burst` | a thread allocates 64 MB blocks and keeps them, under `RLIMIT_AS` 14 GB, until refused | infer |
| `gpu_oom` | `gpu_mem_limit=256 MB` on a CUDA session, a thread allocates 8 MB `DeviceTensor`s until refused | infer, CUDA |
| `sigkill`, `sigterm` | signal sent 2 s into a long run; then a restart is timed and checked | process |
| `stdin_closed` | stdin is closed (empty, or after two lines) on a program that reads prompts from it | process |
| `disk_full` | each agent's log path is a symlink to `/dev/full` (every write fails with ENOSPC) | process |
| `clock_step` | the REALTIME clock steps +3600 s or -3600 s mid-run, with a 100 ms tool limit in force | agents |

Not covered, and why: a hung GPU kernel and a driver crash cannot be injected from user space on this
machine; a worker thread cannot be killed from outside (Objeck has no thread kill; `thread_fault` and
`thread_exit` are the two ways a thread ends abnormally); true device exhaustion was not induced
(`gpu_mem_limit` stands in, so that other work on the shared GPU is not disturbed).

## Metrics (CSV columns)

Per repetition: `rc` (negative = killed by that signal), `watchdog_killed`, `stuck` (agents or workers that had not
finished at the program's own deadline), `by_*` (bystander units: expected, seen, ok, max ms), `inj_*` (victim units:
expected, seen, detected, recovered = completed with a correct answer after a reported failure, absorbed = completed
with no failure reported, error = run ended in error, lost = never finished), `silent` (a result the loop called ok
that differs from what a healthy tool returns, or a completed run with the wrong answer, or an inference result that
differs from the single-threaded reference), `threads_peak/last`, `fds_last`, `rss_peak_mb` (sampled from `/proc`),
`gpu_delta_mb` (`nvidia-smi` used memory 1.5 s after exit minus before start; other users of the GPU add noise),
`load1` (the 1-minute load average, `uptime`, at the end of the run), `leftover_procs` (members of the child's process
group still alive after it exited).

Containment (run level) = every bystander unit finished with the correct answer and none was slower than
3 x the control's 95th percentile of per-run maximum + 100 ms.

## Reproducing

Everything below was run in WSL2 Ubuntu 24.04 on an AMD Ryzen 9 7950X3D with an RTX 4090 (CUDA ONNX Runtime
1.24.2 from `~/ortgpu-1.24.2/lib`; the Python baseline uses CPU `onnxruntime` from a venv).

```sh
# 1. build the toolchain from this branch (obc, obr, libobjk_onnx) and make a deploy tree $FD
cd core/compiler && cp make/Makefile.amd64 Makefile && make -j4 OBJECK_LIB_PATH=///".///"
cd ../vm        && cp make/Makefile.amd64 Makefile && OBJECK_DEPS=$HOME/objeck-deps/linux-x86_64 make -j4
cd ../lib/onnx/eq && ORT_ROOT=$HOME/ortgpu-1.24.2/lib sh ./build.sh cuda
mkdir -p $FD/bin $FD/lib/native
cp core/compiler/obc core/vm/obr $FD/bin; cp core/lib/*.obl core/lib/*.ini core/vm/misc/*.pem $FD/lib
cp core/lib/onnx/eq/libobjk_onnx.so $FD/lib/native

# 2. compile the harness
export OBJECK_LIB_PATH=$FD/lib LD_LIBRARY_PATH=$HOME/ortgpu-1.24.2/lib
cd tools/research/faults
$FD/bin/obc -src fault_agents.obs -lib agent,net,json,cipher,misc -dest fa.obe
$FD/bin/obc -src fault_infer.obs  -lib models,onnx,opencv,cipher,json -dest fi.obe
gcc -shared -fPIC -O2 -o clockshim.so clockshim.c -ldl
python3 -m venv ~/faultvenv && ~/faultvenv/bin/pip install onnxruntime numpy

# 3. run (the paths default to the layout above; override with FAULTS_OBR, FAULTS_LIB, FAULTS_ORT_LD,
#    FAULTS_PY, FAULTS_MODEL, FAULTS_BIN)
python3 run_faults.py --list
python3 run_faults.py --suite all --reps 25 --seed 1000 --csv results.csv     # about two hours on a shared machine
python3 analyze.py results.csv > summary.md
```

A single scenario by hand (seed 7, 8 agents, 8 runs each, `tool_exception` on 50% of victim units, scripted model,
no tool limit, 8 s deadline):

```sh
$FD/bin/obr fa.obe 7 8 8 tool_exception 0.5 scripted 0 8
python3 baseline_agents.py --config naive 7 8 8 tool_exception 0.5 scripted 0 8
```

Seeds: repetition `k` of a scenario uses seed `1000 + k`, so any row of `results.csv` can be replayed with the
`seed` column. Hung scenarios are bounded twice: by the harness's own deadline (which prints `HANG` lines and exits
with `Runtime->Exit`) and by the driver's watchdog (SIGKILL of the whole process group 15 s later).
