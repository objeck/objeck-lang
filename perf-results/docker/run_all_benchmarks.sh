#!/bin/bash
# Self-contained Objeck benchmark suite for Docker
# Runs CLBG benchmarks, perf micro-benchmarks, and cross-language comparisons
# Results are written to /results/ (mount a volume to retrieve them)
set +e  # don't exit on compile failures

RUNS=${BENCH_RUNS:-3}
OBC=/opt/objeck/bin/obc
OBR=/opt/objeck/bin/obr
CLBG=/benchmarks/clbg
PERF=/benchmarks/perf
CROSS=/benchmarks/cross-lang
OUT=/results
mkdir -p "$OUT" /tmp/bench

# ============================================
# System info
# ============================================
echo "============================================="
echo "  Objeck Docker Benchmark Suite"
echo "============================================="
echo "Date:    $(date -u '+%Y-%m-%d %H:%M UTC')"
echo "CPU:     $(lscpu | grep 'Model name' | sed 's/Model name:\s*//')"
echo "Cores:   $(nproc) logical"
echo "RAM:     $(free -h | awk '/Mem:/{print $2}')"
echo "OS:      $(cat /etc/os-release | grep PRETTY_NAME | cut -d= -f2 | tr -d '"')"
echo "Objeck:  $($OBC 2>&1 | head -1 || echo 'built from source')"
echo "Python:  $(python3 --version 2>&1)"
echo "Ruby:    $(ruby --version 2>&1 | head -1)"
echo "LuaJIT:  $(luajit -v 2>&1 | head -1)"
echo "Runs:    $RUNS"
echo "============================================="
echo ""

# Save system info
cat > "$OUT/system_info.txt" << SYSEOF
date: $(date -u '+%Y-%m-%d %H:%M UTC')
cpu: $(lscpu | grep 'Model name' | sed 's/Model name:\s*//')
cores: $(nproc) logical
ram: $(free -h | awk '/Mem:/{print $2}')
os: $(cat /etc/os-release | grep PRETTY_NAME | cut -d= -f2 | tr -d '"')
SYSEOF

# ============================================
# CLBG Benchmarks (Objeck)
# ============================================
OBJECK_CSV="$OUT/objeck_clbg.csv"
echo "benchmark,run,time_seconds,peak_rss_kb" > "$OBJECK_CSV"

compile_bench() {
    local name=$1
    local src=$2
    local extra_flags=$3
    echo "  Compiling $name..."
    $OBC -src "$src" -dest "/tmp/bench/${name}.obe" -opt s3 $extra_flags 2>/dev/null
    if [ ! -f "/tmp/bench/${name}.obe" ]; then
        echo "  SKIP: $name (compile failed)"
        return 1
    fi
}

run_objeck_bench() {
    local name=$1
    local args=$2
    local csv=$3
    if [ ! -f "/tmp/bench/${name}.obe" ]; then
        echo "=== $name === SKIP (no .obe)"
        return
    fi
    echo "=== $name ==="
    for run in $(seq 1 $RUNS); do
        result=$( { /usr/bin/time -f "%e %M" $OBR "/tmp/bench/${name}.obe" $args > /dev/null; } 2>&1 | tail -1 )
        time_sec=$(echo "$result" | awk '{print $1}')
        peak_rss=$(echo "$result" | awk '{print $2}')
        echo "  Run $run: ${time_sec}s (${peak_rss}KB)"
        echo "$name,$run,$time_sec,$peak_rss" >> "$csv"
    done
}

echo ""
echo ">>> Compiling CLBG benchmarks (Objeck, -opt s3)"
compile_bench "nbody" "$CLBG/nbody.obs"
compile_bench "spectralnorm" "$CLBG/spectralnorm.obs"
compile_bench "binarytrees" "$CLBG/binarytrees.obs"
compile_bench "mandelbrot" "$CLBG/mandelbrot.obs"
compile_bench "fannkuchredux" "$CLBG/fannkuchredux.obs"
compile_bench "fasta" "$CLBG/fasta.obs"
echo ""

echo ">>> Running CLBG benchmarks (Objeck)"
run_objeck_bench "nbody"         "50000000"  "$OBJECK_CSV"
run_objeck_bench "spectralnorm"  "5500"      "$OBJECK_CSV"
run_objeck_bench "binarytrees"   "17"        "$OBJECK_CSV"
run_objeck_bench "mandelbrot"    "4000"      "$OBJECK_CSV"
run_objeck_bench "fannkuchredux" "12"        "$OBJECK_CSV"
run_objeck_bench "fasta"         "25000000"  "$OBJECK_CSV"
echo ""

# ============================================
# Perf Micro-Benchmarks (Objeck)
# ============================================
PERF_CSV="$OUT/objeck_perf.csv"
echo "benchmark,run,time_seconds,peak_rss_kb" > "$PERF_CSV"

echo ">>> Compiling perf micro-benchmarks"
for obs in "$PERF"/*.obs; do
    name=$(basename "${obs%.obs}")
    compile_bench "$name" "$obs"
done
echo ""

echo ">>> Running perf micro-benchmarks"
run_objeck_bench "bench_loop_invariant"  ""    "$PERF_CSV"
run_objeck_bench "bench_cse"             ""    "$PERF_CSV"
run_objeck_bench "bench_strength_ext"    ""    "$PERF_CSV"
run_objeck_bench "bench_gc_churn"        ""    "$PERF_CSV"
run_objeck_bench "bench_gc_large_heap"   "18"  "$PERF_CSV"
run_objeck_bench "bench_array_intensive" ""    "$PERF_CSV"
run_objeck_bench "bench_matrix_multiply" "500" "$PERF_CSV"
run_objeck_bench "bench_method_dispatch" ""    "$PERF_CSV"
run_objeck_bench "bench_copy_prop"       ""    "$PERF_CSV"
run_objeck_bench "bench_dead_code"       ""    "$PERF_CSV"
run_objeck_bench "bench_tco"             ""    "$PERF_CSV"
run_objeck_bench "bench_spectralnorm_native" "" "$PERF_CSV" 2>/dev/null || true
echo ""

# ============================================
# Cross-Language Comparison
# ============================================
CROSS_CSV="$OUT/cross_lang.csv"
echo "language,benchmark,run,time_seconds" > "$CROSS_CSV"

# Cross-language inputs — same as Objeck CLBG for fair comparison
# These are large enough to show real differences but tractable for all languages
NBODY_N=50000000
BINARYTREES_N=17
SPECTRALNORM_N=5500
FANNKUCH_N=12

run_lang_bench() {
    local lang=$1
    local name=$2
    local cmd=$3
    local csv=$4
    echo "=== $lang: $name ==="
    for r in $(seq 1 $RUNS); do
        start_ns=$(date +%s%N)
        eval "$cmd" > /dev/null 2>&1
        end_ns=$(date +%s%N)
        elapsed_ms=$(( (end_ns - start_ns) / 1000000 ))
        elapsed_s=$(echo "scale=3; $elapsed_ms / 1000" | bc)
        echo "  Run $r: ${elapsed_s}s"
        echo "$lang,$name,$r,$elapsed_s" >> "$csv"
    done
}

echo ">>> Cross-language benchmarks"
echo ""

# Python
run_lang_bench "python3" "nbody"         "python3 $CROSS/nbody.py $NBODY_N"             "$CROSS_CSV"
run_lang_bench "python3" "binarytrees"   "python3 $CROSS/binarytrees.py $BINARYTREES_N"  "$CROSS_CSV"
run_lang_bench "python3" "spectralnorm"  "python3 $CROSS/spectralnorm.py $SPECTRALNORM_N" "$CROSS_CSV"
run_lang_bench "python3" "fannkuchredux" "python3 $CROSS/fannkuchredux.py $FANNKUCH_N"   "$CROSS_CSV"

# Ruby
run_lang_bench "ruby" "nbody"         "ruby $CROSS/nbody.rb $NBODY_N"             "$CROSS_CSV"
run_lang_bench "ruby" "binarytrees"   "ruby $CROSS/binarytrees.rb $BINARYTREES_N"  "$CROSS_CSV"
run_lang_bench "ruby" "spectralnorm"  "ruby $CROSS/spectralnorm.rb $SPECTRALNORM_N" "$CROSS_CSV"
run_lang_bench "ruby" "fannkuchredux" "ruby $CROSS/fannkuchredux.rb $FANNKUCH_N"   "$CROSS_CSV"

# LuaJIT
run_lang_bench "luajit" "nbody"         "luajit $CROSS/nbody.lua $NBODY_N"             "$CROSS_CSV"
run_lang_bench "luajit" "binarytrees"   "luajit $CROSS/binarytrees.lua $BINARYTREES_N"  "$CROSS_CSV"
run_lang_bench "luajit" "spectralnorm"  "luajit $CROSS/spectralnorm.lua $SPECTRALNORM_N" "$CROSS_CSV"
run_lang_bench "luajit" "fannkuchredux" "luajit $CROSS/fannkuchredux.lua $FANNKUCH_N"   "$CROSS_CSV"

echo ""

# ============================================
# Summary
# ============================================
echo "============================================="
echo "  RESULTS"
echo "============================================="
echo ""
echo "--- Objeck CLBG ---"
cat "$OBJECK_CSV"
echo ""
echo "--- Objeck Perf ---"
cat "$PERF_CSV"
echo ""
echo "--- Cross-Language ---"
cat "$CROSS_CSV"
echo ""
echo "============================================="
echo "Results saved to /results/"
echo "  objeck_clbg.csv"
echo "  objeck_perf.csv"
echo "  cross_lang.csv"
echo "  system_info.txt"
echo "============================================="
