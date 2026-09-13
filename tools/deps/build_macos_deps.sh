#!/bin/bash
#
# Builds the third-party libraries the macOS package carries INSIDE itself, so a
# Mac needs nothing from Homebrew to run any part of Objeck:
#
#   OpenCV 4.12.0       static, the 7 modules the bindings call   -> libobjk_opencv, libobjk_onnx
#   ONNX Runtime 1.30.0 Microsoft's prebuilt, shipped beside it     -> libobjk_onnx (floor macOS 14.0)
#   mbedTLS 3.6.4       static, the in-tree mbedtls_config.h        -> obr, obd, obi, objk_crypto, diags, odbc
#   LAME 3.100          SHARED (LGPL), shipped beside libobjk_lame  -> libobjk_lame
#
# Every source is pinned by version AND sha256, and everything is built for
# MACOSX_DEPLOYMENT_TARGET 13.3 with Homebrew hidden from configure, so a
# developer Mac and a CI runner produce the same archives. v2026.9.2 linked
# Homebrew's opencv@4 .414 dylibs, onnxruntime and lame by absolute path, and
# libobjk_lame was built for macOS 15.5.
#
# Recipes proven on an M4 Max (2026-09-13): core_opencv, onnx_runtime_test,
# lame_encode_test and the full regression suite (both JIT passes) green with
# zero /opt/homebrew loads.
#
# Usage: tools/deps/build_macos_deps.sh [prefix]
#   prefix defaults to $OBJECK_DEPS, then ~/objeck-deps/darwin-arm64; the libraries
#   land in <prefix>/macos-bundle/{opencv,onnxruntime,mbedtls,lame}.
# Re-running with the same versions is a no-op (stamp file), so CI caches the
# prefix keyed on this script's hash.

set -euo pipefail

[ "$(uname -s)-$(uname -m)" = "Darwin-arm64" ] || { echo "macOS arm64 only"; exit 2; }

OPENCV_VERSION=4.12.0
OPENCV_URL=https://github.com/opencv/opencv/archive/refs/tags/$OPENCV_VERSION.tar.gz
OPENCV_SHA256=44c106d5bb47efec04e531fd93008b3fcd1d27138985c5baf4eafac0e1ec9e9d
ORT_VERSION=1.30.0
ORT_URL=https://github.com/microsoft/onnxruntime/releases/download/v$ORT_VERSION/onnxruntime-osx-arm64-$ORT_VERSION.tgz
ORT_SHA256=6ebb5062a934537c352937821f9fe9718e7de1a2db1122a93dd363ffd53a7012
MBEDTLS_VERSION=3.6.4
MBEDTLS_URL=https://github.com/Mbed-TLS/mbedtls/releases/download/mbedtls-$MBEDTLS_VERSION/mbedtls-$MBEDTLS_VERSION.tar.bz2
MBEDTLS_SHA256=ec35b18a6c593cf98c3e30db8b98ff93e8940a8c4e690e66b41dfc011d678110
LAME_VERSION=3.100
LAME_URL=https://downloads.sourceforge.net/project/lame/lame/$LAME_VERSION/lame-$LAME_VERSION.tar.gz
LAME_SHA256=ddfe36cab873794038ae2c1210557ad34857a4b6bdc515785d1da9e175b1da1e

export MACOSX_DEPLOYMENT_TARGET=13.3
REPO=$(cd "$(dirname "$0")/../.." && pwd)
PREFIX_ROOT="${1:-${OBJECK_DEPS:-$HOME/objeck-deps/darwin-arm64}}"
OUT="$PREFIX_ROOT/macos-bundle"
STAMP="$OUT/.macos-deps-stamp"
CONFIG_H="$REPO/core/lib/openssl/macos/include/mbedtls/mbedtls_config.h"
WANT="opencv $OPENCV_VERSION ort $ORT_VERSION mbedtls $MBEDTLS_VERSION lame $LAME_VERSION target $MACOSX_DEPLOYMENT_TARGET config $(shasum -a 256 "$CONFIG_H" | cut -c1-16)"

if [ -f "$STAMP" ] && [ "$(cat "$STAMP")" = "$WANT" ]; then
	echo "macOS bundle deps already built in $OUT ($WANT)"
	exit 0
fi

for tool in cmake make curl shasum xcrun install_name_tool codesign otool; do
	command -v "$tool" >/dev/null || { echo "ERROR: $tool is required" >&2; exit 1; }
done

JOBS=$(sysctl -n hw.ncpu)
WORK=$(mktemp -d)
trap 'rm -rf "$WORK"' EXIT
rm -rf "$OUT"
mkdir -p "$OUT"

# Homebrew must not leak in: not through CMake's search prefixes, not through
# pkg-config. (Homebrew's cmake itself is fine as a tool.)
HIDE_BREW=(-DCMAKE_IGNORE_PREFIX_PATH="/opt/homebrew;/usr/local")
export PKG_CONFIG_PATH= PKG_CONFIG_LIBDIR=/nonexistent
CMAKE_COMMON=(-G "Unix Makefiles" -DCMAKE_BUILD_TYPE=Release
	-DCMAKE_OSX_ARCHITECTURES=arm64 -DCMAKE_OSX_DEPLOYMENT_TARGET=$MACOSX_DEPLOYMENT_TARGET "${HIDE_BREW[@]}")

fetch() {   # name url sha256 -> $WORK/<basename>
	local file="$WORK/$(basename "$2")"
	echo "== fetch $1" >&2
	curl -fsSL --retry 3 -o "$file" "$2"
	echo "$3  $file" | shasum -a 256 -c - >/dev/null || { echo "ERROR: $1 checksum mismatch" >&2; exit 1; }
	echo "$file"
}

step() { echo; echo "===== $1 ($(date -u +%H:%M:%SZ))"; }
log() { "$@" > "$WORK/step.log" 2>&1 || { tail -60 "$WORK/step.log"; exit 1; }; }

# ---- OpenCV -------------------------------------------------------------------
step "OpenCV $OPENCV_VERSION (static)"
tarball=$(fetch opencv "$OPENCV_URL" "$OPENCV_SHA256" | tail -1)
tar xzf "$tarball" -C "$WORK"
# WITH_ADE=OFF is required: with gapi outside BUILD_LIST the static install still
# exports an ade target whose libade.a is never built, and find_package fails.
log cmake -S "$WORK/opencv-$OPENCV_VERSION" -B "$WORK/build-ocv" "${CMAKE_COMMON[@]}" \
	-DCMAKE_INSTALL_PREFIX="$OUT/opencv" -DBUILD_SHARED_LIBS=OFF \
	-DBUILD_LIST=core,imgproc,imgcodecs,videoio,highgui,calib3d,dnn \
	-DBUILD_ZLIB=ON -DBUILD_PNG=ON -DBUILD_JPEG=ON -DBUILD_TIFF=ON -DBUILD_WEBP=ON \
	-DBUILD_OPENJPEG=ON -DBUILD_PROTOBUF=ON -DWITH_OPENEXR=OFF \
	-DWITH_FFMPEG=OFF -DWITH_GSTREAMER=OFF -DWITH_QT=OFF -DWITH_AVFOUNDATION=ON -DWITH_COCOA=ON \
	-DWITH_ADE=OFF -DWITH_ITT=OFF -DBUILD_TESTS=OFF -DBUILD_PERF_TESTS=OFF -DBUILD_EXAMPLES=OFF \
	-DBUILD_opencv_apps=OFF -DBUILD_JAVA=OFF -DBUILD_opencv_python2=OFF -DBUILD_opencv_python3=OFF \
	-DPYTHON3_EXECUTABLE=/usr/bin/python3 -DOPENCV_GENERATE_PKGCONFIG=OFF
log cmake --build "$WORK/build-ocv" -j"$JOBS"
log cmake --install "$WORK/build-ocv"

# ---- ONNX Runtime ---------------------------------------------------------------
step "ONNX Runtime $ORT_VERSION (prebuilt)"
tarball=$(fetch onnxruntime "$ORT_URL" "$ORT_SHA256" | tail -1)
tar xzf "$tarball" -C "$WORK"
ORT_DIR="$WORK/onnxruntime-osx-arm64-$ORT_VERSION"
mkdir -p "$OUT/onnxruntime/lib"
cp -R "$ORT_DIR/include" "$OUT/onnxruntime/include"
# One real file under the name the bindings link; the archive also carries a
# byte-identical libonnxruntime.dylib copy and a 73 MB dSYM, neither shipped.
cp "$ORT_DIR/lib/libonnxruntime.$ORT_VERSION.dylib" "$OUT/onnxruntime/lib/libonnxruntime.1.dylib"
[ "$(otool -D "$OUT/onnxruntime/lib/libonnxruntime.1.dylib" | tail -1)" = "@rpath/libonnxruntime.1.dylib" ] \
	|| { echo "ERROR: unexpected ONNX Runtime install name" >&2; exit 1; }

# ---- mbedTLS ----------------------------------------------------------------------
step "mbedTLS $MBEDTLS_VERSION (static, in-tree config)"
tarball=$(fetch mbedtls "$MBEDTLS_URL" "$MBEDTLS_SHA256" | tail -1)
tar xjf "$tarball" -C "$WORK"
# The in-tree config enables DTLS_SRTP and THREADING_C/PTHREAD beyond 3.6.4's
# defaults; the headers the VM compiles against must match the archives.
log cmake -S "$WORK/mbedtls-$MBEDTLS_VERSION" -B "$WORK/build-mbedtls" "${CMAKE_COMMON[@]}" \
	-DCMAKE_INSTALL_PREFIX="$OUT/mbedtls" -DENABLE_TESTING=OFF -DENABLE_PROGRAMS=OFF \
	-DUSE_STATIC_MBEDTLS_LIBRARY=ON -DUSE_SHARED_MBEDTLS_LIBRARY=OFF -DGEN_FILES=OFF \
	-DMBEDTLS_CONFIG_FILE="$CONFIG_H"
log cmake --build "$WORK/build-mbedtls" -j"$JOBS"
log cmake --install "$WORK/build-mbedtls"

# ---- LAME -------------------------------------------------------------------------
step "LAME $LAME_VERSION (shared, LGPL)"
tarball=$(fetch lame "$LAME_URL" "$LAME_SHA256" | tail -1)
tar xzf "$tarball" -C "$WORK"
( cd "$WORK/lame-$LAME_VERSION"
  # 3.100's export list names lame_init_old, which is not defined; ld fails on it.
  sed -i '' '/^lame_init_old$/d' include/libmp3lame.sym
  log ./configure --prefix="$OUT/lame" --enable-shared --disable-static --disable-frontend \
	CC=clang CFLAGS="-O2 -arch arm64 -mmacosx-version-min=$MACOSX_DEPLOYMENT_TARGET" \
	LDFLAGS="-arch arm64 -mmacosx-version-min=$MACOSX_DEPLOYMENT_TARGET"
  log make -j"$JOBS"
  log make install )
install_name_tool -id @rpath/libmp3lame.0.dylib "$OUT/lame/lib/libmp3lame.0.dylib"
codesign --force --sign - "$OUT/lame/lib/libmp3lame.0.dylib" 2>/dev/null
rm -rf "$OUT/lame/share" "$OUT/lame/bin"

# ---- checks -------------------------------------------------------------------------
step "checks"
MISSING=""
for f in opencv/lib/libopencv_core.a opencv/lib/cmake/opencv4/OpenCVConfig.cmake \
         onnxruntime/lib/libonnxruntime.1.dylib onnxruntime/include/onnxruntime_cxx_api.h \
         mbedtls/lib/libmbedtls.a mbedtls/lib/libmbedx509.a mbedtls/lib/libmbedcrypto.a \
         lame/lib/libmp3lame.0.dylib lame/include/lame/lame.h; do
	[ -e "$OUT/$f" ] || MISSING="$MISSING $f"
done
[ -z "$MISSING" ] || { echo "ERROR: not built:$MISSING" >&2; exit 1; }
LAME_MINOS=$(otool -l "$OUT/lame/lib/libmp3lame.0.dylib" | awk '/LC_BUILD_VERSION/{f=1} f&&/minos/{print $2; exit}')
[ "$LAME_MINOS" = "$MACOSX_DEPLOYMENT_TARGET" ] || { echo "ERROR: libmp3lame minos $LAME_MINOS" >&2; exit 1; }
if otool -L "$OUT/lame/lib/libmp3lame.0.dylib" | tail -n +2 | grep -qE '/opt/homebrew|/usr/local'; then
	echo "ERROR: libmp3lame links Homebrew" >&2; exit 1
fi

echo "$WANT" > "$STAMP"
echo "macOS bundle deps ready in $OUT"
du -sh "$OUT"/*
