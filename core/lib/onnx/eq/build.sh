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

rm -rf *.o *.so *.dll *.dylib

case "$PROVIDER" in
	cuda)
		EP_DEFINE="-DONNX_EP_CUDA"
		ORT_INCLUDE="-I./cuda/lib/include"
		ORT_LIB="-L./cuda/lib/x64/lib -lonnxruntime"
		;;
	coreml)
		EP_DEFINE="-DONNX_EP_COREML"
		ORT_INCLUDE="-I./cuda/lib/include"
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

$CXX -O3 -std=c++17 -Wall -fPIC $EP_DEFINE $ORT_INCLUDE \
	-c `pkg-config --cflags opencv4` onnx.cpp \
	-Wno-unused-function -Wno-deprecated-declarations

OS=$(uname -s)
case "$OS" in
	Darwin)
		$CXX -O3 -shared -o libobjk_onnx.dylib *.o \
			`pkg-config --libs opencv4` $ORT_LIB
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
