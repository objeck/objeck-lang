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

case "$PROVIDER" in
	cuda)
		EP_DEFINE="-DONNX_EP_CUDA"
		ORT_INCLUDE="-I./cuda/lib/include"
		ORT_LIB="-L./cuda/lib/x64/lib -lonnxruntime"
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
		ORT_LIB="-lonnxruntime"
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
	-Wno-unused-function -Wno-deprecated-declarations

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
		$CXX -O3 -shared -Wl,-soname,libobjk_onnx.so.1 -o libobjk_onnx.so *.o \
			`pkg-config --libs $OPENCV_PC` $ORT_LIB
		;;
esac
