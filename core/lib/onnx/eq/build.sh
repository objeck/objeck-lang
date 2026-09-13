#!/bin/sh

# Unified ONNX build script for Linux, macOS, and MSYS2
# Usage: ./build.sh <provider>
# Providers: cuda, coreml, cpu

PROVIDER=$1

if [ -z "$PROVIDER" ]; then
	echo "Usage: ./build.sh <provider>"
	echo "Providers: cuda, coreml, cpu"
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
	coreml)
		EP_DEFINE="-DONNX_EP_COREML"
		# Use system ONNX Runtime headers on macOS to match linked library version
		if [ -d "/opt/homebrew/include/onnxruntime" ]; then
			ORT_INCLUDE="-I/opt/homebrew/include/onnxruntime"
		else
			ORT_INCLUDE="-I./cuda/lib/include"
		fi
		ORT_LIB="-lonnxruntime"
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

# macOS: Homebrew's default `opencv` is now 5.0 (moved functions out of cv::), so
# the build pins to the keg-only opencv@4. Expose its pkg-config dir so the
# detection below resolves `opencv4` to that keg. (CI also sets PKG_CONFIG_PATH.)
if [ "$(uname -s)" = "Darwin" ] && [ -d /opt/homebrew/opt/opencv@4/lib/pkgconfig ]; then
	PKG_CONFIG_PATH="/opt/homebrew/opt/opencv@4/lib/pkgconfig:${PKG_CONFIG_PATH}"
	export PKG_CONFIG_PATH
fi

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

# On macOS, vm/common.h pulls in v3-API mbedtls headers. Use the in-tree v3
# headers (same set the diags/matrix/opencv xcodeproj builds link against)
# instead of homebrew's mbedtls — brew now ships v4, which removed entropy.h.
EXTRA_INCLUDE=""
EXTRA_LIB=""
if [ "$(uname -s)" = "Darwin" ]; then
	IN_TREE_MBEDTLS_INC="$(cd "$(dirname "$0")/../../openssl/macos/include" 2>/dev/null && pwd)"
	if [ -n "$IN_TREE_MBEDTLS_INC" ] && [ -f "$IN_TREE_MBEDTLS_INC/mbedtls/entropy.h" ]; then
		EXTRA_INCLUDE="-I$IN_TREE_MBEDTLS_INC"
	elif [ -d "/opt/homebrew/opt/mbedtls@3/include" ]; then
		EXTRA_INCLUDE="-I/opt/homebrew/opt/mbedtls@3/include"
	fi
	# Append homebrew include for other deps (opencv etc.) and link path.
	if [ -d "/opt/homebrew" ]; then
		EXTRA_INCLUDE="$EXTRA_INCLUDE -I/opt/homebrew/include"
		EXTRA_LIB="-L/opt/homebrew/lib"
	fi
fi

$CXX -O3 -std=c++17 -Wall -fPIC $EP_DEFINE $ORT_INCLUDE $EXTRA_INCLUDE \
	-c `pkg-config --cflags $OPENCV_PC` onnx.cpp \
	-Wno-unused-function -Wno-deprecated-declarations || exit 1

OS=$(uname -s)
case "$OS" in
	Darwin)
		$CXX -O3 -shared -o libobjk_onnx.dylib *.o \
			`pkg-config --libs $OPENCV_PC` $EXTRA_LIB $ORT_LIB
		;;
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
