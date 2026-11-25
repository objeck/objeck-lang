#!/bin/sh
# Runs bench_run.obs over a matrix of batch sizes and modes and prints markdown rows.
#
#   MODEL=/path/resnet34.onnx EP=cuda BATCHES="1 8 32" ITERS=40 \
#   MODES=run,run_raw,bound_copy,bound_resident,bound_dynout ./run_bench.sh [key=value ...]
#
# Needs an Objeck install (obc, obr on PATH, OBJECK_LIB_PATH set) whose libobjk_onnx is linked
# to a GPU ONNX Runtime, with that runtime's directory (and the CUDA 12 / cuDNN 9 libraries)
# on LD_LIBRARY_PATH (PATH on Windows). Extra key=value arguments go to the session config,
# e.g.  ./run_bench.sh cudnn_conv_algo_search=HEURISTIC
#
# Rows:  | model | batch | provider | mode | median ms | p95 ms | per second |
# Run nothing else on the GPU meanwhile, and do not compare numbers from a busy machine.

: "${MODEL:?set MODEL to the .onnx file}"
EP=${EP:-cuda}
BATCHES=${BATCHES:-1}
ITERS=${ITERS:-40}
MODES=${MODES:-run,bound_copy,bound_resident}
LIBS=${LIBS:-models,opencv,cipher,json,onnx,gen_collect}
HERE=$(cd "$(dirname "$0")" && pwd)

OBE=${TMPDIR:-/tmp}/bench_run.obe
obc -src "$HERE/bench_run.obs" -lib "$LIBS" -dest "$OBE" >/dev/null || exit 1

echo "| model | batch | provider | mode | median ms | p95 ms | per second |"
echo "|---|---:|---|---|---:|---:|---:|"
for batch in $BATCHES; do
  obr "$OBE" "$MODEL" "$EP" "$batch" "$ITERS" "$MODES" "$@" 2>/dev/null | while IFS='|' read -r tag model b ep mode n median p95 fastest rate; do
    [ "$tag" = "RESULT" ] || continue
    name=$(basename "$model" .onnx)
    printf '| %s | %s | %s | %s | %.2f | %.2f | %.0f |\n' "$name" "$b" "$ep" "$mode" "$median" "$p95" "$rate"
  done
done
