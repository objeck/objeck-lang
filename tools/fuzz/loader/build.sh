#!/usr/bin/env bash
#
# build.sh -- builds the loader's libFuzzer targets (see README.md)
#
#   CXX=clang++ tools/fuzz/loader/build.sh [out_dir]
#
# Compiles the VM's sources (all but main) with AddressSanitizer,
# UndefinedBehaviorSanitizer's bounds and alignment-free checks, and libFuzzer's
# coverage, then links loader_fuzz and loader_file_fuzz into out_dir (default
# tools/fuzz/loader/build). Needs clang with libFuzzer (Ubuntu 24.04: apt install
# clang; the libFuzzer runtime ships in libclang-rt-18-dev), plus the packages
# obr itself builds with: zlib1g-dev and libmbedtls-dev.
set -euo pipefail

HERE=$(cd "$(dirname "$0")" && pwd)
ROOT=$(cd "$HERE/../../.." && pwd)
VM="$ROOT/core/vm"
OUT=$(mkdir -p "${1:-$HERE/build}" && cd "${1:-$HERE/build}" && pwd)
CXX="${CXX:-clang++}"
JOBS="${JOBS:-$(nproc)}"

if ! "$CXX" --version 2>/dev/null | grep -qi clang; then
  echo "build.sh needs clang (libFuzzer): set CXX to a clang++ (now '$CXX')" >&2
  exit 2
fi

# -O1 and frame pointers keep the sanitizer stacks readable. The VM's own flags otherwise
# follow core/vm/make/Makefile.amd64, without the HTTP/2 and HTTP/3 libraries,
# which the loader never reaches.
SAN="-fsanitize=address,fuzzer-no-link -fsanitize=bounds,signed-integer-overflow,shift,null,return,unreachable,vla-bound -fno-sanitize-recover=undefined"
FLAGS="-g -O1 -fno-omit-frame-pointer -std=c++20 -mavx2 -pthread -m64 -D_X64 -D_OBJECK_NATIVE_LIB_PATH -w"

SOURCES=(
  "$VM/common.cpp"
  "$VM/dispatch.cpp"
  "$VM/interpreter.cpp"
  "$VM/loader.cpp"
  "$VM/arch/memory.cpp"
  "$VM/arch/memory_verify.cpp"
  "$VM/arch/jit/jit_common.cpp"
  "$VM/arch/jit/amd64/jit_amd_lp64.cpp"
)

echo "== compiling ${#SOURCES[@]} VM sources with $CXX ($JOBS jobs) into $OUT"
printf '%s\n' "${SOURCES[@]}" | xargs -P "$JOBS" -I{} sh -c \
  'src="{}"; obj="$1/$(basename "${src%.cpp}").o"; '"$CXX $FLAGS $SAN"' -c "$src" -o "$obj" || { echo "FAILED: $src" >&2; exit 255; }' _ "$OUT"

OBJS=()
for src in "${SOURCES[@]}"; do
  OBJS+=("$OUT/$(basename "${src%.cpp}").o")
done
LIBS="-lmbedtls -lmbedx509 -lmbedcrypto -ldl -lz -pthread"

echo "== linking loader_fuzz and loader_file_fuzz"
"$CXX" $FLAGS $SAN -c "$HERE/loader_fuzz.cpp" -o "$OUT/loader_fuzz.o"
"$CXX" $FLAGS $SAN -DLOADER_FUZZ_FILE -c "$HERE/loader_fuzz.cpp" -o "$OUT/loader_file_fuzz.o"
"$CXX" -fsanitize=address,fuzzer $SAN "$OUT/loader_fuzz.o" "${OBJS[@]}" $LIBS -o "$OUT/loader_fuzz"
"$CXX" -fsanitize=address,fuzzer $SAN "$OUT/loader_file_fuzz.o" "${OBJS[@]}" $LIBS -o "$OUT/loader_file_fuzz"
echo "== built $OUT/loader_fuzz and $OUT/loader_file_fuzz"
