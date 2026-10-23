# Library benchmarks

Micro-benchmarks for the standard libraries, separate from the CLBG and cross-language
suites one directory up. Each `<name>_bench.obs` here is an ordinary Objeck program that
prints one line per case:

```
BENCH <case> <seconds> <reference_seconds>
```

`reference_seconds` is a plain-array or simplest-possible loop doing comparable work in the
same run (hash the key and touch one array slot, fill an `Int[]`, walk a `Bool[]`). The
ratio `seconds / reference_seconds` cancels most of the machine's speed and its noise, so
the ratios can be compared across machines and runs where raw seconds cannot.

## Running

```
python perf-results/libs/run_lib_bench.py <bin_dir> [--lib-dir DIR] [--runs N]
        [--only SUBSTR] [--baseline FILE] [--update-baseline] [--no-fail] [--csv FILE]
```

- `bin_dir` holds `obc` and `obr`, e.g. `core/release/deploy-x64/bin`.
- `--lib-dir` is the library directory to test (`OBJECK_LIB_PATH`); it defaults to
  `<bin_dir>/../lib`. Point it at a scratch directory to measure a rebuilt `.obl`.
- Every benchmark is compiled once (`-opt s3`, the regression suite's libraries) and run
  `--runs` times (default 5). The table shows median seconds, median reference seconds and
  the median of the per-run ratios, and a CSV goes to `results/` (ignored by git).
- Ratios are compared with `lib_baseline.json`: more than 15% slower warns, more than 30%
  fails and the script exits 1, unless `--no-fail` is given. `--update-baseline` writes the
  measured ratios as the new baseline instead.
- `--stat min` combines runs by their fastest value instead of the median, which ignores
  runs slowed by other load; the baseline records which statistic it was measured with.
- The plain-array references are cheap enough that each repeats 20 times (100 for the
  ones that touch a single `Int` or `Bool` slot) and is divided back down, so it spans
  enough clock ticks. Every case group also runs once untimed first, so JIT compilation
  is not in the timings.
- A case whose (divided) reference is under 2 ms has no ratio (Windows `clock()` ticks in
  milliseconds); give it more repetitions instead.

## Benchmarks

`collections_bench.obs` (Collection, `gen_collect.obl`):

| cases | what |
|---|---|
| `hash_str_{insert,find,remove}_{10k,100k,1m}` | `Hash<String, IntRef>` with prebuilt keys; 10k runs 100 times, 100k 10 times |
| `hash_int_{insert,find,remove}_{10k,100k,1m}` | `Hash<IntRef, IntRef>` with prebuilt keys |
| `intmap_{insert,find,remove}_*`, `intset_{add,has,remove}_*` | the unboxed `IntMap` and `IntSet`; 10k runs 1000 times, 100k 100 times, 1M 5 times, so their seconds are 10x/10x/5x the work of the Hash rows of the same size and are not comparable to them directly |
| `intvector_*` / `vector_intref_*` | append, indexed sum and sort of 1M values, unboxed against `Vector<IntRef>` |
| `bitset_{set,get,ops}_1m` | `BitSet` against `Bool[]` loops |

The primitive cases sit between `# PRIMITIVE-BEGIN` and `# PRIMITIVE-END`; deleting those
lines gives a program that compiles against a `gen_collect.obl` from before `IntMap` and
friends existed, which is how the before/after Hash numbers were taken.

## Noise

`lib_baseline.json` was measured with `--stat min` (so compare against it with `--stat min`)
on a Windows x64 desktop that was running other builds and
test suites at the same time. On that machine two back-to-back 5-run passes of the same
build disagreed by more than 30% on 2-3 of 46 cases (with either statistic), mostly cases
whose reference is a few milliseconds against a measured side 50-100 times longer, where
the reference's own jitter decides the ratio. Treat a single WARN or FAIL as a prompt to
rerun; trust a regression that repeats, or that shows up across a whole case family.

## CI

Nothing runs this in CI yet. It could run as a non-gating step after the Windows and Linux
x64 deploy builds, `python perf-results/libs/run_lib_bench.py <deploy>/bin --no-fail`, with
the CSV uploaded as an artifact; once a few runs show the hosted runners' ratios are stable,
dropping `--no-fail` makes it a gate. The baseline should be refreshed on the same runner
class it gates (`--update-baseline`), since a ratio still moves somewhat with the CPU.
