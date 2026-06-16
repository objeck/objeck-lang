#!/bin/bash

# Inputs match the Objeck CLBG table in docs/performance.md for fair comparison
NBODY_N=50000000
BINARYTREES_N=17
SPECTRALNORM_N=5500
FANNKUCH_N=12
RUNS=3

CSV=/results/cross_lang_results.csv
echo "language,benchmark,run,time_seconds" > "$CSV"

run_bench() {
  local lang="$1" name="$2" cmd="$3"
  echo "=== $lang: $name ==="
  for r in $(seq 1 $RUNS); do
    local start_ns=$(date +%s%N)
    eval "$cmd" > /dev/null 2>&1
    local end_ns=$(date +%s%N)
    local elapsed_ms=$(( (end_ns - start_ns) / 1000000 ))
    local elapsed_s=$(echo "scale=3; $elapsed_ms / 1000" | bc)
    echo "  Run $r: ${elapsed_s}s"
    echo "$lang,$name,$r,$elapsed_s" >> "$CSV"
  done
}

echo "============================================"
echo "  Cross-Language Benchmark Comparison"
echo "============================================"
echo "  Python: $(python3 --version 2>&1)"
echo "  Ruby:   $(ruby --version 2>&1 | head -1)"
echo "  LuaJIT: $(luajit -v 2>&1 | head -1)"
echo "  Java:   $(java -version 2>&1 | head -1)"
echo "============================================"
echo ""

# Python
run_bench "python3" "nbody" "python3 /benchmarks/nbody.py $NBODY_N"
run_bench "python3" "binarytrees" "python3 /benchmarks/binarytrees.py $BINARYTREES_N"
run_bench "python3" "spectralnorm" "python3 /benchmarks/spectralnorm.py $SPECTRALNORM_N"
run_bench "python3" "fannkuchredux" "python3 /benchmarks/fannkuchredux.py $FANNKUCH_N"

# Ruby
run_bench "ruby" "nbody" "ruby /benchmarks/nbody.rb $NBODY_N"
run_bench "ruby" "binarytrees" "ruby /benchmarks/binarytrees.rb $BINARYTREES_N"
run_bench "ruby" "spectralnorm" "ruby /benchmarks/spectralnorm.rb $SPECTRALNORM_N"
run_bench "ruby" "fannkuchredux" "ruby /benchmarks/fannkuchredux.rb $FANNKUCH_N"

# LuaJIT
run_bench "luajit" "nbody" "luajit /benchmarks/nbody.lua $NBODY_N"
run_bench "luajit" "binarytrees" "luajit /benchmarks/binarytrees.lua $BINARYTREES_N"
run_bench "luajit" "spectralnorm" "luajit /benchmarks/spectralnorm.lua $SPECTRALNORM_N"
run_bench "luajit" "fannkuchredux" "luajit /benchmarks/fannkuchredux.lua $FANNKUCH_N"

# Java (HotSpot) — compile once; timed runs exclude javac, matching the other languages
echo "=== Compiling Java benchmarks (javac) ==="
javac -d /benchmarks/java-out /benchmarks/nbody.java /benchmarks/binarytrees.java \
  /benchmarks/spectralnorm.java /benchmarks/fannkuchredux.java
run_bench "java" "nbody" "java -cp /benchmarks/java-out nbody $NBODY_N"
run_bench "java" "binarytrees" "java -cp /benchmarks/java-out binarytrees $BINARYTREES_N"
run_bench "java" "spectralnorm" "java -cp /benchmarks/java-out spectralnorm $SPECTRALNORM_N"
run_bench "java" "fannkuchredux" "java -cp /benchmarks/java-out fannkuchredux $FANNKUCH_N"

echo ""
echo "=== COMPLETE ==="
cat "$CSV"
