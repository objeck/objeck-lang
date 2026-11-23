# Objeck Performance

> **Benchmark results and cross-language comparisons**

---

## Benchmark Results

### Test Environment

| | |
|---|---|
| **CPU** | AMD Ryzen 9 7950X3D (16C/32T) |
| **RAM** | 128 GB (62 GB visible in Docker) |
| **OS** | Ubuntu 24.04.5 in Docker (Windows 11 host) |
| **Compiler** | Objeck **v2026.11.3** (`ef26ff969d`, the release tag's commit), built from source with the standard libraries committed at that tag (`core/lib/*.obl`, built by the same compiler), `-opt s3` |
| **Methodology** | 3 runs per benchmark (`BENCH_RUNS`, default 3), median reported; wall-clock, see [Measurement conditions and limits](#measurement-conditions-and-limits) |
| **Date** | **2026-11-22** (unified Docker run) |
| **Previous runs** | 2026-11-07 against v2026.11.0, 2026-09-12 against v2026.9.1, and 2026-06-21 against v2026.6.2 — see Optimization History for what changed between them |

> All tables below come from a **single unified Docker run** (`perf-results/docker/Dockerfile`) on the box above, so the single-language and cross-language numbers are directly comparable. Note Docker's virtualized backend runs Objeck's **interpreter**-bound benchmarks ~30–50% slower than a native (bare-metal/WSL2) build, while JIT/`native` code is nearly unaffected — so the interpreter rows here are conservative.

### Measurement conditions and limits

Everything on this page comes from one harness, `perf-results/docker/run_all_benchmarks.sh`, run inside the image that `perf-results/docker/Dockerfile` builds. Read this section before comparing a number here with one from another source.

**Reproduce.**

```bash
docker build -t objeck-bench -f perf-results/docker/Dockerfile .      # from the repository root
docker run --rm -e BENCH_RUNS=3 -v "$(pwd)/perf-results/docker-results:/results" objeck-bench
```

The image is `ubuntu:24.04`. It builds `obc` and `obr` from the checked-out sources with `make/Makefile.amd64`, copies the committed `core/lib/*.obl` (it does not rebuild them: `tools/cicd/check_obl_versions.py` only requires each `.obl` to carry the toolchain's version number, not that the same compiler commit produced it), and installs Python, Ruby, LuaJIT and `default-jdk-headless` from Ubuntu's packages, which is why the peer versions are the distribution's (Python 3.12.3, Ruby 3.2.3, OpenJDK 21) rather than current releases. The script prints the CPU, core count, RAM, OS and every tool version at the top of its log and writes `system_info.txt`, `objeck_clbg.csv`, `objeck_perf.csv` and `cross_lang.csv` to `/results`.

**What is timed.**

| | Objeck rows | Python, Ruby, LuaJIT, Java rows |
|---|---|---|
| Command | `/usr/bin/time -f "%e %M" obr <file>.obe <input>`, stdout to `/dev/null` | `date +%s%N` before and after the command, stdout to `/dev/null` |
| Result | elapsed wall seconds (resolution 0.01 s) and peak resident set in KB | elapsed wall seconds (millisecond resolution); **no memory figure is recorded** |
| Included | VM start-up and library load | interpreter or JVM start-up |
| Excluded | `obc -opt s3` compilation | `javac` compilation |

Not CPU time, not user time, no warm-up run discarded. Java's 0.24 s for binarytrees therefore includes JVM start-up, which is a visible fraction of a run that short.

**What is not in the repository.** The script appends every run to a CSV and does not compute a median; the medians on this page were taken from those CSVs, and the CSVs of the 2026-11-07 run were not committed (`perf-results/cross-lang/results/cross_lang_results.csv` is a February 2026 Python/Ruby/LuaJIT file). The CSVs of the 2026-11-22 run are committed in `perf-results/runs/2026-11-22/` (every run of every cell, plus `system_info.txt`), so these tables can be recomputed from them; earlier tables can only be re-derived by re-running. No variance, confidence interval or per-run spread is recorded for them; the only spread on this page is the five-run repeat quoted below, whose raw figures are not committed either.

**Inputs.** nbody 50,000,000; binarytrees 17; spectralnorm 5500; fannkuchredux 12; fasta 25,000,000; mandelbrot 4000; `bench_gc_large_heap` 18; `bench_matrix_multiply` 500 (the others take their defaults). `tools/cicd/check_benchmark_inputs.py` keeps `perf-results/run_benchmarks.sh` in step with the tables on this page; it does not read the Docker script, which carries its own copy of the inputs (compared by hand against `run_benchmarks.sh` while updating this page: they agree).

**Peer programs.** The Python, Ruby, LuaJIT and Java sources are in `perf-results/cross-lang/benchmarks/`. `binarytrees.java` describes itself as single-threaded; the other peer sources were not individually checked for threading. Peers run with their default flags: no JIT or GC tuning, no `-Xmx`, no LuaJIT options.

**Limits.**

- One machine, one container configuration, three runs per cell. A 6-17% shift of the whole table between two passes on the same host (below) is larger than most differences the tables are used to argue, so rows within about 10% of each other should be read as ties.
- Five of the six Objeck CLBG programs (all but `spectralnorm`) mark hot methods `native`, an opt-in with no counterpart in the peer programs (`grep native programs/tests/clbg/*.obs`); the comparison is between each language's idiomatic or annotated version, not between compilers given identical source.
- Six CLBG programs and twelve optimizer-targeted micro-benchmarks written by the project. These tables measure no string, I/O, regex, collection-library, multi-threaded or start-up-latency workload (a first set of library timings is under Library workloads below), and none of the numbers says anything about compile time.
- Peak RSS is recorded for Objeck only, so no statement about a peer's memory in this page rests on a measurement.
- The claim that Docker slows Objeck's interpreter-bound rows by 30-50% against a native build is quoted from earlier notes; the repository holds no same-OS native-versus-Docker pairing that supports it. For statistically controlled comparisons (ABAB interleaving, 15 runs, p-values) see [perf-results/baseline-2026.10.0/README.md](../perf-results/baseline-2026.10.0/README.md), which is Apple M4 Max and compares two Objeck builds, not languages.

### CLBG Benchmarks

Classic [Computer Language Benchmarks Game](https://benchmarksgame-team.pages.debian.net/benchmarksgame/) programs compiled with `-opt s3`.

| Benchmark | Input | Time (s) | Peak RSS |
|-----------|-------|----------|----------|
| **mandelbrot** | 4000 | 0.77 | 10 MB |
| **nbody** | 50M | 8.15 | 8 MB |
| **binarytrees** | 17 | 2.03 | 177 MB |
| **fannkuchredux** | 12 | 33.24 | 8 MB |
| **spectralnorm** | 5500 | 2.91 | 9 MB |
| **fasta** | 25M | 6.86 | 9 MB |


> Measured: **v2026.11.3** (`ef26ff969d`), 2026-11-22 unified Docker run on the host above.

**mandelbrot** and **nbody** benefit from `native`-annotated methods that JIT-compile to x64. **binarytrees** benefits from the young-gen bump allocator and auto-JIT for methods containing `MTHD_CALL`.

**Against the previous pass (2026-11-07, v2026.11.0) every row but mandelbrot is 8–10% faster, and against September (v2026.9.1) the rows are within 7% either way.** Against 2026-11-07: nbody 9.02 → 8.15 s, binarytrees 2.26 → 2.03 s, fannkuchredux 36.01 → 33.24 s, spectralnorm 3.17 → 2.91 s, fasta 7.56 → 6.86 s, mandelbrot 0.77 → 0.77 s (one of its three runs read 1.89 s; the other two 0.77 and 0.72 s). Against September: nbody 8.14 → 8.15 s, binarytrees 2.14 → 2.03 s, fannkuchredux 32.76 → 33.24 s, fasta 7.07 → 6.86 s, spectralnorm 2.72 → 2.91 s (+7%), mandelbrot 0.72 → 0.77 s (+7%). This reads as the host returning to the state it was in in September, not as an engine speedup. The hand-`native` control in the micro-benchmark table, which the page treats as immune to interpreter changes, read 0.42 s on 2026-11-07 and reads 0.38 s here, the September figure. The changes between v2026.11.0 and v2026.11.3 were compile speed, a collector crash fix and libraries, none aimed at run-time throughput, and nothing here attributes a speedup to them. It also matches the five-run repeat taken on 2026-11-07, after the host had settled (spectralnorm 2.91–2.98 s, nbody 8.25–8.49 s, binarytrees 1.95–2.08 s). The residual +7% on spectralnorm against September has not been attributed to a cause. The CI perf gate is report-only (it cannot fail the build) and its baseline ratios predate the F7 calling convention ([PERF_GATE.md](../perf-results/PERF_GATE.md)), so it is not evidence either way. Read the table as "unchanged, ranking unchanged", with one run per cell set by three runs and a host that moves by 6–17% between passes.

**binarytrees peak RSS is 177 MB**, unchanged from 2026-11-07 (209 MB in September). It is still an order of magnitude above every other row, so allocation memory remains the standing gap.

**fannkuchredux** is call-bound in structure, but its cost is dominated by the permutation/flip inner loop rather than call overhead, which is why the calling-convention work in v2026.9.1 did not move it (32.32 s in June, 32.76 s in September, 36.01 s on 2026-11-07, 33.24 s here).

**Since June** (v2026.6.2, the comparison the page used to lead with): spectralnorm 44.86 s → 2.91 s (15.4x) and binarytrees 8.53 s → 2.03 s (4.2x). Those are the v2026.9.1 changes, measured again on a later build.

---

## Cross-Language Comparison

All five languages measured in **one Docker run on identical inputs** (the mandelbrot and fasta rows were timed in a later pass, described below the table) (`perf-results/docker/Dockerfile`), so these times are directly comparable to each other. Median of 3 runs.

| | |
|---|---|
| **Objeck** | **v2026.11.3** (`ef26ff969d`), libraries committed at that tag, `-opt s3` |
| **Python** | 3.12.3 (CPython) |
| **Ruby** | 3.2.3 |
| **LuaJIT** | 2.1 (tracing JIT) |
| **Java** | OpenJDK 21.0.13.1 (HotSpot, tiered JIT) |
| **Host** | AMD Ryzen 9 7950X3D (16C/32T), Ubuntu 24.04.5 in Docker (32 vCPU / 62 GB) |

> Same unified Docker run as the single-language tables above, so every number on this page is directly comparable. As noted, Objeck's *interpreter*-bound benchmarks (e.g. spectralnorm) would run ~30–50% faster on a native build; JIT/`native` code is essentially unaffected by Docker.

| Benchmark | Input | Objeck | Python 3.12 | Ruby 3.2 | LuaJIT 2.1 | Java 21 | Best |
|-----------|-------|--------|-------------|----------|------------|---------|------|
| **nbody** | 50M | 8.15s | 144.46s | 203.60s | 4.29s | **2.29s** | Java |
| **binarytrees** | 17 | 2.03s | 3.55s | 3.46s | 3.53s | **0.24s** | Java |
| **spectralnorm** | 5500 | 2.91s | 126.57s | 85.13s | **1.12s** | 1.18s | LuaJIT |
| **fannkuchredux** | 12 | 33.24s | 422.92s | 1148.69s | 116.46s | **20.50s** | Java |
| **mandelbrot** | 4000 | 0.98s (3 threads) / 1.90s (1 thread) | 33.20s | 34.58s | 0.73s | **0.70s** | Java |
| **fasta** | 25M | 6.77s | 31.31s | 31.65s | 6.89s | **3.48s** | Java |

**mandelbrot and fasta were added on 2026-11-22.** They were timed in a separate pass from the four rows above, on the same image and host: Objeck alone first, then Python, Ruby, LuaJIT and Java together in four containers at once, each single-threaded on its own core. Run that way the peers' times may carry a few percent of shared-host noise; the gaps below are far larger. Every peer program was checked against Objeck's output before timing: fasta is byte-identical across all five languages at n=1000, and for mandelbrot the four peers match each other byte for byte and Objeck's digits match theirs at n=64, 200 and 203. Objeck's shipped `mandelbrot.obs` runs **three threads** and prints each byte as decimal text through a per-byte `Print`, where the peers are single-threaded and write the packed PBM in bulk, so the row gives both Objeck figures: a one-thread build (the same source with `Thread->New[1]`, made for this table) is the like-for-like one. Fasta is single-threaded in every language. Peak RSS was not measured for these two rows.

### Reading the table

- **Java (HotSpot) wins five of six in this run, and LuaJIT takes spectralnorm.** LuaJIT is 5% ahead there (1.12s vs 1.18s); on 2026-11-07 Java led it by 0.02s and in September LuaJIT led by 0.06s, so that row is a tie that flips between runs, not a change. A mature tiered JIT with escape analysis and a generational GC is the bar a younger JIT is measured against.
- **Objeck beats Python and Ruby on all six:** nbody **17.7x** / **25.0x**, spectralnorm **43.5x** / **29.3x**, fannkuchredux **12.7x** / **34.6x**, binarytrees **1.7x** / **1.7x**. These ratios are 5–15% higher than on 2026-11-07 because Objeck's own times are lower in this pass (see above); Python's and Ruby's own times also moved, CPython nbody 148.57s → 144.46s and Ruby nbody 213.96s → 203.60s, which is more evidence of run-to-run drift on this host.
- **mandelbrot: Objeck is 34x / 35x faster than CPython / Ruby on three threads and 17x / 18x on one, but LuaJIT and Java beat it.** Objeck 0.98s (three threads; runs read 1.78, 0.70 and 0.98s, so the median sits in a spread) and 1.90s on one thread, against LuaJIT 0.73s and Java 0.70s on one thread each. On one thread that is LuaJIT 2.6x and Java 2.7x faster; Objeck's three-thread run only just trails them. Part of Objeck's time is its output path (per-byte `Print` of decimal text), which the peers do not pay.
- **fasta: Objeck ties LuaJIT, is 4.6x / 4.7x faster than CPython / Ruby, and Java is 1.9x faster than Objeck.** 6.77s against LuaJIT 6.89s (Objeck 2% ahead, inside the run-to-run spread), Python 31.31s, Ruby 31.65s, Java 3.48s. This is the same program shape on every side: the same random generator, linear cumulative selection and 60-column lines.
- **Objeck beats LuaJIT on fannkuchredux** (33.24s vs 116.46s, **3.5x faster**) — the integer-array permutation/flip pattern is a poor fit for LuaJIT's tracing JIT and suits a method JIT well. September measured 3.6x and June 3.8x.
- **binarytrees is not Objeck's weak spot on time.** It leads CPython (1.7x), Ruby (1.7x) and LuaJIT (1.7x). Java is still **8.5x** faster (0.24s vs 2.03s), and Objeck's **177 MB** peak RSS against 8-10 MB on its own other CLBG rows is where its remaining gap lives (peers' peak RSS is not recorded by the harness, so no peer figure is claimed). Allocation throughput stays the clearest improvement target (P1), argued on memory rather than wall clock.
- **spectralnorm's warmup story is over.** June's 44.86s at the default threshold is now 2.91s here (2.72s in September), better than the 3.3s June reached by forcing compilation. The v2026.9.1 calling convention removed the warmup gap rather than narrowing it, so there is no threshold left to tune.
- **fannkuchredux did not move** with the calling-convention work (32.32s → 32.76s → 33.24s). Its inner loop, not call overhead, is what costs. Java is **1.6x** faster on it.

### Key Takeaways

1. **Against scripting peers, Objeck is decisively faster.** nbody is **17.7x** faster than CPython and **25.0x** faster than Ruby; spectralnorm **43.5x** and **29.3x**; fannkuchredux **12.7x** and **34.6x**.
2. **Objeck beats LuaJIT on fannkuchredux** (33.24s vs 116.46s, **3.5x faster**) — the integer-array permutation/flip pattern suits a method JIT better than a tracing one. Java still leads it by 1.6x.
3. **binarytrees is not the weak spot on time.** It beats CPython, Ruby and LuaJIT (1.7x each). Java remains 8.5x faster, and Objeck's **177 MB** peak RSS (209 MB in September; no peer's RSS was measured) is where the gap now lives; allocation throughput is still the highest-leverage work, but the case rests on memory rather than wall clock.
4. **Closure calls are no longer a category of weakness.** The F7 calling convention took spectralnorm from 44.86s to under 3s at the *default* threshold (2.91s here), beating what June achieved by forcing compilation.
5. **Float loops remain the gap against compiled-class JITs.** nbody trails LuaJIT by 1.9x and Java by 3.6x; spectralnorm trails both by about 2.5x.
6. **Measurement environment matters.** Objeck's interpreter is more sensitive to virtualization than its JIT. This pass ran within about 7% of September's on the same host and 8–10% faster than the 2026-11-07 pass, with the engine design unchanged; native (non-Docker) numbers would be meaningfully better than the Docker numbers shown here.

---

## The `native` Keyword

Methods marked `native` are JIT-compiled to x64 or ARM64 machine code. All other methods run in the interpreter unless auto-JIT compiles them after 10 calls.

> Measured: **v2026.11.3** (`ef26ff969d`), 2026-11-22 unified Docker run.

| spectralnorm | Input | Time | Notes |
|-------------|-------|------|-------|
| Auto-JIT, default threshold | 5500 | **2.91s** | v2026.6.2 took 44.86s here; v2026.9.1 measured 2.72s; 2026-11-07 measured 3.17s |
| `bench_spectralnorm_native` (hand-`native`) | 2000 | 0.38s | June 0.37s, v2026.9.1 0.38s, 2026-11-07 0.42s; it moves with the host, see above |

Methods marked `native` that contain `MTHD_CALL` are JIT-compiled via `ProcessStackCallback`. Auto-JIT also compiles hot methods automatically (default: after 10 calls). Closure/function-reference calls (`DYN_MTHD_CALL`) are auto-JIT'd as well, and since v2026.9.1 a compiled caller enters a compiled callee's native entry directly, with an inline cache at `virtual` and func-ref sites. A closure-heavy kernel therefore reaches native-level speed at the **default** threshold: spectralnorm no longer needs `OBJECK_JIT_THRESHOLD=1` to be fast, and the hand-`native` kernel is no longer meaningfully ahead of it.

---

## Micro-Benchmarks

Targeted benchmarks for specific optimization patterns (`programs/tests/perf/`). Compiled with `-opt s3`, median of 3 runs.

> Measured: **v2026.11.3** (`ef26ff969d`), 2026-11-22 unified Docker run. The June column is the 2026-06-21 run against v2026.6.2 and the v2026.9.1 column is the 2026-09-12 run; the change is June to now.

| Benchmark | Target | June (s) | v2026.9.1 (s) | Now (s) | Change | Peak RSS |
|-----------|--------|---------:|--------------:|--------:|--------|----------|
| `bench_strength_ext` | Non-power-of-2 multiply patterns | 2.14 | 0.04 | **0.04** | 54x | 8 MB |
| `bench_dead_code` | Unreachable assignments | 1.68 | 0.04 | **0.04** | 42x | 8 MB |
| `bench_copy_prop` | Variable copy chains | 1.19 | 0.03 | **0.04** | 30x | 8 MB |
| `bench_array_intensive` | Sequential array access patterns | 2.40 | 0.08 | **0.08** | 30x | 8 MB |
| `bench_cse` | Common subexpression elimination | 0.25 | 0.01 | **0.01** | 25x | 8 MB |
| `bench_loop_invariant` | Loop-invariant expressions (LICM) | 0.24 | 0.01 | **0.01** | 24x | 8 MB |
| `bench_matrix_multiply` | Nested loop float computation (n=500) | 4.82 | 0.28 | **0.28** | 17x | 15 MB |
| `bench_method_dispatch` | Repeated method calls on objects | 2.96 | 0.29 | **0.30** | 9.9x | 8 MB |
| `bench_gc_churn` | Rapid short-lived object allocation | 0.64 | 0.15 | **0.14** | 4.6x | 136 MB |
| `bench_gc_large_heap` | Large live set, GC sweep time | 0.07 | 0.02 | **0.02** | 3.5x | 42 MB |
| `bench_tco` | Tail-recursive accumulator (TCO, n=1M×200) | 0.33 | 0.11 | **0.11** | 3.0x | 8 MB |
| `bench_spectralnorm_native` | Float arrays with `native` JIT (n=2000) | 0.37 | 0.38 | 0.38 | +3% | 8 MB |

The two fastest rows (`bench_cse`, `bench_loop_invariant`) now sit at 0.01 s,
close enough to the harness's resolution that their ratios should be read as
"too fast to distinguish" rather than as precise multiples.
`bench_spectralnorm_native` is the control: it is hand-`native`, so it was
already compiled before this work and read 0.38 s in September. It read 0.42 s on
2026-11-07 and reads **0.38 s** now, so the control moved with the host and not with
the engine: the other rows moved by a similar fraction between those two passes. A
control that moves is what makes the other rows credible. Because its kernel is
`native`-annotated it is *always* compiled and never runs through the interpreter,
so it is valid for comparing one build against another but must never be read as an
interpreter measurement.

The June column's environment (host, harness, run count) is not recorded on this page beyond its date and version; treat June-to-now ratios as order-of-magnitude, not as controlled comparisons. The v2026.9.1 column is the 2026-09-12 Docker run at `f6b3b09046`. Times are `/usr/bin/time` wall seconds with 0.01 s resolution, which is why the 0.01 s rows cannot be told apart and why a "+3%" on a 0.38 s row is a 0.01 s difference. "Peak RSS" is the maximum resident set of the `obr` process in the current run.

### Library workloads

Timings of library code rather than compiler or VM patterns. Measured 2026-11-22 on the Ryzen 9 7950X3D (Windows, JIT on, libraries built with `-opt s3`, the v2026.11.2 compiler), **one run per cell** and not interleaved, so a difference under about 10% is not a result. The programs were scratch benchmarks, not files in this repository; the data are generated from a fixed integer generator, so they can be rewritten. "Before" is the library as released in v2026.11.2, "after" is v2026.11.3.

**Regular expressions** (`Query.RegEx`, `Find` over 680,000 characters of text):

| Pattern | Before (s) | After (s) |
|---|---:|---:|
| `[a-z]+-[0-9]+\.[0-9]` (class first, 8,000 matches) | 0.546 | 0.043 |
| `[a-z]+@[a-z]+` (no match) | 0.510 | 0.043 |
| `(foo\|bar\|crossenv\|numpy)-?[0-9]*` (32,000 matches) | 0.988 | 0.090 |
| twelve alternatives under `*`, then `z` | 1.463 | 0.060 |
| `[0-9]+\.[0-9]+\.[0-9]+\.[0-9]+` | 0.296 | 0.011 |
| `\w+` (128,000 matches) | 0.423 | 0.093 |
| `ReplaceAll` of `[a-z]+-[a-z]+` | 0.451 | 0.062 |

**Exact vector search** (`VectorIndex`, 100,000 vectors of 128 values, L2, top 10, 500 queries, one query at a time):

| Threads (`SetThreads`) | 1 | 2 | 4 | 8 | 16 | 32 |
|---|---:|---:|---:|---:|---:|---:|
| Queries/s | 66 | 81 | 132 | 229 | 315 | 298 |

For comparison, numpy (BLAS) on the same shape does 517 queries/s one at a time and 840 batched. Searching with 32 threads on a plain scan that is spread over queries (not inside one query) reaches 789 queries/s, so thread start-up per query is part of the gap. An inverted-file (IVF) index prototype on this data, which has uniform noise in every dimension and is the worst case for it, reached recall@10 of 0.93 only after scanning a quarter of the lists, at 85 queries/s; no approximate index is shipped.

**Nearest neighbors and edit distance:**

| Workload | Before | After |
|---|---:|---:|
| `KNearestNeighbors->Query`, 50,000 x 16, k = 5, 200 queries | 15.06 s | 0.057 s |
| `LevenshteinDistance`, 20,000 pairs of 38 characters | 0.683 s | 0.118 s |

**Measured and already fast** (1,005,000 characters of text; a 789 KB JSON file): `Find` of 15,000 occurrences 7 ms, `ReplaceAll` 12 ms, `Split` into 165,001 parts 70 ms, `ToLower` plus `ToUpper` 6 ms, 20,000 `s += ...` 35 ms (20,000 `Append` calls 74 ms), `JsonParser` parse 34 ms, `ToString` of the parsed document 131 ms, DBSCAN on 4,000 points of 8 values 0.20 s, a Gaussian mixture over 5,000 points of 4 values 0.05 s.

**Not measured:** `query.obs`, `net_server`, ODBC, the tree and SVM learners, matrix multiplication in pure Objeck, and any ARM64 or Linux result.

### Running Benchmarks

```bash
# Docker (recommended for reproducible results)
docker build -t objeck-bench -f perf-results/docker/Dockerfile .
docker run --rm -v "$(pwd)/perf-results/docker-results:/results" objeck-bench

# Linux/WSL (using deploy directory)
bash perf-results/run_benchmarks.sh <deploy_dir> <output_dir> [num_runs]
```

`run_benchmarks.sh` takes a deploy tree (`<deploy_dir>/bin/obc` and `obr`), runs the six CLBG programs and the twelve micro-benchmarks `num_runs` times each (default 10, not 3), and writes `<output_dir>/results.csv` (`benchmark,run,time_seconds,peak_rss_kb`). It times with GNU `/usr/bin/time -f "%e %M"`, so it runs on Linux and WSL only; for macOS and Windows, and for statistical comparison of two builds (interleaved runs, warm-ups discarded, p-values), use `perf-results/objeck_bench.py` (see [perf-results/baseline-2026.10.0/README.md](../perf-results/baseline-2026.10.0/README.md)). The cross-language comparison needs the Docker image above, or `perf-results/cross-lang/run_native.sh` for the CI-scaled Java and LuaJIT subset ([PERF_GATE.md](../perf-results/PERF_GATE.md)). Do not time anything on a machine that is busy with a build or a test run: this page's own notes show a pass taken that way drifting by 6-17%.

---

## Optimization History

| Version | Date | Key Optimization | Impact |
|---------|------|-----------------|--------|
| Pre-2024 | -- | Stack-based VM, interpreter-only | -- |
| v2024.x | 2024 | JIT compilers (ARM64 + x64), basic bytecode optimizer | -- |
| v2026.2.0 | Feb 2026 | O(1) GC lookups, ARM64 JIT optimizations, instruction rewrite framework | Foundation |
| v2026.2.1 | Feb 2026 | Inline limit 128->256, CSE, dead code elimination | **4.38x nbody** |
| v2026.2.1+ | Mar 2026 | JIT whitelist fix: 3 instructions had code generators but weren't enabled | **28.4x mandelbrot** |
| v2026.3.0 | Apr 2026 | Young-gen bump allocator, MTHD_CALL whitelist, direct JIT-to-JIT calling, atomic mark bits | **2.3x binarytrees** |
| v2026.4.2 | Apr 2026 | JIT local variable register cache, LTO (`-flto=auto`), ARM64 `-mcpu=native` | **~3x all benchmarks** |
| v2026.5.3 | May 2026 | Jump table dispatch for dense integer `select` (O(1) vs O(log n) BST) | select-heavy programs; no regression on existing benchmarks |
| v2026.6.0 | May 2026 | Auto-JIT for MTHD_CALL, 15 new interpreter fast-path opcodes, TCO, LICM | **matrix_multiply −14%, dead_code −15%, array_intensive −12%, binarytrees −7%, mandelbrot −6%** |
| v2026.6.2+ | Jun 2026 | ARM64 forced-JIT correctness hardening (PR #548, #551) | Correctness, not speed — clears every ARM64 JIT miscompile that forced JIT exposes, **unblocking a lower auto-JIT threshold** (the auto-JIT trigger is still 10 calls; these fixes let it drop without miscompiling cold helpers). Full ARM64 suite green at `OBJECK_JIT_THRESHOLD=1`. |
| v2026.8.2 | Aug 2026 | Indexed call results (`GetItems()[0]->Name()`); `obu` updater packaged | Language/tooling, not throughput |
| v2026.8.4 | Aug 2026 | Multithreaded GC fixes; UTF-8 locale correctness | Correctness |
| v2026.9.0 | Sep 2026 | Loopback teardown (`CloseGracefully`), per-thread LSP analysis | Correctness/concurrency, not throughput |
| **v2026.9.1** | **Sep 2026** | **F7 calling convention — a compiled caller builds the callee's frame and enters its native entry directly, with inline caches for `virtual` and func-ref sites; dense `select` → jump table on both backends; `a->Size()` inlined, constant division by multiply, one-instruction array addressing, eight allocatable registers on AMD64** | **bound call 26.5 ns → 5.5 ns; `virtual` call 125 ns → 6.0 ns; `Fib(32)` 0.208 s → 0.043 s (AMD64). M4 Max: call 17.1 ns → 7.4 ns, `Fib(32)` 0.135 s → 0.050 s. Loops 2–8x (array-summing loop 8x, constant division 2.5x)** |

*Conditions for this table:* the machine, input and run count behind the rows before v2026.9.1 are **not recorded** on this page (conditions not recorded); the speedups are quoted from the release notes of the time. The v2026.9.1 row quotes `docs/JIT_CALLING_CONVENTION_DESIGN.md`, where the AMD64 figures are medians of three from `programs/tests/jit_call_probe.obs` alternated with a master binary on one box (the last step, 5.5 ns and 0.043 s, on Windows x64; the CPU model is not recorded there). The M4 Max pair "17.1 ns to 7.4 ns" and "0.135 s to 0.050 s" joins two alternated series in that document: 17.1 ns and 0.135 s are master's figures at the phase-2 entry/exit work (its section 12), and 7.4 ns and 0.050 s are the native-entry result measured against a later master at `3366d92c4a`, whose own figures were 13.8 ns and 0.107 s (its section 13). Against that later master the improvement is 13.8 to 7.4 ns and 0.107 to 0.050 s.

### v2026.3.0 Detail

**Headline: binarytrees (depth=17) from 65.8s to 28.7s.** Young-gen bump allocator enabled with complete call stack fixup.

| Optimization | Category | Impact |
|-------------|----------|--------|
| Auto-JIT operand3 dispatch fix | VM | **1.5x** -- failed JIT methods were re-attempting `Compile()` on every call. |
| MTHD_CALL JIT whitelist (x64 + ARM64) | JIT | **1.2x** -- methods containing method calls can now be JIT-compiled. |
| Direct JIT-to-JIT calling | JIT/VM | Part of MTHD_CALL -- `JitStackCallback` calls callee native code directly, eliminating interpreter trampoline. |
| Atomic mark bits | GC | ~5% -- lock-free CAS replaces mutex across 3 parallel mark threads. |
| MEM_START_MAX 1 MB -> 8 MB | GC | Fewer early GC cycles; `old_generation.reserve` 4096 -> 65536. |
| Young-gen bump allocator | GC | **1.5x** (on top of above) -- `atomic_fetch_add` replaces mutex + hash-set insert. 128MB nursery; short-lived objects die without promotion. Fixed call stack fixup to include top frame pushed by direct JIT-to-JIT calls. |

**How it was found:** GC profiling revealed only 18% of binarytrees runtime was in GC. The remaining 82% was per-object allocation overhead and interpreter dispatch -- contradicting the assumption that "GC is the #1 bottleneck."

### v2026.6.0 Detail

**Headline: broad 5–15% speedup across all interpreter and JIT benchmarks.**

| Optimization | Category | Impact |
|-------------|----------|--------|
| Auto-JIT for MTHD_CALL | JIT | **−7% binarytrees, −6% mandelbrot** — methods containing method calls now auto-JIT after 10 invocations, same `ProcessStackCallback` path as explicit `native`. Previously blocked due to false positives from library method interactions. |
| 15 new interpreter fast-path inline opcodes | VM | **−10 to −15% micro-benchmarks** — comparisons (`EQL/NEQL/LES/GTR/LES_EQL/GTR_EQL`), logical (`AND/OR`), bitwise (`BIT_AND/OR/XOR`), shifts (`SHL/SHR`), `LOAD_CHAR_LIT` added to the inline switch. Eliminates function-pointer dispatch overhead for the most common interpreter instructions. |
| Tail Call Optimization (TCO) | Compiler (`-opt s1+`) | Self-recursive tail calls rewritten to `POP_INT` + param restores + `JMP` — eliminates call-frame growth. Stack overflow at n=1M without TCO; correct in O(1) stack space with it. |
| Loop-Invariant Code Motion (LICM) | Compiler (`-opt s2+`) | Hoists `arr->Size()` reads and pure arithmetic statements out of loop bodies when inputs are loop-invariant. |

### v2026.6.2+ Detail (ARM64 forced-JIT correctness)

**Headline: no benchmark moves, but every ARM64 JIT miscompile that forced JIT (`OBJECK_JIT_THRESHOLD=1`) exposed is now fixed — a prerequisite for lowering the auto-JIT threshold below 10.** These were latent because cold helper methods rarely reach the trigger, so they ran interpreted; forcing JIT surfaced them. Found with the JIT's `_DEBUG_JIT_JIT` codegen disassembly and lldb.

| Fix | Category | Bug |
|-----|----------|-----|
| Cached-local float operands in transcendental/round ops (PR #548) | JIT (ARM64) | `ProcessFloatOperation`/`Operation2`/`Round`/`SquareRoot` read a `REG_FLOAT` operand's register bookkeeping as a stack slot → garbage (e.g. `atan` got 0). |
| libc float result dropped when holder ≠ D0 (PR #548) | JIT (ARM64) | The result move used `move_freg_freg`'s GP-bridge (swapped FMOV opcodes), dropping an FP-only `pow`/`sin`/`exp` result. Now a true `fmov Dd,Dn`. |
| Working-stack regs clobbered across inlined float libc calls (PR #548) | JIT (ARM64) | An inlined `Float->Pow`'s `blr` clobbers caller-saved temps; pending working-stack ints are now spilled across the call (`Sum5`/`Int->Pow`). |
| Unary float-op argument dropped when not in D0 (PR #551) | JIT (ARM64) | Same GP-bridge bug on the *argument* load — `exp` inside `1/(1+exp(x))` got a stale arg, diverging LogisticRegression (`ml_phase1`). |
| `imm19` not masked in error-handler branch backpatch (PR #551) | JIT (ARM64) | A backward (negative) div-by-zero / bounds / null-deref branch sign-extended over the `b.cond` opcode → illegal instruction (`ml_gbt` SIGILL). Now masked to `& 0x7FFFF`. |
| Deferred local load stale after an overwriting store (PR #551) | JIT (ARM64) | A TCO'd `return Gcd(b, a%b)` stored `b:=a%b` before the deferred `LOAD b` was consumed, so `a:=a%b` (GCD→0). `ProcessStore` now materializes pending refs to the slot first. |
| `Int->MinSize()` returned `INT64_MAX` (PR #551) | Library | `2->Pow(63)` computes `+2^63` in float and saturates on F2I; fixed to `1 << 63`. (Not a JIT bug — failed interpreted too; was masked by exit-code-based tests that printed `FAIL` without `Runtime->Exit(1)`.) |

### v2026.9.1 Detail (the calling convention)

**Headline: a call between compiled methods no longer crosses the C++ bridge.**
Before this, a JIT'd method calling another JIT'd method went out through an
eleven-argument bridge entry, took a pooled frame and released it. The caller now
builds the callee's frame on its own stack and jumps straight to its native entry.

| Measurement | Before | After |
|---|---|---|
| bound call (AMD64) | 26.5 ns | **5.5 ns** |
| `virtual` call (AMD64) | 125 ns | **6.0 ns** |
| `Fib(32)` (AMD64) | 0.208 s | **0.043 s** |
| call (Apple M4 Max) | 17.1 ns | **7.4 ns** |
| `Fib(32)` (Apple M4 Max) | 0.135 s | **0.050 s** |

`virtual` and func-ref sites carry inline caches keyed on the receiver's class word
(or the func-ref word); a miss falls back to the resolver, and the slow path also
serves a callee not yet compiled, a full call stack and a `Nil` receiver. A dense
integer `select` compiles to a jump table on both backends.

*Limits:* the call-cost table is a call-bound micro-kernel (`jit_call_probe.obs`, 20M bound calls, 2M `virtual` calls, `Fib(32)` about 7M calls) and says nothing about call-light code; "before" is a master binary of the time, not a release.

**What this meant for the tables above, and what re-measuring found.** The
2026-09-12 run settled it. `spectralnorm` fell **16.5x** (44.86s to 2.72s) and its
"JIT warmup dominates" narrative is gone: the default threshold now beats what
forcing compilation achieved in June. `binarytrees` fell **4.0x** (8.53s to 2.14s)
and now leads all three scripting/JIT peers on time, leaving its 209 MB peak RSS
as the real gap. `fannkuchredux`, despite being call-bound, did **not** move
(32.32s to 32.76s) -- its permutation/flip inner loop dominates, not call
overhead. The hand-`native` spectralnorm kernel was already compiled and comes
back unchanged (0.37s to 0.38s), which is the run's own sanity check.

---

## What We Tried and Reverted

| Optimization | Result | Why |
|-------------|--------|-----|
| Extended strength reduction (x*3,5,7,9,15) | 0.66x slower | Modern CPUs execute MUL in 3 cycles; shift+add has more dispatch overhead |
| Copy propagation | 0.85x slower | Changed patterns the JIT register allocator expected |
| GC lock-free mark via snapshot | 0.64x slower | Copying entire `allocated_memory` set before each mark phase was O(n) |
| Inline limit 512 | 0.91x slower | Exceeded JIT register allocator capacity |
| Auto-JIT MTHD_CALL via interpreter trampoline | 0.5x slower | Per-call trampoline overhead exceeded interpreter cost. Fixed by adding direct JIT-to-JIT calling. |
| ProcessInlineMethod for MTHD_CALL | Miscompiles broadly | Wiring `ProcessInlineMethod` into the MTHD_CALL path fails 36 regression tests. Constructors lose the implicit new-instance result (popped into INSTANCE_MEM, never pushed back) — excluding them clears 24. But the rest persist **even when restricted to pure, instance-memory-free leaf callees**: the inliner's register/working-stack handling miscompiles small methods inlined into high-register-pressure callers (ML/network/collection code). The shared register allocator and working stack don't compose correctly across the inline boundary. Needs a rewrite of `ProcessInlineMethod`, not an incremental fix; left disabled (dead code). Direct JIT-to-JIT calling already removes most call overhead, so the payoff is small. |
| Monomorphic per-call-site dispatch cache | Net regression | Caching `(call-site, receiver-class) → method` on each `StackInstr` to skip `ResolveVirtualMethod`'s map lookup. Measured: monomorphic virtual 3.35s→3.53s, non-virtual 2.75s→2.83s, bimorphic flat — all worse or equal. The extra `StackInstr` fields (+50% size) cost more in dcache pressure than the saved lookup, and `virtual_methods` is already a hashed `unordered_map`. Reverted. |

*Conditions not recorded:* the ratios in this table (0.66x, 0.85x, 0.64x, 0.91x, 0.5x, the 3.35 s to 3.53 s dispatch pair) have no host, input or run count on this page; they are experiment notes, not reproducible measurements, and "x slower" is the factor the experiment's benchmark ran at.

---

## Speedup Roadmap

Prioritized by leverage, grounded in the cross-language run above. The single
clearest signal *(as written in June; see the 2026-11-22 figures above for where it stands now)*: **Objeck already beats Python/Ruby on JIT-friendly loops but
loses to everything — including the interpreters — on allocation-bound work
(binarytrees), and trails Java/LuaJIT on float loops.** So the highest-value work
is allocation throughput and float codegen, not more integer-loop tuning.

### P0 — Recover the GC-safepoint regression *(complete — both arches)*

The cooperative stop-the-world work (v2026.6.2) emits an unconditional
`call MemoryManager::SafePoint` at **every JIT label**, which regressed
label-dense integer loops (fannkuchredux ~35s → ~59s; the call/ret + optimization
barrier is paid every loop iteration) while barely touching float/call-dominated
loops (spectralnorm_native +7%).

| Step | Status | Impact |
|------|--------|--------|
| Inline the flag test, call only when a collection is active (`cmp [stw_active],0; je skip`) | **AMD64 done, validated** (branch `perf/jit-safepoint-inline`); ARM64 mirror pending on-device test | fannkuch 71.6s → 56.9s (**−20%**, recovers ~71% of the regression); GC/JIT + multithreaded-STW stress green |
| Cache `&stw_active` in a callee-saved reg at the prologue | **AMD64 done, validated** (R12); **ARM64 done, validated on Apple Silicon** (X19) | AMD64: each label's poll is now a 5-byte `cmp byte [r12],0` (was a 10-byte `movabs` + 3-byte `cmp`); fannkuch-12 38.4s → 34.7s (**−10%**, far less run-to-run variance). ARM64: each poll drops from a 2-instr const-pool address load + `ldarb` to just `ldarb W11,[X19]`; fannkuch-11 2.52s → 2.49s (**~−1%**, min-of-6 — smaller win since the ARM64 address load was already a cheap const-pool `ldr`, not AMD64's 10-byte `movabs`). The fully-packed RED_ZONE frame had no free fixed slot and `RegisterRoot` zeros the local region at entry, so X19 is saved in the one untouched spot: the top 8 bytes of the frame (`final_local_space - 8`, above the zeroed region and below the incoming args). 158/158 ARM64 regression × 3 + `core_thread_gc_stress`/`jit_gc_stress` × 5 clean (no STW deadlock) |
| Emit the poll only at true loop back-edges, not at if/else merges | **AMD64 done, validated**; **ARM64 done, validated on Apple Silicon** | reuses the back-edge pre-scan: a label gets a poll only if it is the target of a backward jump (loop header). AMD64 inlined callees carry no control flow (`CanInlineMethod` rejects `JMP`/`LBL`) and ARM64 has no inlining, so every loop is covered. AMD64 fannkuch-12 34.7s → 32.5s (**−6%** more); 157/158 regression pass with no STW deadlock (threading/GC tests green). ARM64 mirrors the same logic (note: ARM64 JMP operand is target-index + 1) |

*Conditions for the P0 table: not recorded* beyond what each cell states (fannkuch-12 on AMD64, fannkuch-11 on Apple Silicon for ARM64, min-of-6 for the ARM64 figures); the host CPU, run counts for the AMD64 figures and the benchmark input for the first row (71.6 s to 56.9 s) are not on this page. The rows are not mutually comparable: fannkuchredux appears at ~35 s, ~59 s, 71.6 s, 56.9 s and 38.4 s across them, so do not chain them into one trajectory and do not compare them with the 2026-11-07 figure (36.01 s).

### P1 — Allocation & GC throughput *(the #1 cross-language gap)*

binarytrees is the benchmark where Objeck trails the most. *(The original text, written
against the June run, said it lost to every peer, Java 22x and the interpreters ~2x. In
the 2026-11-22 run Objeck beats CPython, Ruby and LuaJIT on time and trails Java 8.5x;
the memory gap, 177 MB, remains.)* The young-gen bump allocator helped, but per-object
allocation + a call into the allocator still dominates.

| Opportunity | Category | Expected Impact |
|-------------|----------|----------------|
| **Escape analysis** — stack-allocate objects that don't outlive their frame (tree nodes, short-lived temps) | Compiler/VM | **HIGH** — removes GC pressure entirely for non-escaping values; directly targets the binarytrees loss |
| **Inline the young-gen bump-alloc fast path into JIT'd code** | JIT | **HIGH** — replace the allocator `call` for nursery objects with an inline `atomic_fetch_add` + bounds check |
| **Thread-local allocation buffers (TLAB)** | GC | MED — cut `young_offset` CAS contention under multithreaded allocation |

### P2 — Close JIT coverage gaps

| Opportunity | Category | Expected Impact |
|-------------|----------|----------------|
| ~~**DYN_MTHD_CALL auto-JIT** — closure / function-ref calls~~ | JIT | **DONE** (v2026.6.2). Superseded by v2026.9.1's calling convention, which gives func-ref sites an inline cache and a direct native entry |
| **Float JIT codegen + float fast-path opcodes** (`LOAD_FLOAT_LIT`, `ADD/SUB/MUL_FLOAT`) | JIT/VM | **HIGH** — Java and LuaJIT lead specifically on float loops (nbody, spectralnorm). *Status check:* `ADD_FLOAT`, `SUB_FLOAT`, `MUL_FLOAT` and the float comparisons are in the interpreter's inline switch (`core/vm/interpreter.cpp`, around line 453; `DIV_FLOAT` deliberately is not), and `LOAD_FLOAT_LIT` is not, so the "integer-only" description this row used to carry is out of date |
| **ProcessInlineMethod for MTHD_CALL** — getter/ctor inlining inside JIT'd methods | JIT | **LOW now** — the trampoline it existed to remove is gone (v2026.9.1); a direct native-entry call is 5.5 ns, so inlining buys far less than when this was written. Still blocked by INSTANCE_MEM offset corruption in constructors |
| **Per-call-site monomorphic virtual dispatch cache** | VM | LOW — store the last (class→method) pointer in the instruction to skip the hash lookup in the common case |
| **Lower the auto-JIT threshold** (currently 10 calls) | VM | MED — now *unblocked* on ARM64: the forced-JIT (`THRESHOLD=1`) correctness fixes above mean cold helpers JIT correctly, so the trigger can drop to compile warm code sooner. Measure the compile-time-vs-runtime trade-off before changing the default; re-validate x64 at the lower threshold first |

### P3 — Measurement & methodology

| Opportunity | Impact |
|-------------|--------|
| ~~**Re-run every table on v2026.11.3**~~ | **DONE** (2026-11-22, `ef26ff969d`, same 7950X3D host). Within 7% of the v2026.9.1 pass and 8–10% faster than the 2026-11-07 pass, the hand-`native` control moving with it, so read as the host settling; binarytrees peak RSS 177 MB. Previously re-run on v2026.11.0 (2026-11-07, `21393c2fbb`: every row 6–17% slower than September) and on v2026.9.1 (2026-09-12, `f6b3b09046`): spectralnorm 16.5x, binarytrees 4.0x, nbody 2.3x versus June |
| Stand up a **native (non-Docker) cross-language harness** so peer comparisons aren't muddied by the ~30–50% Docker interpreter overhead documented above | gives bare-metal interpreter numbers; the current page is entirely Docker |
| ~~**Add `fasta` to the measured set**~~ | **DONE** — both causes fixed in v2026.9.1 (four `d`-suffixed float literals, and `Console->WriteBuffer(Char[])` encoding its output twice), and it is measured above at **6.86s** (7.07s in September, 7.56s on 2026-11-07). It had been skipped silently since before June because the harness discarded the compiler's stderr |
| **A second cross-language table on current language releases** — the run above uses Ubuntu 24.04's Python 3.12.3, Ruby 3.2.3 (pre-YJIT-by-default) and OpenJDK 21 | must be *additive*: swapping runtimes breaks comparison with this page's own history, so it belongs beside the like-for-like table, not instead of it |
| Add **Java + LuaJIT to the gating perf CI** so regressions like the safepoint one are caught automatically | the fannkuch regression shipped unnoticed because nothing compared releases head-to-head |
| ~~Record the **Objeck version beside every table**~~ | **DONE** — every measured table now carries the version and the commit it was taken at. This page went four releases without its numbers being re-measured and nothing in the tables said so; a stamp per table is what makes that visible next time |

---

*Prose and measured tables last updated: **November 22, 2026** (run started 19:43 UTC) — Objeck **v2026.11.3** (`ef26ff969d`, the release tag's commit), a unified Docker run on an AMD Ryzen 9 7950X3D (32 vCPU / 62 GB visible to Docker; the host has 128 GB). Median of 3 runs. Numbers are Docker: interpreter-bound rows run ~30–50% slower than a native build, while JIT/`native` code is largely unaffected.*

*The earlier measured runs were November 7, 2026 (v2026.11.0, `21393c2fbb`), September 12, 2026 (v2026.9.1, `f6b3b09046`) and June 21, 2026 (v2026.6.2). June is retained as the "June" column in the micro-benchmark table and as the before-figures quoted in the prose; September is the "v2026.9.1" column. Headline movement June to now: spectralnorm **15.4x**, binarytrees **4.2x**, micro-benchmarks up to **54x**; fannkuchredux flat. November 7 to now: every row but mandelbrot 8–10% faster, read as the host settling (the hand-`native` control moved with it). September to now: every row within 7% either way.*

*All six CLBG benchmarks are measured here, `fasta` included.*

*The measured commit is the v2026.11.3 release tag. Since v2026.11.0 the changes were compile speed, a collector crash fix, libraries and release tooling, none aimed at run-time throughput. The report-only CI perf gate is not evidence for or against that.*

*P0 safepoint roadmap: complete on both arches — AMD64 (all three steps) and ARM64 (back-edge + X19 register-cache, validated on Apple Silicon). The ARM64 forced-JIT correctness chain (PR #548, #551) is merged and the full ARM64 suite is green at `OBJECK_JIT_THRESHOLD=1`; as of v2026.9.1 every CI leg runs that pass, ARM64 included.*
