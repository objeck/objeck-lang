# Benchmark Skill

Run CLBG benchmarks and cross-language comparisons using Docker.

## Invocation

`/benchmark` — Run all benchmarks (Objeck + cross-language)
`/benchmark objeck` — Run Objeck benchmarks only
`/benchmark cross` — Run cross-language comparison only
`/benchmark binarytrees` — Run a specific benchmark

## Instructions

1. Build the Docker benchmark image from the current source:

```bash
docker build -t objeck-bench -f perf-results/docker/Dockerfile .
```

2. Run the benchmarks inside Docker for reproducible results. Use `/usr/bin/time -f "%e %M"` for timing and peak RSS. Run each benchmark 3 times and report the median.

3. **Objeck CLBG benchmarks** (compile with `-opt s3`):

| Benchmark | Source | Input |
|-----------|--------|-------|
| binarytrees | `/benchmarks/clbg/binarytrees.obs` | 17 |
| nbody | `/benchmarks/clbg/nbody.obs` | 50000000 |
| fannkuchredux | `/benchmarks/clbg/fannkuchredux.obs` | 12 |
| spectralnorm | `/benchmarks/clbg/spectralnorm.obs` | 5500 |
| mandelbrot | `/benchmarks/clbg/mandelbrot.obs` | 4000 |

4. **Cross-language comparison** (binarytrees depth=17, 3 runs each):

| Language | Command |
|----------|---------|
| Python 3 | `python3 /benchmarks/cross-lang/binarytrees.py 17` |
| Ruby | `ruby /benchmarks/cross-lang/binarytrees.rb 17` |
| LuaJIT | `luajit /benchmarks/cross-lang/binarytrees.lua 17` |

5. Present results in a comparison table with median times. Compare against the documented results in `docs/performance.md` and flag any regressions (>5% slower) or improvements (>5% faster).

6. If the user asks to update metrics, edit `docs/performance.md` with the new numbers.

## Example Docker command

```bash
docker run --rm objeck-bench bash -c '
OBC=/opt/objeck/bin/obc
OBR=/opt/objeck/bin/obr
$OBC -src /benchmarks/clbg/binarytrees.obs -dest /tmp/bt.obe -opt s3 2>/dev/null
for run in 1 2 3; do
    result=$( { /usr/bin/time -f "%e %M" $OBR /tmp/bt.obe 17 > /dev/null; } 2>&1 | tail -1 )
    echo "Run $run: $(echo "$result" | awk "{print \$1}")s RSS=$(echo "$result" | awk "{print \$2}")KB"
done
'
```

## Notes

- Always rebuild the Docker image before benchmarking to test the current source
- The Docker image includes Python 3, Ruby, and LuaJIT for cross-language comparison
- Use the same Docker container for all measurements to ensure fair comparison
- Report peak RSS alongside time for GC-heavy benchmarks (binarytrees)
