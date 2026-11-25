#!/bin/bash
# Runs INSIDE an AlmaLinux 8 (glibc 2.28) container. Builds obr + obc from the
# source tree mounted at /src and writes /out/objeck-portable-linux-x64.tgz.
#
# The binaries link libstdc++/libgcc/mbedTLS/AWS-LC/ngtcp2/nghttp statically and
# need only glibc >= 2.28, libz, libm, libdl and libpthread -- so they run on
# CentOS 8 / Rocky 8 / any newer distribution with no root and no modules.
# Recipe proven by the 2026-11 portable-Linux spike (see docs/hpc_alderaan.md).
#
# Natives: only libobjk_ml (Eigen is vendored) is built; ONNX, OpenCV, SDL and
# ODBC need system libraries a glibc-2.28 host does not have -- use the container
# image (objeck-release.def) for those.
set -euo pipefail

SRC=${SRC:-/src}
OUT=${OUT:-/out}
JOBS=${JOBS:-4}
MBEDTLS_VERSION=${MBEDTLS_VERSION:-3.6.4}
DEPS=/opt/objeck-deps

dnf -y install gcc-toolset-14-gcc-c++ gcc-toolset-14-binutils git make cmake perl zlib-devel python3 tar gzip findutils >/dev/null
# shellcheck disable=SC1091
source /opt/rh/gcc-toolset-14/enable

WORK=/build
rm -rf "$WORK"; mkdir -p "$WORK" "$OUT"
cp -a "$SRC"/. "$WORK/src"
cd "$WORK/src"
# A tree exported on Windows can carry CRLF line endings; scripts and makefiles must not.
find . \( -name '*.sh' -o -name 'Makefile*' \) -type f -exec sed -i 's/\r$//' {} +

echo "== QUIC/HTTP2 deps (static) =="
bash tools/deps/build_quic_deps.sh "$DEPS"

echo "== mbedTLS $MBEDTLS_VERSION (static) =="
git clone -q --depth 1 --branch "v$MBEDTLS_VERSION" --recurse-submodules --shallow-submodules \
	https://github.com/Mbed-TLS/mbedtls.git "$WORK/mbedtls"
cmake -S "$WORK/mbedtls" -B "$WORK/mbedtls/b" -DCMAKE_BUILD_TYPE=Release -DCMAKE_INSTALL_PREFIX="$DEPS" \
	-DCMAKE_INSTALL_LIBDIR=lib -DUSE_SHARED_MBEDTLS_LIBRARY=OFF -DUSE_STATIC_MBEDTLS_LIBRARY=ON \
	-DCMAKE_POSITION_INDEPENDENT_CODE=ON -DENABLE_TESTING=OFF -DENABLE_PROGRAMS=OFF >/dev/null
cmake --build "$WORK/mbedtls/b" -j"$JOBS" >/dev/null
cmake --install "$WORK/mbedtls/b" >/dev/null

# The arch/ sub-makefiles ignore $(OBJECK_DEPS)/include; the top Makefile does not.
export CPLUS_INCLUDE_PATH="$DEPS/include"
export LIBRARY_PATH="$DEPS/lib"
static_link() {   # make the C++ runtime static on the final link line
	sed -i 's/\$(CXX) -m64 -flto=auto -o/$(CXX) -m64 -flto=auto -static-libstdc++ -static-libgcc -o/' "$1"
}

echo "== obc =="
cd "$WORK/src/core/compiler"
cp make/Makefile.amd64 Makefile; static_link Makefile
make -j"$JOBS" OBJECK_LIB_PATH=///".///"

echo "== obr =="
cd "$WORK/src/core/vm"
cp make/Makefile.amd64 Makefile; static_link Makefile
make -j"$JOBS" OBJECK_DEPS="$DEPS"

echo "== libobjk_ml =="
ML_OK=0
cd "$WORK/src/core/lib/matrix"
rm -f ./*.o ./*.so
if g++ -I lib/eigen-3.4.0 -O3 -std=c++17 -fPIC -c matrix.cpp -Wno-unused-function \
	-Wno-deprecated-declarations -Wno-maybe-uninitialized &&
   g++ -O3 -shared -static-libstdc++ -static-libgcc -Wl,-soname,matrix.so.1 -o matrix.so ./*.o \
	"$DEPS/lib/libmbedtls.a" "$DEPS/lib/libmbedx509.a" "$DEPS/lib/libmbedcrypto.a" -lpthread; then
	ML_OK=1
else
	echo "NOTE: libobjk_ml did not build; the tarball ships without it"
fi

echo "== assemble =="
D="$WORK/objeck-lang"
rm -rf "$D"; mkdir -p "$D/bin" "$D/lib/native" "$D/doc/licenses"
cp "$WORK/src/core/compiler/obc" "$WORK/src/core/vm/obr" "$D/bin/"
cp "$WORK"/src/core/lib/*.obl "$WORK"/src/core/lib/*.ini "$D/lib/"
cp "$WORK"/src/core/vm/misc/*.pem "$D/lib/"
[ "$ML_OK" = 1 ] && cp "$WORK/src/core/lib/matrix/matrix.so" "$D/lib/native/libobjk_ml.so"
cp "$WORK/src/core/release/linux/env.sh" "$D/env.sh"
cp -R "$DEPS/licenses/." "$D/doc/licenses/"
VERSION=$(grep VERSION_STRING "$WORK/src/core/shared/version.h" | cut -d'"' -f2)
echo "$VERSION" > "$D/PORTABLE_VERSION"
{
	echo "objeck $VERSION portable (built on $(. /etc/os-release; echo "$PRETTY_NAME"), glibc $(ldd --version | head -1 | awk '{print $NF}'))"
	echo "natives: $( [ "$ML_OK" = 1 ] && echo ml || echo none )"
} > "$D/PORTABLE_INFO"
echo "-- max GLIBC symbol versions --"
for b in obr obc; do
	printf '%s: ' "$b"; objdump -T "$D/bin/$b" | grep -o 'GLIBC_[0-9.]*' | sort -uV | tail -1
done
echo "-- NEEDED --"
readelf -d "$D/bin/obr" | grep NEEDED
cd "$WORK"
tar czf "$OUT/objeck-portable-linux-x64.tgz" objeck-lang
ls -l "$OUT/objeck-portable-linux-x64.tgz"
