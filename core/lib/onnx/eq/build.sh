#!/bin/sh

# ONNX build script for Linux and MSYS2
# Usage: ./build.sh <provider>
# Providers: cuda, cpu

PROVIDER=$1

if [ -z "$PROVIDER" ]; then
	echo "Usage: ./build.sh <provider>"
	echo "Providers: cuda, cpu"
	exit 1
fi

# macOS builds libobjk_onnx with core/lib/opencv/macos/CMakeLists.txt, against the
# static OpenCV and the prebuilt ONNX Runtime from tools/deps/build_macos_deps.sh.
# This script linked Homebrew's opencv@4 and onnxruntime instead, so the library
# did not load on a Mac without those formulas.
if [ "$(uname -s)" = "Darwin" ]; then
	echo "ERROR: on macOS, build libobjk_onnx with core/lib/opencv/macos/CMakeLists.txt" >&2
	echo "       (see core/release/deploy_macos_arm64.sh)" >&2
	exit 1
fi

rm -f *.o *.so *.dll *.dylib 2>/dev/null

# The vendored runtime is per architecture, under cuda/lib/<arch>/lib, and only
# x64 is vendored. Linking that copy on aarch64 fails ("skipping incompatible
# ... cannot find -lonnxruntime") but only after a minute compiling onnx.cpp --
# which is how v2026.9.1's linux-arm64 release came to ship without
# libobjk_onnx. Say so up front, and use an arm64 runtime once one is vendored.
case "$(uname -m)" in
	x86_64|amd64) ORT_ARCH=x64 ;;
	aarch64|arm64) ORT_ARCH=arm64 ;;
	*) ORT_ARCH=$(uname -m) ;;
esac
ORT_VENDORED="./cuda/lib/$ORT_ARCH/lib"

require_vendored_ort() {
	if [ ! -d "$ORT_VENDORED" ]; then
		echo "ERROR: no vendored ONNX Runtime for $(uname -m): $ORT_VENDORED does not exist" >&2
		echo "       (only cuda/lib/x64 is vendored, so libobjk_onnx cannot be built here)" >&2
		exit 1
	fi
}

case "$PROVIDER" in
	cuda)
		EP_DEFINE="-DONNX_EP_CUDA"
		ORT_INCLUDE="-I./cuda/lib/include"
		require_vendored_ort
		ORT_LIB="-L$ORT_VENDORED -lonnxruntime"
		;;
	cpu)
		EP_DEFINE=""
		ORT_INCLUDE="-I./cuda/lib/include"
		# Use the VENDORED runtime, same as the cuda case. This previously said
		# just -lonnxruntime, which requires a system-installed onnxruntime and
		# fails to link on a machine that has none -- even though a usable
		# runtime is sitting in cuda/lib/x64/lib. That runtime is CPU-only, so
		# it is exactly the right one for this mode.
		require_vendored_ort
		ORT_LIB="-L$ORT_VENDORED -lonnxruntime"
		;;
	*)
		echo "Unknown provider: $PROVIDER"
		exit 1
		;;
esac

CXX=${CXX:-g++}

# OpenCV renamed its pkg-config module across major versions (opencv -> opencv4
# -> opencv5) and relocated headers (include/opencv4 -> include/opencv5). Detect
# whichever module is installed so the build survives a Homebrew/apt OpenCV major
# bump instead of failing with "opencv2/opencv.hpp file not found".
OPENCV_PC=""
for m in opencv4 opencv5 opencv; do
	if pkg-config --exists "$m" 2>/dev/null; then
		OPENCV_PC="$m"
		break
	fi
done
if [ -z "$OPENCV_PC" ]; then
	echo "ERROR: no OpenCV pkg-config module found (tried opencv4, opencv5, opencv)" >&2
	exit 1
fi

EXTRA_INCLUDE=""

$CXX -O3 -std=c++17 -Wall -fPIC $EP_DEFINE $ORT_INCLUDE $EXTRA_INCLUDE \
	-c `pkg-config --cflags $OPENCV_PC` onnx.cpp \
	-Wno-unused-function -Wno-deprecated-declarations || exit 1

OS=$(uname -s)
case "$OS" in
	MSYS*|MINGW*)
		$CXX -O3 -shared -o libobjk_onnx.dll *.o \
			`pkg-config --libs $OPENCV_PC` $ORT_LIB
		;;
	*)
		# RUNPATH $ORIGIN: deploy_posix.sh ships libonnxruntime.so.1 beside this
		# library in lib/native, but the VM dlopens libobjk_onnx by absolute path,
		# and that does not add lib/native to the search for its dependencies.
		# Without this the shipped library loaded only under
		# LD_LIBRARY_PATH=lib/native -- which CI and run_regression.sh set and a
		# user who untarred the release does not: v2026.9.1 fails with
		# "libonnxruntime.so.1: cannot open shared object file", or, on a machine
		# with its own onnxruntime, "version `VERS_1.19.0' not found". RUNPATH is
		# searched before ld.so.cache, so the bundled runtime wins over that one.
		$CXX -O3 -shared -Wl,-soname,libobjk_onnx.so.1 -Wl,-rpath,'$ORIGIN' -o libobjk_onnx.so *.o \
			`pkg-config --libs $OPENCV_PC` $ORT_LIB
		;;
esac
