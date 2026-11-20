#!/bin/sh
# Builds libobjk_ml_cuda.so, the OPTIONAL GPU backend of System.ML.Cnn. Not part of CI, the deploy
# scripts or a release; run it by hand on a machine with an NVIDIA GPU. (Written for Linux and NOT
# tested there: the library was built and validated on Windows only.)
#
#   build_cuda.sh [output-dir]       default: a "build" folder next to this script
#
# Needs the CUDA Toolkit 12.x or 13.x (nvcc and cuBLAS, no cuDNN) and a host g++ that nvcc accepts.
# Environment (optional): CUDA_HOME (default /usr/local/cuda), CUDA_ARCHS (default "75 80 86 89 90 120";
# the last also gets PTX), CXX_HOST (nvcc -ccbin, when the default g++ is newer than the toolkit supports).
set -e
HERE=$(cd "$(dirname "$0")" && pwd)
OUT=${1:-$HERE/build}
CUDA_HOME=${CUDA_HOME:-/usr/local/cuda}
ARCHS=${CUDA_ARCHS:-"75 80 86 89 90 120"}
NVCC=${NVCC:-$CUDA_HOME/bin/nvcc}
command -v "$NVCC" >/dev/null 2>&1 || NVCC=nvcc
command -v "$NVCC" >/dev/null 2>&1 || { echo "nvcc not found: install the CUDA Toolkit or set CUDA_HOME"; exit 1; }
GENCODE=""
LAST=""
for a in $ARCHS; do
  GENCODE="$GENCODE -gencode arch=compute_$a,code=sm_$a"
  LAST=$a
done
GENCODE="$GENCODE -gencode arch=compute_$LAST,code=compute_$LAST"
CCBIN=""
[ -n "$CXX_HOST" ] && CCBIN="-ccbin $CXX_HOST"
mkdir -p "$OUT"
# shellcheck disable=SC2086
"$NVCC" -O3 -shared -Xcompiler -fPIC $CCBIN -o "$OUT/libobjk_ml_cuda.so" "$HERE/objk_ml_cuda.cu" $GENCODE -lcublas
echo "Built $OUT/libobjk_ml_cuda.so"
echo "Copy it next to libobjk_ml.so, or point OBJK_CUDA_LIB at it; libcublas must be on the loader path (LD_LIBRARY_PATH)."
