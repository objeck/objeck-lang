#!/usr/bin/env bash
#
# build_linux_portable_deps.sh [prefix]
#
# The libraries the Linux release carries inside itself, built as static,
# position-independent archives. Run inside the image from
# tools/packaging/portable-linux/Dockerfile (tools/packaging/build_portable_linux.sh
# does that); design and reasons: docs/design/PORTABLE_LINUX.md.
#
#   <prefix>/lib, include        AWS-LC, ngtcp2, nghttp3, nghttp2 (build_quic_deps.sh),
#                                and mbedTLS: what obr, obd and obi link
#   <prefix>/opencv              OpenCV, for libobjk_opencv and libobjk_onnx
#   <prefix>/licenses/<name>     the license texts of all of the above
#   <prefix>/sdl2-link           SDL2 headers and a library to link libobjk_sdl
#                                against. NOT shipped: SDL2 stays the system's.
#
# No shared library is installed under <prefix>/lib, so a plain -lmbedtls with
# -L<prefix>/lib first can only resolve to the archive. zlib is not here: the
# tools link the system's libz.so.1 (docs/design/PORTABLE_LINUX.md says why).
#
# mbedTLS is the release and configuration the macOS package ships
# (tools/deps/build_macos_deps.sh): one TLS library, built one way, on both.
# OpenCV likewise, with GTK 3, GStreamer and V4L2 in place of Cocoa and
# AVFoundation; those three are linked dynamically and stay the system's.

set -euo pipefail

MBEDTLS_VERSION=3.6.4
MBEDTLS_URL=https://github.com/Mbed-TLS/mbedtls/releases/download/mbedtls-$MBEDTLS_VERSION/mbedtls-$MBEDTLS_VERSION.tar.bz2
MBEDTLS_SHA256=ec35b18a6c593cf98c3e30db8b98ff93e8940a8c4e690e66b41dfc011d678110

OPENCV_VERSION=4.12.0
OPENCV_URL=https://github.com/opencv/opencv/archive/refs/tags/$OPENCV_VERSION.tar.gz
OPENCV_SHA256=44c106d5bb47efec04e531fd93008b3fcd1d27138985c5baf4eafac0e1ec9e9d

# The SDL2 of the oldest distribution the release supports, Ubuntu 22.04.
# libobjk_sdl is compiled against these headers, so it calls nothing a newer
# SDL2 added and loads on every supported system. AlmaLinux 8's own SDL2 is
# 2.0.10, which lacks functions the binding uses (SDL_GetTicks64 is 2.0.18).
SDL2_VERSION=2.0.20
SDL2_URL=https://github.com/libsdl-org/SDL/releases/download/release-$SDL2_VERSION/SDL2-$SDL2_VERSION.tar.gz
SDL2_SHA256=c56aba1d7b5b0e7e999e4a7698c70b63a3394ff9704b5f6e1c57e0c16f04dd06
SDL2_MIXER_URL=https://www.libsdl.org/projects/SDL_mixer/release/SDL2_mixer-2.0.4.tar.gz
SDL2_MIXER_SHA256=b4cf5a382c061cd75081cf246c2aa2f9df8db04bdda8dcdc6b6cca55bede2419
SDL2_IMAGE_URL=https://www.libsdl.org/projects/SDL_image/release/SDL2_image-2.0.5.tar.gz
SDL2_IMAGE_SHA256=bdd5f6e026682f7d7e1be0b6051b209da2f402a2dd8bd1c4bd9c25ad263108d0
SDL2_TTF_URL=https://www.libsdl.org/projects/SDL_ttf/release/SDL2_ttf-2.0.18.tar.gz
SDL2_TTF_SHA256=7234eb8883514e019e7747c703e4a774575b18d435c22a4a29d068cb768a2251

[ "$(uname -s)" = Linux ] || { echo "Linux only" >&2; exit 2; }
REPO=$(cd "$(dirname "$0")/../.." && pwd)
PREFIX="${1:-${OBJECK_DEPS:-$HOME/objeck-deps/portable-linux-$(uname -m)}}"
CONFIG_H="$REPO/core/lib/openssl/macos/mbedtls_config.h"
[ -f "$CONFIG_H" ] || { echo "ERROR: $CONFIG_H not found" >&2; exit 1; }

for tool in cmake make curl sha256sum pkg-config tar; do
	command -v "$tool" >/dev/null || { echo "ERROR: $tool is required" >&2; exit 1; }
done

# The QUIC stack first: when its versions change, build_quic_deps.sh clears
# <prefix>/lib and include, which is also where mbedTLS goes.
bash "$REPO/tools/deps/build_quic_deps.sh" "$PREFIX"

STAMP="$PREFIX/.portable-deps-stamp"
WANT="quic [$(cat "$PREFIX/.quic-deps-stamp")] mbedtls $MBEDTLS_VERSION opencv $OPENCV_VERSION sdl2 $SDL2_VERSION mixer-2.0.4 image-2.0.5 ttf-2.0.18 config $(sha256sum "$CONFIG_H" | cut -c1-16) script $(sha256sum "$0" | cut -c1-16) cc $(gcc -dumpfullversion) glibc $(getconf GNU_LIBC_VERSION | cut -d' ' -f2)"
if [ -f "$STAMP" ] && [ "$(cat "$STAMP")" = "$WANT" ] && [ -f "$PREFIX/lib/libmbedtls.a" ] \
	&& [ -f "$PREFIX/opencv/lib/libopencv_core.a" ] \
	&& [ -f "$PREFIX/sdl2-link/include/SDL2/SDL.h" ]; then
	echo "Portable Linux deps already built in $PREFIX"
	exit 0
fi
rm -f "$STAMP"

JOBS=$(getconf _NPROCESSORS_ONLN)
WORK=$(mktemp -d)
trap 'rm -rf "$WORK"' EXIT

step() { echo; echo "===== $1 ($(date -u +%H:%M:%SZ))"; }
run() {   # name command...
	local name="$1"; shift
	"$@" > "$WORK/$name.log" 2>&1 || { echo "ERROR: $name failed"; tail -60 "$WORK/$name.log"; exit 1; }
}
cmake_configure() {   # name cmake-args...
	local name="$1"; shift
	run "$name.configure" cmake "$@"
	# A flag CMake does not know is a setting this script believes it made and did not.
	if grep -q "Manually-specified variables were not used" "$WORK/$name.configure.log"; then
		echo "ERROR: $name: CMake ignored flags this script passes:"
		grep -A6 "Manually-specified variables were not used" "$WORK/$name.configure.log"
		exit 1
	fi
}
fetch() {   # name url sha256 -> prints the downloaded path
	local file="$WORK/$(basename "$2")"
	echo "== fetch $1" >&2
	curl -fsSL --retry 3 -o "$file" "$2" || { echo "ERROR: $1 download failed" >&2; exit 1; }
	echo "$3  $file" | sha256sum -c - >/dev/null || { echo "ERROR: $1 checksum mismatch" >&2; exit 1; }
	echo "$file"
}
licenses() {   # name source-dir files...
	local dest="$PREFIX/licenses/$1" src="$2"; shift 2
	rm -rf "$dest"; mkdir -p "$dest"
	local f
	for f in "$@"; do
		[ -f "$src/$f" ] || { echo "ERROR: license file $f missing from $src" >&2; exit 1; }
		cp "$src/$f" "$dest/$(echo "$f" | tr '/' '_')"
	done
}

CMAKE_COMMON=(-G "Unix Makefiles" -DCMAKE_BUILD_TYPE=Release
	-DCMAKE_POSITION_INDEPENDENT_CODE=ON -DCMAKE_INSTALL_LIBDIR=lib)

# ---------------------------------------------------------------- mbedTLS
step "mbedTLS $MBEDTLS_VERSION (static, in-tree config)"
tarball=$(fetch mbedtls "$MBEDTLS_URL" "$MBEDTLS_SHA256" | tail -1)
tar xjf "$tarball" -C "$WORK"
MBED_SRC="$WORK/mbedtls-$MBEDTLS_VERSION"
cmake_configure mbedtls -S "$MBED_SRC" -B "$WORK/build-mbedtls" "${CMAKE_COMMON[@]}" \
	-DCMAKE_INSTALL_PREFIX="$PREFIX" -DENABLE_TESTING=OFF -DENABLE_PROGRAMS=OFF \
	-DUSE_STATIC_MBEDTLS_LIBRARY=ON -DUSE_SHARED_MBEDTLS_LIBRARY=OFF -DGEN_FILES=OFF \
	-DPython3_EXECUTABLE=/usr/bin/python3 -DMBEDTLS_CONFIG_FILE="$CONFIG_H"
run mbedtls.build cmake --build "$WORK/build-mbedtls" -j"$JOBS"
run mbedtls.install cmake --install "$WORK/build-mbedtls"
# The installed header is the stock one; obr must be compiled against the
# configuration the archives were built with.
cp "$CONFIG_H" "$PREFIX/include/mbedtls/mbedtls_config.h"
licenses mbedtls "$MBED_SRC" LICENSE

# ----------------------------------------------------------------- OpenCV
# Codecs, protobuf and zlib come from OpenCV's own tree, so none of them is a
# system library with a soname that moves. FFmpeg is off for exactly that
# reason: libavcodec's soname changes with every FFmpeg release. GTK 3,
# GStreamer 1.0 and V4L2 have kept theirs for a decade, so windows, video files
# and cameras go through them.
step "OpenCV $OPENCV_VERSION (static)"
tarball=$(fetch opencv "$OPENCV_URL" "$OPENCV_SHA256" | tail -1)
tar xzf "$tarball" -C "$WORK"
OCV_SRC="$WORK/opencv-$OPENCV_VERSION"
OCV_PREFIX="$PREFIX/opencv"
rm -rf "$OCV_PREFIX"
cmake_configure opencv -S "$OCV_SRC" -B "$WORK/build-ocv" "${CMAKE_COMMON[@]}" \
	-DCMAKE_INSTALL_PREFIX="$OCV_PREFIX" -DBUILD_SHARED_LIBS=OFF \
	-DBUILD_LIST=core,imgproc,imgcodecs,videoio,highgui,calib3d,dnn \
	-DBUILD_ZLIB=ON -DBUILD_PNG=ON -DBUILD_JPEG=ON -DBUILD_TIFF=ON -DBUILD_WEBP=ON \
	-DBUILD_OPENJPEG=ON -DBUILD_PROTOBUF=ON -DWITH_OPENEXR=OFF -DWITH_EIGEN=OFF \
	-DWITH_FFMPEG=OFF -DWITH_GSTREAMER=ON -DWITH_V4L=ON -DWITH_QT=OFF -DWITH_GTK=ON -DWITH_GTK_2_X=OFF \
	-DWITH_IPP=OFF -DWITH_LAPACK=OFF -DWITH_TBB=OFF -DWITH_OPENMP=OFF -DWITH_1394=OFF -DWITH_VA=OFF \
	-DWITH_OPENGL=OFF -DWITH_JASPER=OFF -DWITH_AVIF=OFF -DWITH_JPEGXL=OFF -DWITH_GDAL=OFF \
	-DWITH_ADE=OFF -DWITH_ITT=OFF -DBUILD_TESTS=OFF -DBUILD_PERF_TESTS=OFF -DBUILD_EXAMPLES=OFF \
	-DBUILD_opencv_apps=OFF -DBUILD_JAVA=OFF -DBUILD_opencv_python2=OFF -DBUILD_opencv_python3=OFF \
	-DPYTHON3_EXECUTABLE=/usr/bin/python3 -DOPENCV_GENERATE_PKGCONFIG=ON \
	-DOPENCV_LIB_INSTALL_PATH=lib -DOPENCV_3P_LIB_INSTALL_PATH=lib/opencv4/3rdparty
# The three system backends must have been found: without them OpenCV still
# configures, and ships a library that cannot open a window or a video.
for want in "GTK+:" "GStreamer:" "v4l/v4l2:"; do
	grep -F "$want" "$WORK/opencv.configure.log" | grep -q "YES" \
		|| { echo "ERROR: OpenCV configured without ${want%:}"; grep -E "GUI|GTK|GStreamer|v4l" "$WORK/opencv.configure.log"; exit 1; }
done
run opencv.build cmake --build "$WORK/build-ocv" -j"$JOBS"
run opencv.install cmake --install "$WORK/build-ocv"
licenses opencv "$OCV_SRC" LICENSE \
	3rdparty/libjpeg-turbo/LICENSE.md 3rdparty/libjpeg-turbo/README.ijg 3rdparty/libpng/LICENSE \
	3rdparty/libtiff/COPYRIGHT 3rdparty/libwebp/COPYING 3rdparty/openjpeg/LICENSE \
	3rdparty/protobuf/LICENSE 3rdparty/zlib/LICENSE 3rdparty/flatbuffers/LICENSE.txt

# Every library here is an archive, so what pkg-config calls private is needed
# on every link (core/lib/*/build_linux.sh ask for plain --libs), and so are the
# GTK and GStreamer libraries highgui and videoio call: OpenCV's generated file
# names neither, and a shared library links without them, leaving symbols that
# fail when a window is first opened. --as-needed keeps only the ones used.
PC="$OCV_PREFIX/lib/pkgconfig/opencv4.pc"
[ -f "$PC" ] || { echo "ERROR: $PC was not installed"; find "$OCV_PREFIX" -name '*.pc'; exit 1; }
private=$(sed -n 's/^Libs\.private: *//p' "$PC")
system=$(pkg-config --libs gtk+-3.0 gstreamer-1.0 gstreamer-app-1.0 gstreamer-riff-1.0 gstreamer-pbutils-1.0 gstreamer-video-1.0 gstreamer-audio-1.0) \
	|| { echo "ERROR: pkg-config cannot find GTK 3 or GStreamer"; exit 1; }
sed -i -e "s|^Libs: \(.*\)\$|Libs: \1 $private $system|" -e '/^Libs\.private:/d' "$PC"

# ------------------------------------------------------------- SDL2 (link)
step "SDL2 $SDL2_VERSION (headers and link library only, not shipped)"
tarball=$(fetch sdl2 "$SDL2_URL" "$SDL2_SHA256" | tail -1)
tar xzf "$tarball" -C "$WORK"
SDL_PREFIX="$PREFIX/sdl2-link"
rm -rf "$SDL_PREFIX"
cmake_configure sdl2 -S "$WORK/SDL2-$SDL2_VERSION" -B "$WORK/build-sdl2" "${CMAKE_COMMON[@]}" \
	-DCMAKE_INSTALL_PREFIX="$SDL_PREFIX" -DSDL_SHARED=ON -DSDL_STATIC=OFF -DSDL_TEST=OFF
run sdl2.build cmake --build "$WORK/build-sdl2" -j"$JOBS"
run sdl2.install cmake --install "$WORK/build-sdl2"
# SDL2_image, SDL2_mixer and SDL2_ttf: the HEADER of Ubuntu 22.04's release of
# each, beside the SDL.h above. The image's packages are newer (SDL2_mixer 2.8),
# and a newer header can turn what was a macro into a function: Mix_LoadWAV
# became one in 2.6, and a binding compiled against it does not load where
# SDL2_mixer is 2.0.4. The library the link names is still the image's; only its
# soname is recorded, and that has not changed.
sdl_header() {   # name url sha256 header
	local tarball
	tarball=$(fetch "$1" "$2" "$3" | tail -1)
	tar xzf "$tarball" -C "$WORK"
	cp "$WORK/$(basename "$2" .tar.gz)/$4" "$SDL_PREFIX/include/SDL2/" || { echo "ERROR: $4 not in $1" >&2; exit 1; }
}
sdl_header sdl2_mixer "$SDL2_MIXER_URL" "$SDL2_MIXER_SHA256" SDL_mixer.h
sdl_header sdl2_image "$SDL2_IMAGE_URL" "$SDL2_IMAGE_SHA256" SDL_image.h
sdl_header sdl2_ttf "$SDL2_TTF_URL" "$SDL2_TTF_SHA256" SDL_ttf.h

# ------------------------------------------------------------------ checks
step "checks"
FAILED=0
fail() { echo "ERROR: $*" >&2; FAILED=1; }
for f in lib/libmbedtls.a lib/libmbedx509.a lib/libmbedcrypto.a include/mbedtls/ssl.h \
         lib/libssl.a lib/libngtcp2.a opencv/lib/libopencv_core.a opencv/lib/libopencv_dnn.a \
         sdl2-link/include/SDL2/SDL.h sdl2-link/lib/libSDL2main.a sdl2-link/lib/pkgconfig/sdl2.pc \
         licenses/mbedtls/LICENSE licenses/opencv/LICENSE licenses/aws-lc/LICENSE; do
	[ -e "$PREFIX/$f" ] || fail "not built: $f"
done
shared=$(find "$PREFIX/lib" "$OCV_PREFIX/lib" -name '*.so*' 2>/dev/null || true)
[ -z "$shared" ] || fail "shared libraries in the prefix, which -l would prefer to the archives: $shared"
cmp -s "$CONFIG_H" "$PREFIX/include/mbedtls/mbedtls_config.h" || fail "installed mbedtls_config.h differs from $CONFIG_H"
PKG_CONFIG_PATH="$OCV_PREFIX/lib/pkgconfig" pkg-config --libs opencv4 >/dev/null || fail "opencv4.pc does not resolve"
[ $FAILED -eq 0 ] || exit 1

echo "$WANT" > "$STAMP"
echo "Portable Linux deps ready in $PREFIX"
du -sh "$PREFIX/lib" "$OCV_PREFIX" "$SDL_PREFIX" "$PREFIX/licenses"
