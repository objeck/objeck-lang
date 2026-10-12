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

# cuda/lib/<arch>/lib holds ELF shared objects -- libonnxruntime.so.1.19.0 and
# friends -- so under MSYS2 the link failed with `cannot find -lonnxruntime`
# after compiling onnx.cpp, and libobjk_onnx.dll was simply absent from every
# MSYS2 build. A Windows runtime IS vendored, just elsewhere: the DirectML
# NuGet package the MSVC project already links. MinGW's ld reads an MSVC import
# library directly when given the file by path, so no .dll.a has to be
# generated.
#
# The headers are ORT 1.19 and that package is 1.22.1. That is safe by design,
# not by luck: the C API is obtained through OrtGetApiBase()->GetApi(
# ORT_API_VERSION), so the runtime hands back the 1.19 function table it still
# supports. Linking 1.19 headers against a NEWER runtime is the supported
# direction; the reverse is not.
case "$(uname -s)" in
	MINGW*|MSYS*|CYGWIN*) ORT_WINDOWS=1 ;;
	*) ORT_WINDOWS=0 ;;
esac
ORT_DML_ARCH=$(case "$ORT_ARCH" in arm64) echo win-arm64 ;; *) echo win-x64 ;; esac)
ORT_DML_LIB="./dml/packages/Microsoft.ML.OnnxRuntime.DirectML.1.22.1/runtimes/$ORT_DML_ARCH/native/onnxruntime.lib"

require_vendored_ort() {
	if [ "$ORT_WINDOWS" = "1" ]; then
		if [ ! -f "$ORT_DML_LIB" ]; then
			echo "ERROR: no Windows ONNX Runtime import library: $ORT_DML_LIB" >&2
			echo "       (the cuda/lib tree is ELF-only and cannot be linked here)" >&2
			exit 1
		fi
		return
	fi
	if [ ! -d "$ORT_VENDORED" ]; then
		echo "ERROR: no vendored ONNX Runtime for $(uname -m): $ORT_VENDORED does not exist" >&2
		echo "       (only cuda/lib/x64 is vendored, so libobjk_onnx cannot be built here)" >&2
		exit 1
	fi
}

# The import library by path, not -lonnxruntime: `-l` looks for
# libonnxruntime.{dll.a,a,so} and would miss an MSVC-style onnxruntime.lib.
ort_link_flags() {
	if [ "$ORT_WINDOWS" = "1" ]; then
		echo "$ORT_DML_LIB"
	else
		echo "-L$ORT_VENDORED -lonnxruntime"
	fi
}

case "$PROVIDER" in
	cuda)
		EP_DEFINE="-DONNX_EP_CUDA"
		ORT_INCLUDE="-I./cuda/lib/include"
		require_vendored_ort
		ORT_LIB=$(ort_link_flags)
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
		ORT_LIB=$(ort_link_flags)
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

# gnu++17 under MinGW, c++17 everywhere else.
#
# onnxruntime_c_api.h defines ORT_API_CALL as `_stdcall` whenever _WIN32 is
# defined -- the MSVC spelling. GCC accepts only `__stdcall`, and recognises
# that spelling solely with GNU extensions on, which -std=c++17 turns OFF. So
# under MSYS2 the very first use, OrtAllocator at line 322, fails with
#
#   error: expected ')' before '*' token
#     void*(ORT_API_CALL* Alloc)(struct OrtAllocator* this_, size_t size);
#
# and takes struct OrtApi down with it, 4,200 lines further on. The visible
# errors are then a wall of "'const OrtApi' has no member named ReadOpAttr /
# ShapeInferContext_GetAttribute", which reads as vendored headers newer than
# the runtime they compile against -- a version mismatch that does not exist.
# 632 errors become 0 with this one flag.
#
# Not patched in the header: it is vendored upstream and a re-vendor would undo
# it. Not applied everywhere either: on Linux and macOS _WIN32 is undefined,
# ORT_API_CALL expands to nothing, and c++17 is correct.
CXX_STD="-std=c++17"
case "$(uname -s)" in
	MINGW*|MSYS*|CYGWIN*) CXX_STD="-std=gnu++17" ;;
esac

$CXX -O3 $CXX_STD -Wall -fPIC $EP_DEFINE $ORT_INCLUDE $EXTRA_INCLUDE \
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
