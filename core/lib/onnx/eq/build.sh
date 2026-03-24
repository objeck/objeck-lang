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

# On macOS with Homebrew, add include/lib paths for dependencies
EXTRA_INCLUDE=""
EXTRA_LIB=""
if [ "$(uname -s)" = "Darwin" ] && [ -d "/opt/homebrew" ]; then
	# Prefer mbedtls@3 (v4 has breaking API changes), fall back to generic path
	if [ -d "/opt/homebrew/opt/mbedtls@3/include" ]; then
		EXTRA_INCLUDE="-I/opt/homebrew/opt/mbedtls@3/include -I/opt/homebrew/include"
	else
		EXTRA_INCLUDE="-I/opt/homebrew/include"
	fi
	EXTRA_LIB="-L/opt/homebrew/lib"
fi

$CXX -O3 -std=c++17 -Wall -fPIC $EP_DEFINE $ORT_INCLUDE $EXTRA_INCLUDE \
	-c `pkg-config --cflags opencv4` onnx.cpp \
	-Wno-unused-function -Wno-deprecated-declarations

OS=$(uname -s)
case "$OS" in
	Darwin)
		$CXX -O3 -shared -o libobjk_onnx.dylib *.o \
			`pkg-config --libs opencv4` $EXTRA_LIB $ORT_LIB
		;;
	MSYS*|MINGW*)
		$CXX -O3 -shared -o libobjk_onnx.dll *.o \
			`pkg-config --libs opencv4` $ORT_LIB
		;;
	*)
		$CXX -O3 -shared -Wl,-soname,libobjk_onnx.so.1 -o libobjk_onnx.so *.o \
			`pkg-config --libs opencv4` $ORT_LIB
		;;
esac
