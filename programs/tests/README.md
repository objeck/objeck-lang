# programs/tests

Older test programs, benchmarks and a few standalone reproductions. The suite CI runs on every push is [../regression](../regression/README.md), not this directory (the exceptions: `prgm_http2.obs`, `prgm_http3.obs`, `http3_downgrade_test.obs` and `http_header_echo_test.obs` are compiled on linux-x64 and run by the non-gating network step in `ci-build.yml`, and the programs in `clbg/` and `perf/` are the benchmark sources the perf harnesses use); [../TESTING.md](../TESTING.md) places both in the whole testing picture and [docs/ARCHITECTURE_MAP.md](../../docs/ARCHITECTURE_MAP.md#testing) lists every layer.

## What is here

| Path | What it is |
|---|---|
| `prgm0.obs` and up (327 files, numbered with gaps) | numbered programs covering language features, collections, strings, JSON, networking, encryption, threads, regex. Each is a program to compile and run; there is no expected-output file and no runner |
| `*_test.obs`, `diag_*.obs`, `jit_*.obs`, `mt_alloc_658.obs`, `nlp_test.obs` and similar | single-purpose programs written while chasing a defect or checking a feature (language-server diagnostics, JIT probes, ML and NLP smoke tests). Newer checks belong in `../regression` |
| `clbg/` | six Computer Language Benchmarks Game programs: `binarytrees`, `fannkuchredux`, `fasta`, `mandelbrot`, `nbody`, `spectralnorm` |
| `perf/` | twelve `bench_*.obs` micro-benchmarks (array loops, GC churn, method dispatch, tail calls, CSE and the like) |
| `aoc_2022/`, `aow/` | Advent of Code solutions |
| `data/`, `*.json`, `lorem*.txt` | inputs some programs read; run them from this directory |
| `win32_socket_close_race.cpp` | a plain-sockets C++ reproduction of the Windows loopback data-loss behaviour ([docs/windows_loopback_sockets.md](../../docs/windows_loopback_sockets.md)); no Objeck involved |
| `test_gen_collect_fixes.sh`, `build.sh` | a shell check of `gen_collect` fixes; `build.sh` recompiles `net` and `net_server` into the deploy tree's `lib/` and optionally builds the program you name |

## Run one

From a built deploy tree or an install whose `bin/` is on `PATH` (and `OBJECK_LIB_PATH` pointing at its `lib/`):

```
cd programs/tests
obc -src prgm10.obs -opt s3 -dest prgm10.obe     # add -lib <libraries> as the program needs, e.g. -lib xml,json
obr prgm10.obe
```

`clbg/` and `perf/` programs run the same way from their own directory, for example `obc -src nbody.obs -opt s3 -dest nbody.obe` in `clbg/` then `obr nbody.obe`.

To measure rather than just run, use the harness in `perf-results/` (`run_benchmarks.sh`); `tools/cicd/check_benchmark_inputs.py` keeps the inputs it uses in step with [docs/performance.md](../../docs/performance.md). Do not compare timings from a machine that is busy with something else.

## What this directory proves, and what it does not

Nothing here has an expected-output file or a pass/fail contract, and no runner judges it: running a `prgmN.obs` and seeing no crash is the only check, and many print results a person has to read. A defect found with one of these programs is only guarded once its reproduction is a self-asserting test in `../regression`. `jit_probe.obs`, `jit_repro.obs` and `jit_call_probe.obs` are measurement and reproduction aids (the last is the call-cost probe behind the figures in [docs/JIT_CALLING_CONVENTION_DESIGN.md](../../docs/JIT_CALLING_CONVENTION_DESIGN.md)), not tests. The benchmarks in `clbg/` and `perf/` are valid only as timing inputs, and their results are not compared with anything unless a harness does it ([docs/performance.md](../../docs/performance.md) states the conditions of the published numbers).

## Which libraries a program needs

Compile errors name the library to add (`Add it with '-lib net_server'`). The libraries and their groups are in [docs/cli_options.md](../../docs/cli_options.md). Programs that need a network, an API key, a database or a display (`prgm102`, `prgm103`, the SDL and ODBC ones) will not run unattended.
