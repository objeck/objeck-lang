#!/bin/sh

# Prerequisites first, before the UI and before anything is deleted: a missing
# package should be a plain sentence and an exit, not a failed stage inside a
# progress display. Discovering one six stages in -- as `pkg-config: command not
# found` from inside a library build -- is how this script used to read.
missing=""
for tool in g++ make windres ar pkg-config; do
  command -v "$tool" >/dev/null 2>&1 || missing="$missing $tool"
done
# Asked of pkg-config, not guessed from include paths: MSYS2's OpenCV went from
# opencv4 to opencv5 and a directory check called a correct install missing.
for mod in eigen3 sdl2; do
  pkg-config --exists "$mod" 2>/dev/null || missing="$missing $mod"
done
pkg-config --exists opencv5 2>/dev/null || pkg-config --exists opencv4 2>/dev/null \
  || pkg-config --exists opencv 2>/dev/null || missing="$missing opencv"
if [ -n "$missing" ]; then
  echo "ERROR: missing prerequisites:$missing" >&2
  echo "" >&2
  echo "  pacman -S --needed \\" >&2
  echo "    mingw-w64-clang-x86_64-toolchain mingw-w64-clang-x86_64-pkgconf \\" >&2
  echo "    mingw-w64-clang-x86_64-eigen3 mingw-w64-clang-x86_64-opencv \\" >&2
  echo "    mingw-w64-clang-x86_64-SDL2 mingw-w64-clang-x86_64-SDL2_image \\" >&2
  echo "    mingw-w64-clang-x86_64-SDL2_mixer mingw-w64-clang-x86_64-SDL2_ttf \\" >&2
  echo "    mingw-w64-clang-x86_64-mbedtls mingw-w64-clang-x86_64-nghttp2 \\" >&2
  echo "    mingw-w64-clang-x86_64-lame mingw-w64-clang-x86_64-zlib" >&2
  exit 1
fi

# Presentation -- banner, live progress, quiet build output -- lives in ui.sh,
# as it has for deploy_posix.sh. UI_TOTAL is counted from this file rather than
# hardcoded: the posix copy carries a comment warning that the constant and the
# ui_step calls must be kept in step by hand, and this removes that chore. The
# package stage is the only conditional one, and it is indented, so `^ui_step`
# counts exactly the unconditional stages.
. "$(dirname "$0")/ui.sh"
UI_TOTAL=$(grep -c '^ui_step ' "$0")
[ "$2" = "deploy" ] && UI_TOTAL=$((UI_TOTAL + 1))
_ver=$(grep VERSION_STRING ../shared/version.h 2>/dev/null | cut -d'"' -f2)
ui_banner "msys2-clang-x64" "${_ver:-dev}"

ui_step "directories"
# setup directories
rm -rf deploy-msys2-clang
mkdir deploy-msys2-clang
mkdir deploy-msys2-clang/bin
mkdir deploy-msys2-clang/lib
mkdir deploy-msys2-clang/lib/sdl
mkdir deploy-msys2-clang/lib/sdl/fonts
mkdir deploy-msys2-clang/lib/native
mkdir deploy-msys2-clang/lib/native/misc
mkdir deploy-msys2-clang/doc

ui_step "compiler (obc)"
# build compiler
cd ../compiler
cp make/Makefile.msys2-clang.amd64 Makefile

make clean >/dev/null 2>&1 || true
make -j3 OBJECK_LIB_PATH=///".///" || ui_fail
cp obc ../release/deploy-msys2-clang/bin
cp ../lib/*.obl ../release/deploy-msys2-clang/lib
cp ../lib/*.ini ../release/deploy-msys2-clang/lib
cp ../vm/misc/*.pem ../release/deploy-msys2-clang/lib

ui_step "VM (obr)"
# build VM
cd ../vm
cp make/Makefile.msys2-clang.amd64 Makefile

make clean >/dev/null 2>&1 || true
make -j3 || ui_fail
cp obr ../release/deploy-msys2-clang/bin

ui_step "debugger (obd)"
# build debugger
cd ../debugger
cp make/Makefile.msys2-clang.amd64 Makefile

make clean >/dev/null 2>&1 || true
make -j3 || ui_fail
cp obd ../release/deploy-msys2-clang/bin

ui_step "repl (obi)"
# build repl
cd ../repl
cp make/Makefile.msys2-clang.amd64 Makefile

make clean >/dev/null 2>&1 || true
make -j3 || ui_fail
cp obi ../release/deploy-msys2-clang/bin

# build libraries
ui_step "library: odbc"
cd ../lib/odbc
./build_msys2-clang.sh odbc || ui_fail
cp odbc.dll ../../release/deploy-msys2-clang/lib/native/libobjk_odbc.dll

ui_step "library: crypto"
cd ../crypto
./build_msys2-clang.sh crypto || ui_fail
cp crypto.dll ../../release/deploy-msys2-clang/lib/native/libobjk_crypto.dll

ui_step "library: lame"
cd ../lame
./build_msys2-clang.sh lame || ui_fail
cp lame.dll ../../release/deploy-msys2-clang/lib/native/libobjk_lame.dll

ui_step "library: matrix"
cd ../matrix
./build_msys2-clang.sh matrix || ui_fail
cp matrix.dll ../../release/deploy-msys2-clang/lib/native/libobjk_ml.dll

ui_step "library: opencv"
cd ../opencv
./build_msys2-clang.sh opencv || ui_fail
cp opencv.dll ../../release/deploy-msys2-clang/lib/native/libobjk_opencv.dll

ui_step "library: sdl"
cd ../sdl
./build_msys2-clang.sh sdl || ui_fail
cp sdl.dll ../../release/deploy-msys2-clang/lib/native/libobjk_sdl.dll
cp lib/fonts/*.ttf ../../release/deploy-msys2-clang/lib/sdl/fonts

ui_step "library: diags"
cd ../diags
./build_msys2-clang.sh diags || ui_fail
cp diags.dll ../../release/deploy-msys2-clang/lib/native/libobjk_diags.dll

# Every native library this script builds must be present (and, on Linux,
# resolvable) before the tree is packaged. Without this a failed build was
# dropped from the release with a green exit -- the obu incident, again.
ui_step "library: onnx"
cd ../onnx/eq
# The vendored libonnxruntime here is a CPU-ONLY build -- it reports only
# CPUExecutionProvider despite living under eq/cuda/lib. Building with
# ONNX_EP_CUDA therefore made every session creation fail, and because the
# catch returned without setting the session handle it surfaced as a silently
# null session rather than an error. Link a CUDA-enabled onnxruntime before
# switching this back to cuda.
./build.sh cpu || ui_fail
cp libobjk_onnx.dll ../../../release/deploy-msys2-clang/lib/native/libobjk_onnx.dll

# libobjk_onnx imports onnxruntime.dll, so the runtime has to travel with it or
# loading fails with error 126 on a tree that otherwise looks complete -- the
# same way the Windows deploy once shipped without it. The DirectML package is
# where the Windows runtime lives; the eq/cuda tree beside it is ELF-only.
cp dml/packages/Microsoft.ML.OnnxRuntime.DirectML.1.22.1/runtimes/win-x64/native/*.dll    ../../../release/deploy-msys2-clang/bin/ || ui_fail


ui_step "verify native libs"
sh ../../../release/verify_native_libs.sh ../../../release/deploy-msys2-clang/lib/native dll crypto diags lame ml odbc onnx opencv sdl || ui_fail

ui_step "launcher (obb, obn)"
cd ../../../utils/launcher
make -f make/Makefile.obb.msys2-clang.amd64 clean >/dev/null 2>&1 || true
make -f make/Makefile.obb.msys2-clang.amd64 -j3 || ui_fail
cp obb.exe ../../release/deploy-msys2-clang/bin

make -f make/Makefile.obn.msys2-clang.amd64 clean >/dev/null 2>&1 || true
make -f make/Makefile.obn.msys2-clang.amd64 -j3 || ui_fail
cp obn.exe ../../release/deploy-msys2-clang/lib/native/misc

cp ../../vm/misc/config.prop ../../release/deploy-msys2-clang/lib/native/misc

ui_step "updater (obu)"
# build updater
# obu needs no msys2-specific makefile: it links no zlib and carries no windres
# resource, which is all the msys2 obb/obn variants add. MinGW appends .exe to
# the makefile's EXE=obu on its own, exactly as it does for obb.
cd ../updater
make -f make/Makefile.amd64 clean >/dev/null 2>&1 || true
make -f make/Makefile.amd64 -j3 || ui_fail
cp obu.exe ../../release/deploy-msys2-clang/bin
# No 'set -e' here, so a failed make or cp would silently yield a tree with no
# obu -- how v2026.8.0 shipped without it. Fail loudly instead.
if [ ! -f ../../release/deploy-msys2-clang/bin/obu.exe ]; then
	echo "ERROR: obu was not built or copied - aborting deploy"
	exit 1
fi

cd ../../release

ui_step "docs"
# copy docs
cd ../..
cp -R docs/syntax core/release/deploy-msys2-clang/doc/syntax
cp docs/readme.html core/release/deploy-msys2-clang
cp docs/style/readme.css core/release/deploy-msys2-clang/doc

cp LICENSE core/release/deploy-msys2-clang
unzip docs/api.zip -d core/release/deploy-msys2-clang/doc

ui_step "examples"
# copy examples
mkdir core/release/deploy-msys2-clang/examples
mkdir core/release/deploy-msys2-clang/examples/media
cp programs/deploy/*.obs core/release/deploy-msys2-clang/examples
cp programs/deploy/README.md core/release/deploy-msys2-clang/examples
# This script builds libobjk_sdl against opengl32 and ships its fonts, but copied
# none of the examples that use them, nor the data two examples read. Ship the
# same set as the Windows and POSIX deploys.
mkdir -p core/release/deploy-msys2-clang/examples/opengl
cp programs/examples/gl_*.obs core/release/deploy-msys2-clang/examples/opengl
cp programs/examples/cube_gl.obs core/release/deploy-msys2-clang/examples/opengl
cp programs/examples/gl_crystal.obj core/release/deploy-msys2-clang/examples/opengl
mkdir -p core/release/deploy-msys2-clang/examples/data
cp programs/deploy/data/* core/release/deploy-msys2-clang/examples/data
cp programs/deploy/media/*.png core/release/deploy-msys2-clang/examples/media
cp programs/deploy/media/*.wav core/release/deploy-msys2-clang/examples/media
sh core/release/verify_example_assets.sh core/release/deploy-msys2-clang/examples || exit 1

cd core/release

# deploy
if [ ! -z "$2" ] && [ "$2" = "deploy" ]; then
	ui_step "package tarball"
	mkdir -p ~/Desktop
	rm -rf ~/Desktop/objeck*
	cp -rf ../release/deploy-msys2-clang ~/Desktop/objeck-lang
	cd ~/Desktop

	rm -f objeck.tar objeck.tgz
	tar cf objeck.tar objeck-lang
	gzip objeck.tar

	mv objeck.tar.gz objeck-utils-msys2-x64_0.0.0.tgz
fi

ui_ok "deploy complete"
