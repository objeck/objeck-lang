#!/bin/bash
#
# Builds obr's HTTP/2 and HTTP/3 libraries as static, position-independent
# archives into one private prefix:
#
#   AWS-LC   TLS 1.3 for QUIC (ngtcp2's BoringSSL backend)
#   ngtcp2   QUIC
#   nghttp3  HTTP/3
#   nghttp2  HTTP/2
#
# obr links them statically, so a user never needs a system or Homebrew copy.
# Until v2026.9.3 obr linked them dynamically: on macOS that meant Homebrew
# formulas plus an ngtcp2 GnuTLS backend Homebrew does not ship, so obr could not
# start on any Mac but the one it was built on; on Linux it tied the archives to
# one distribution's library versions.
#
# Usage: tools/deps/build_quic_deps.sh [prefix]
#   prefix defaults to $OBJECK_DEPS, then ~/objeck-deps/<os>-<arch>, which is
#   where the Makefiles and the Xcode project look.
#
# macOS: export MACOSX_DEPLOYMENT_TARGET to match the binaries that link these
# (CMake reads it), or the link warns that objects target a newer macOS.
#
# Re-running with the same versions is a no-op (see the stamp file), so CI can
# cache the prefix keyed on this script's hash.

set -euo pipefail

AWSLC_VERSION=v5.8.0
NGTCP2_VERSION=v1.25.0
NGHTTP3_VERSION=v1.18.0
NGHTTP2_VERSION=v1.70.0

OS=$(uname -s | tr '[:upper:]' '[:lower:]')
ARCH=$(uname -m)
PREFIX="${1:-${OBJECK_DEPS:-$HOME/objeck-deps/$OS-$ARCH}}"
STAMP="$PREFIX/.quic-deps-stamp"
WANT="aws-lc $AWSLC_VERSION ngtcp2 $NGTCP2_VERSION nghttp3 $NGHTTP3_VERSION nghttp2 $NGHTTP2_VERSION target ${MACOSX_DEPLOYMENT_TARGET:-default}"

if [ -f "$STAMP" ] && [ "$(cat "$STAMP")" = "$WANT" ]; then
	echo "QUIC deps already built in $PREFIX ($WANT)"
	exit 0
fi

for tool in git cmake make; do
	command -v "$tool" >/dev/null || { echo "ERROR: $tool is required" >&2; exit 1; }
done

JOBS=$(getconf _NPROCESSORS_ONLN 2>/dev/null || sysctl -n hw.ncpu 2>/dev/null || echo 4)
WORK=$(mktemp -d)
trap 'rm -rf "$WORK"' EXIT

echo "Building $WANT"
echo "  into $PREFIX"
rm -rf "$PREFIX"
mkdir -p "$PREFIX"

COMMON=(-DCMAKE_BUILD_TYPE=Release
	-DCMAKE_INSTALL_PREFIX="$PREFIX"
	-DCMAKE_INSTALL_LIBDIR=lib
	-DCMAKE_POSITION_INDEPENDENT_CODE=ON
	-DBUILD_TESTING=OFF)

fetch() {   # dir url tag
	git clone -q --depth 1 --branch "$3" --recurse-submodules --shallow-submodules "$2" "$WORK/$1"
}

build() {   # dir cmake-args...
	local dir="$1"; shift
	echo "== $dir"
	cmake -S "$WORK/$dir" -B "$WORK/$dir/build" "${COMMON[@]}" "$@" > "$WORK/$dir.cmake.log" 2>&1 \
		|| { tail -40 "$WORK/$dir.cmake.log"; exit 1; }
	cmake --build "$WORK/$dir/build" -j"$JOBS" > "$WORK/$dir.build.log" 2>&1 \
		|| { tail -60 "$WORK/$dir.build.log"; exit 1; }
	cmake --install "$WORK/$dir/build" > /dev/null
}

fetch aws-lc https://github.com/aws/aws-lc "$AWSLC_VERSION"
build aws-lc -DBUILD_SHARED_LIBS=OFF -DDISABLE_GO=ON -DDISABLE_PERL=ON -DBUILD_TOOL=OFF

fetch nghttp3 https://github.com/ngtcp2/nghttp3 "$NGHTTP3_VERSION"
build nghttp3 -DENABLE_LIB_ONLY=ON -DENABLE_STATIC_LIB=ON -DENABLE_SHARED_LIB=OFF

fetch ngtcp2 https://github.com/ngtcp2/ngtcp2 "$NGTCP2_VERSION"
build ngtcp2 -DENABLE_LIB_ONLY=ON -DENABLE_STATIC_LIB=ON -DENABLE_SHARED_LIB=OFF \
	-DENABLE_OPENSSL=OFF -DENABLE_GNUTLS=OFF -DENABLE_BORINGSSL=ON \
	-DBORINGSSL_INCLUDE_DIR="$PREFIX/include" \
	-DBORINGSSL_LIBRARIES="$PREFIX/lib/libssl.a;$PREFIX/lib/libcrypto.a" \
	-DCMAKE_PREFIX_PATH="$PREFIX"

fetch nghttp2 https://github.com/nghttp2/nghttp2 "$NGHTTP2_VERSION"
build nghttp2 -DENABLE_LIB_ONLY=ON -DBUILD_STATIC_LIBS=ON -DBUILD_SHARED_LIBS=OFF

# Fail here, not at obr's link, if a library did not land.
MISSING=""
for lib in libssl.a libcrypto.a libngtcp2.a libngtcp2_crypto_boringssl.a libnghttp3.a libnghttp2.a; do
	[ -f "$PREFIX/lib/$lib" ] || MISSING="$MISSING $lib"
done
if [ -n "$MISSING" ]; then
	echo "ERROR: not built:$MISSING" >&2
	exit 1
fi

# Drop the CMake/pkg-config metadata and shared objects nothing here uses, so a
# stray dynamic copy can never be picked up by the link.
rm -rf "$PREFIX"/lib/*.so* "$PREFIX"/lib/*.dylib "$PREFIX/bin" "$PREFIX/share"

echo "$WANT" > "$STAMP"
echo "QUIC deps ready in $PREFIX"
ls -l "$PREFIX"/lib/*.a
