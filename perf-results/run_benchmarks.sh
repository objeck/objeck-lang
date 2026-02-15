#!/bin/bash
# Benchmark runner for Objeck performance testing
# Usage: ./run_benchmarks.sh <deploy_dir> <output_dir> [num_runs]
#
# Example: ./run_benchmarks.sh ../../core/release/deploy-x64 ./baseline 10

set -e

DEPLOY_DIR="${1:?Usage: $0 <deploy_dir> <output_dir> [num_runs]}"
OUTPUT_DIR="${2:?Usage: $0 <deploy_dir> <output_dir> [num_runs]}"
NUM_RUNS="${3:-10}"

OBC="$DEPLOY_DIR/bin/obc"
OBR="$DEPLOY_DIR/bin/obr"

if [ ! -f "$OBC" ]; then
    echo "ERROR: Compiler not found at $OBC"
    exit 1
fi

if [ ! -f "$OBR" ]; then
    echo "ERROR: Runtime not found at $OBR"
    exit 1
fi

mkdir -p "$OUTPUT_DIR"

# CLBG benchmarks
declare -A CLBG_BENCHMARKS
CLBG_BENCHMARKS[nbody]="50000000"
CLBG_BENCHMARKS[binarytrees]="21"
CLBG_BENCHMARKS[spectralnorm]="5500"
CLBG_BENCHMARKS[fannkuchredux]="12"
CLBG_BENCHMARKS[fasta]="25000000"
CLBG_BENCHMARKS[mandelbrot]="4000"

# New perf benchmarks
declare -A PERF_BENCHMARKS
PERF_BENCHMARKS[bench_loop_invariant]=""
PERF_BENCHMARKS[bench_cse]=""
PERF_BENCHMARKS[bench_strength_ext]=""
PERF_BENCHMARKS[bench_gc_churn]=""
PERF_BENCHMARKS[bench_gc_large_heap]="18"
PERF_BENCHMARKS[bench_array_intensive]=""
PERF_BENCHMARKS[bench_matrix_multiply]="500"
PERF_BENCHMARKS[bench_method_dispatch]=""
PERF_BENCHMARKS[bench_copy_prop]=""
PERF_BENCHMARKS[bench_dead_code]=""

CLBG_DIR="$(cd "$(dirname "$0")" && pwd)/../programs/tests/clbg"
PERF_DIR="$(cd "$(dirname "$0")" && pwd)/../programs/tests/perf"

CSV_FILE="$OUTPUT_DIR/results.csv"
echo "benchmark,run,time_seconds,peak_rss_kb" > "$CSV_FILE"

run_benchmark() {
    local name="$1"
    local src_file="$2"
    local args="$3"
    local exe_file="${src_file%.obs}.obe"

    echo "=== Compiling $name ==="
    $OBC -src "$src_file" -dest "$exe_file" -opt s3 2>/dev/null || {
        echo "WARNING: Failed to compile $name, skipping"
        return
    }

    echo "=== Running $name ($NUM_RUNS runs) ==="
    for run in $(seq 1 "$NUM_RUNS"); do
        # Use /usr/bin/time for timing and peak RSS
        result=$( { /usr/bin/time -f "%e %M" $OBR "$exe_file" $args > /dev/null; } 2>&1 | tail -1 )
        time_sec=$(echo "$result" | awk '{print $1}')
        peak_rss=$(echo "$result" | awk '{print $2}')

        echo "  Run $run: ${time_sec}s, ${peak_rss}KB RSS"
        echo "$name,$run,$time_sec,$peak_rss" >> "$CSV_FILE"
    done
    echo ""
}

echo "============================================="
echo "Objeck Performance Benchmark Suite"
echo "Deploy: $DEPLOY_DIR"
echo "Output: $OUTPUT_DIR"
echo "Runs:   $NUM_RUNS"
echo "============================================="
echo ""

# Run CLBG benchmarks
for bench in "${!CLBG_BENCHMARKS[@]}"; do
    src="$CLBG_DIR/${bench}.obs"
    if [ -f "$src" ]; then
        run_benchmark "$bench" "$src" "${CLBG_BENCHMARKS[$bench]}"
    else
        echo "WARNING: $src not found, skipping"
    fi
done

# Run perf benchmarks
for bench in "${!PERF_BENCHMARKS[@]}"; do
    src="$PERF_DIR/${bench}.obs"
    if [ -f "$src" ]; then
        run_benchmark "$bench" "$src" "${PERF_BENCHMARKS[$bench]}"
    else
        echo "WARNING: $src not found, skipping"
    fi
done

echo "============================================="
echo "Results saved to: $CSV_FILE"
echo "============================================="
