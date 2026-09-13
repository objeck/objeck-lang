#!/bin/bash
#
# Builds the third-party libraries the macOS package carries INSIDE itself, so a
# Mac needs nothing from Homebrew to run any part of Objeck:
#
#   OpenCV 4.12.0       static, the 7 modules the bindings call    -> libobjk_opencv, libobjk_onnx
#   ONNX Runtime 1.30.0 Microsoft's prebuilt, shipped beside it      -> libobjk_onnx (floor macOS 14.0)
#   mbedTLS 3.6.4       static, the in-tree mbedtls_config.h         -> obr, obd, obi, module, crypto, diags, odbc
#   libiodbc 3.52.12    static                                       -> libobjk_odbc
#   LAME 3.100          SHARED (LGPL), shipped beside libobjk_lame   -> libobjk_lame
#
# Every source is pinned by version AND sha256, everything targets macOS 13.3,
# and Homebrew is hidden from every configure, so a developer Mac and a CI runner
# produce the same archives. Until v2026.9.3 the package linked Homebrew's
# opencv@4 (.414), onnxruntime and lame by absolute path, and its vendored
# mbedTLS (macOS 15.0) and libiodbc (macOS 26.0) archives quietly raised the
# real minimum macOS far above the 11.1-13.3 the projects declared.
#
# Recipes validated on an M4 Max (2026-09-13): every binary relinked from this
# prefix builds with 0 "built for newer macOS" warnings; core_opencv,
# onnx_runtime_test, lame_encode_test, byte_array_header_test, HTTP/2 and HTTP/3
# pass with 0 dyld loads from /opt/homebrew or /usr/local.
#
# Usage: tools/deps/build_macos_deps.sh [prefix]
#   prefix defaults to $OBJECK_DEPS, then ~/objeck-deps/darwin-arm64; the libraries
#   land in <prefix>/macos-bundle/{opencv,onnxruntime,mbedtls,iodbc,lame}, each
#   with the upstream license files under licenses/.
# Re-running with the same versions and the same script is a no-op (stamp file),
# so CI caches the prefix keyed on this script's hash. A failed run leaves any
# previous prefix in place: it builds into <prefix>/macos-bundle.partial.

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
IODBC_VERSION=3.52.12
IODBC_URL=https://github.com/openlink/iODBC/releases/download/v$IODBC_VERSION/libiodbc-$IODBC_VERSION.tar.gz
IODBC_SHA256=51c5ff3a7d9a54202486cb77a3514e0e379a135beefcd5d12b96d1901f9dfb62
LAME_VERSION=3.100
LAME_URL=https://downloads.sourceforge.net/project/lame/lame/$LAME_VERSION/lame-$LAME_VERSION.tar.gz
LAME_SHA256=ddfe36cab873794038ae2c1210557ad34857a4b6bdc515785d1da9e175b1da1e

export MACOSX_DEPLOYMENT_TARGET=13.3
REPO=$(cd "$(dirname "$0")/../.." && pwd)
PREFIX_ROOT="${1:-${OBJECK_DEPS:-$HOME/objeck-deps/darwin-arm64}}"
OUT="$PREFIX_ROOT/macos-bundle"
STAMP="$OUT/.macos-deps-stamp"
CONFIG_H="$REPO/core/lib/openssl/macos/mbedtls_config.h"
[ -f "$CONFIG_H" ] || { echo "ERROR: $CONFIG_H not found" >&2; exit 1; }
SCRIPT_HASH=$(shasum -a 256 "$0" | cut -c1-16)
WANT="opencv $OPENCV_VERSION ort $ORT_VERSION mbedtls $MBEDTLS_VERSION iodbc $IODBC_VERSION lame $LAME_VERSION target $MACOSX_DEPLOYMENT_TARGET config $(shasum -a 256 "$CONFIG_H" | cut -c1-16) script $SCRIPT_HASH"

if [ -f "$STAMP" ] && [ "$(cat "$STAMP")" = "$WANT" ]; then
	echo "macOS bundle deps already built in $OUT ($WANT)"
	exit 0
fi

for tool in cmake make curl shasum install_name_tool codesign otool cmp; do
	command -v "$tool" >/dev/null || { echo "ERROR: $tool is required" >&2; exit 1; }
done
[ -x /usr/bin/clang ] || { echo "ERROR: /usr/bin/clang is required (Xcode command line tools)" >&2; exit 1; }

JOBS=$(sysctl -n hw.ncpu)
WORK=$(mktemp -d)
trap 'rm -rf "$WORK"' EXIT
STAGE="$OUT.partial"
rm -rf "$STAGE"
mkdir -p "$STAGE"

# Homebrew must not leak in: not through CMake's search prefixes, not through
# pkg-config, not through a Homebrew LLVM earlier in PATH. (Homebrew's cmake
# itself is fine as a tool.)
export PKG_CONFIG_PATH= PKG_CONFIG_LIBDIR=/nonexistent
CMAKE_COMMON=(-G "Unix Makefiles" -DCMAKE_BUILD_TYPE=Release
	-DCMAKE_C_COMPILER=/usr/bin/clang
	-DCMAKE_OSX_ARCHITECTURES=arm64 -DCMAKE_OSX_DEPLOYMENT_TARGET=$MACOSX_DEPLOYMENT_TARGET
	-DCMAKE_IGNORE_PREFIX_PATH="/opt/homebrew;/usr/local")
# $(xcrun -f clang) is the bare toolchain clang and cannot link without SDKROOT.
AUTOTOOLS_ENV=(CC=/usr/bin/clang
	CFLAGS="-O2 -arch arm64 -mmacosx-version-min=$MACOSX_DEPLOYMENT_TARGET"
	LDFLAGS="-arch arm64 -mmacosx-version-min=$MACOSX_DEPLOYMENT_TARGET")

step() { echo; echo "===== $1 ($(date -u +%H:%M:%SZ))"; }

# Runs a command with its output in $WORK/<name>.log; prints the tail on failure.
run() {   # name command...
	local name="$1"; shift
	"$@" > "$WORK/$name.log" 2>&1 || { echo "ERROR: $name failed"; tail -60 "$WORK/$name.log"; exit 1; }
}

# A configure that silently ignored a -D means the build is not what this script says.
cmake_configure() {   # name cmake-args...
	local name="$1"; shift
	run "$name.configure" cmake "$@"
	if grep -q "Manually-specified variables were not used" "$WORK/$name.configure.log"; then
		echo "ERROR: $name: CMake ignored flags this script passes:"
		grep -A6 "Manually-specified variables were not used" "$WORK/$name.configure.log"
		exit 1
	fi
}

fetch() {   # name url sha256 -> prints the downloaded path
	local file="$WORK/$(basename "$2")"
	echo "== fetch $1" >&2
	# macOS /bin/bash is 3.2, where set -e does not apply inside $(...): check explicitly.
	curl -fsSL --retry 3 -o "$file" "$2" || { echo "ERROR: $1 download failed" >&2; exit 1; }
	echo "$3  $file" | shasum -a 256 -c - >/dev/null || { echo "ERROR: $1 checksum mismatch" >&2; exit 1; }
	echo "$file"
}

licenses() {   # dest-lib source-dir files...
	local dest="$STAGE/$1/licenses" src="$2"; shift 2
	mkdir -p "$dest"
	local f
	for f in "$@"; do
		[ -f "$src/$f" ] || { echo "ERROR: license file $f missing from $src" >&2; exit 1; }
		cp "$src/$f" "$dest/$(echo "$f" | tr '/' '_')"
	done
}

# ---- OpenCV -------------------------------------------------------------------
step "OpenCV $OPENCV_VERSION (static)"
tarball=$(fetch opencv "$OPENCV_URL" "$OPENCV_SHA256" | tail -1)
tar xzf "$tarball" -C "$WORK"
OCV_SRC="$WORK/opencv-$OPENCV_VERSION"
# WITH_ADE=OFF is required: with gapi outside BUILD_LIST the static install still
# exports an ade target whose libade.a is never built, and find_package fails.
# The C++ compiler is passed here only: mbedTLS is a C project, and CMake reports
# a C++ compiler it never used as an ignored flag, which cmake_configure rejects.
cmake_configure opencv -S "$OCV_SRC" -B "$WORK/build-ocv" "${CMAKE_COMMON[@]}" \
	-DCMAKE_CXX_COMPILER=/usr/bin/clang++ \
	-DCMAKE_INSTALL_PREFIX="$STAGE/opencv" -DBUILD_SHARED_LIBS=OFF \
	-DBUILD_LIST=core,imgproc,imgcodecs,videoio,highgui,calib3d,dnn \
	-DBUILD_ZLIB=ON -DBUILD_PNG=ON -DBUILD_JPEG=ON -DBUILD_TIFF=ON -DBUILD_WEBP=ON \
	-DBUILD_OPENJPEG=ON -DBUILD_PROTOBUF=ON -DWITH_OPENEXR=OFF -DWITH_EIGEN=OFF \
	-DWITH_FFMPEG=OFF -DWITH_GSTREAMER=OFF -DWITH_QT=OFF -DWITH_AVFOUNDATION=ON -DWITH_COCOA=ON \
	-DWITH_ADE=OFF -DWITH_ITT=OFF -DBUILD_TESTS=OFF -DBUILD_PERF_TESTS=OFF -DBUILD_EXAMPLES=OFF \
	-DBUILD_opencv_apps=OFF -DBUILD_JAVA=OFF -DBUILD_opencv_python2=OFF -DBUILD_opencv_python3=OFF \
	-DPYTHON3_EXECUTABLE=/usr/bin/python3 -DOPENCV_GENERATE_PKGCONFIG=OFF
run opencv.build cmake --build "$WORK/build-ocv" -j"$JOBS"
run opencv.install cmake --install "$WORK/build-ocv"
licenses opencv "$OCV_SRC" LICENSE \
	3rdparty/libjpeg-turbo/LICENSE.md 3rdparty/libjpeg-turbo/README.ijg 3rdparty/libpng/LICENSE \
	3rdparty/libtiff/COPYRIGHT 3rdparty/libwebp/COPYING 3rdparty/openjpeg/LICENSE \
	3rdparty/protobuf/LICENSE 3rdparty/zlib/LICENSE 3rdparty/flatbuffers/LICENSE.txt

# ---- ONNX Runtime ---------------------------------------------------------------
step "ONNX Runtime $ORT_VERSION (prebuilt)"
tarball=$(fetch onnxruntime "$ORT_URL" "$ORT_SHA256" | tail -1)
tar xzf "$tarball" -C "$WORK"
ORT_DIR="$WORK/onnxruntime-osx-arm64-$ORT_VERSION"
mkdir -p "$STAGE/onnxruntime/lib"
cp -R "$ORT_DIR/include" "$STAGE/onnxruntime/include"
# One real file under the name the bindings link; the archive also carries a
# byte-identical libonnxruntime.dylib copy and a 73 MB dSYM, neither shipped.
cp "$ORT_DIR/lib/libonnxruntime.$ORT_VERSION.dylib" "$STAGE/onnxruntime/lib/libonnxruntime.1.dylib"
licenses onnxruntime "$ORT_DIR" LICENSE ThirdPartyNotices.txt

# ---- mbedTLS ----------------------------------------------------------------------
step "mbedTLS $MBEDTLS_VERSION (static, in-tree config)"
tarball=$(fetch mbedtls "$MBEDTLS_URL" "$MBEDTLS_SHA256" | tail -1)
tar xjf "$tarball" -C "$WORK"
MBED_SRC="$WORK/mbedtls-$MBEDTLS_VERSION"
# The in-tree config enables DTLS_SRTP and THREADING_C/PTHREAD beyond 3.6.4's
# defaults; the headers everything compiles against must match the archives.
cmake_configure mbedtls -S "$MBED_SRC" -B "$WORK/build-mbedtls" "${CMAKE_COMMON[@]}" \
	-DCMAKE_INSTALL_PREFIX="$STAGE/mbedtls" -DENABLE_TESTING=OFF -DENABLE_PROGRAMS=OFF \
	-DUSE_STATIC_MBEDTLS_LIBRARY=ON -DUSE_SHARED_MBEDTLS_LIBRARY=OFF -DGEN_FILES=OFF \
	-DPython3_EXECUTABLE=/usr/bin/python3 -DMBEDTLS_CONFIG_FILE="$CONFIG_H"
run mbedtls.build cmake --build "$WORK/build-mbedtls" -j"$JOBS"
run mbedtls.install cmake --install "$WORK/build-mbedtls"
# Installed headers must describe the archives. The build used the in-tree
# config (THREADING_C adds mutexes to context structs), but install copies
# mbedTLS's DEFAULT mbedtls_config.h; code compiled against that would disagree
# with the library about struct layouts.
cp "$CONFIG_H" "$STAGE/mbedtls/include/mbedtls/mbedtls_config.h"
licenses mbedtls "$MBED_SRC" LICENSE

# ---- libiodbc ----------------------------------------------------------------------
step "libiodbc $IODBC_VERSION (static)"
tarball=$(fetch iodbc "$IODBC_URL" "$IODBC_SHA256" | tail -1)
tar xzf "$tarball" -C "$WORK"
IODBC_SRC="$WORK/libiodbc-$IODBC_VERSION"
( cd "$IODBC_SRC"
  # configure adds -DNO_FRAMEWORKS itself; objk_odbc links no framework.
  run iodbc.configure env "${AUTOTOOLS_ENV[@]}" ./configure --prefix="$STAGE/iodbc" \
	--disable-shared --enable-static --disable-gui --disable-libodbc
  run iodbc.build make -j"$JOBS"
  run iodbc.install make install )
rm -rf "$STAGE/iodbc/bin" "$STAGE/iodbc/share"
licenses iodbc "$IODBC_SRC" LICENSE LICENSE.BSD LICENSE.LGPL

# ---- LAME -------------------------------------------------------------------------
step "LAME $LAME_VERSION (shared, LGPL)"
tarball=$(fetch lame "$LAME_URL" "$LAME_SHA256" | tail -1)
tar xzf "$tarball" -C "$WORK"
LAME_SRC="$WORK/lame-$LAME_VERSION"
( cd "$LAME_SRC"
  # 3.100's export list names lame_init_old, which is not defined; ld fails on it.
  sed -i '' '/^lame_init_old$/d' include/libmp3lame.sym
  run lame.configure env "${AUTOTOOLS_ENV[@]}" ./configure --prefix="$STAGE/lame" \
	--enable-shared --disable-static --disable-frontend
  run lame.build make -j"$JOBS"
  run lame.install make install )
install_name_tool -id @rpath/libmp3lame.0.dylib "$STAGE/lame/lib/libmp3lame.0.dylib"
codesign --force --sign - "$STAGE/lame/lib/libmp3lame.0.dylib" 2>/dev/null
rm -rf "$STAGE/lame/share" "$STAGE/lame/bin"
licenses lame "$LAME_SRC" COPYING LICENSE

# ---- checks -------------------------------------------------------------------------
step "checks"
FAILED=0
fail() { echo "ERROR: $*" >&2; FAILED=1; }

for f in opencv/lib/libopencv_core.a opencv/lib/cmake/opencv4/OpenCVConfig.cmake \
         onnxruntime/lib/libonnxruntime.1.dylib onnxruntime/include/onnxruntime_cxx_api.h \
         mbedtls/lib/libmbedtls.a mbedtls/lib/libmbedx509.a mbedtls/lib/libmbedcrypto.a \
         iodbc/lib/libiodbc.a iodbc/include/sql.h \
         lame/lib/libmp3lame.0.dylib lame/include/lame/lame.h; do
	[ -e "$STAGE/$f" ] || fail "not built: $f"
done

cmp -s "$CONFIG_H" "$STAGE/mbedtls/include/mbedtls/mbedtls_config.h" \
	|| fail "installed mbedtls_config.h differs from $CONFIG_H"

# What this script exists to replace are archives built for a newer macOS than the
# package declares (vendored mbedTLS 15.0, libiodbc 26.0). Every archive member
# must record exactly the deployment target.
minos_of() { otool -l "$1" | awk '/LC_BUILD_VERSION/{f=1} f&&/minos/{print $2; f=0}'; }
while IFS= read -r archive; do
	bad=$(minos_of "$archive" | grep -v -x "$MACOSX_DEPLOYMENT_TARGET" | sort -u | tr '\n' ' ')
	[ -z "$bad" ] || fail "${archive#$STAGE/}: members built for macOS $bad"
done < <(find "$STAGE" -name '*.a' -type f)

[ "$(minos_of "$STAGE/lame/lib/libmp3lame.0.dylib" | sort -u)" = "$MACOSX_DEPLOYMENT_TARGET" ] \
	|| fail "libmp3lame.0.dylib is not built for macOS $MACOSX_DEPLOYMENT_TARGET"
# Microsoft's prebuilt sets the ONNX binding's real floor; a new version must be a
# deliberate, documented change.
[ "$(minos_of "$STAGE/onnxruntime/lib/libonnxruntime.1.dylib" | sort -u)" = "14.0" ] \
	|| fail "libonnxruntime.1.dylib minimum macOS changed from 14.0; update the docs' ONNX floor"
[ "$(otool -D "$STAGE/onnxruntime/lib/libonnxruntime.1.dylib" | tail -1)" = "@rpath/libonnxruntime.1.dylib" ] \
	|| fail "unexpected ONNX Runtime install name"

for dylib in "$STAGE/lame/lib/libmp3lame.0.dylib" "$STAGE/onnxruntime/lib/libonnxruntime.1.dylib"; do
	if otool -L "$dylib" | tail -n +2 | grep -qE '/opt/homebrew|/usr/local'; then
		fail "${dylib#$STAGE/} links Homebrew"
	fi
done

[ $FAILED -eq 0 ] || exit 1

echo "$WANT" > "$STAGE/.macos-deps-stamp"
rm -rf "$OUT"
mv "$STAGE" "$OUT"
echo "macOS bundle deps ready in $OUT"
du -sh "$OUT"/*
