#!/bin/sh

if [ $# -eq 0 ]; then
	echo "Linux targets: x64 or arm64"
	exit 1
fi

# -v or --verbose after the target streams every build, as VERBOSE=1 does.
for _a in "$@"; do
	case "$_a" in -v|--verbose) VERBOSE=1 ;; esac
done

# Presentation -- banner, live progress, quiet build output -- lives in ui.sh.
# UI_TOTAL must equal the ui_step calls on the path taken: one per stage below,
# plus the tarball stage when $2 is deploy. A step that fails is reported by
# ui_fail and the deploy carries on, as it always has.
. "$(dirname "$0")/ui.sh"
if [ "$2" = "deploy" ]; then UI_TOTAL=19; else UI_TOTAL=18; fi
_ver=$(grep VERSION_STRING ../shared/version.h 2>/dev/null | cut -d'"' -f2)
ui_banner "linux-$1" "${_ver:-dev}"

ui_step "directories"
# setup directories
rm -rf deploy
mkdir deploy
mkdir deploy/bin
mkdir deploy/lib
mkdir deploy/lib/sdl
mkdir deploy/lib/sdl/fonts
mkdir deploy/lib/native
mkdir deploy/lib/native/misc
mkdir deploy/doc

ui_step "compiler (obc)"
# build compiler
cd ../compiler
if [ ! -z "$1" ] && [ "$1" = "arm64" ]; then
	cp make/Makefile.arm64 Makefile
else
	cp make/Makefile.amd64 Makefile
fi
make clean; make -j3 OBJECK_LIB_PATH=///".///" || ui_fail
cp obc ../release/deploy/bin
cp ../lib/*.obl ../release/deploy/lib
cp ../lib/*.ini ../release/deploy/lib
cp ../vm/misc/*.pem ../release/deploy/lib

ui_step "virtual machine (obr)"
# build VM
cd ../vm
if [ ! -z "$1" ] && [ "$1" = "arm64" ]; then
	cp make/Makefile.arm64 Makefile
else 
	cp make/Makefile.amd64 Makefile
fi
make clean; make -j3 || ui_fail
cp obr ../release/deploy/bin

ui_step "debugger (obd)"
# build debugger
cd ../debugger
if [ ! -z "$1" ] && [ "$1" = "arm64" ]; then
	cp make/Makefile.arm64 Makefile
else
	cp make/Makefile.amd64 Makefile
fi
make clean; make -j3 || ui_fail
cp obd ../release/deploy/bin

ui_step "repl (obi)"
# build repl
cd ../repl
if [ ! -z "$1" ] && [ "$1" = "arm64" ]; then
	cp make/Makefile.arm64 Makefile
else
	cp make/Makefile.amd64 Makefile
fi
make clean; make -j3 || ui_fail
cp obi ../release/deploy/bin

ui_step "library: odbc"
# build libraries
cd ../lib/odbc
./build_linux.sh odbc || ui_fail
cp odbc.so ../../release/deploy/lib/native/libobjk_odbc.so

ui_step "library: crypto"
cd ../crypto
./build_linux.sh crypto || ui_fail
cp crypto.so ../../release/deploy/lib/native/libobjk_crypto.so

ui_step "library: lame"
cd ../lame
./build_linux.sh lame || ui_fail
cp lame.so ../../release/deploy/lib/native/libobjk_lame.so

ui_step "library: matrix (ml)"
cd ../matrix
./build_linux.sh matrix || ui_fail
cp matrix.so ../../release/deploy/lib/native/libobjk_ml.so

ui_step "library: opencv"
cd ../opencv
./build_linux.sh opencv || ui_fail
cp opencv.so ../../release/deploy/lib/native/libobjk_opencv.so

ui_step "library: onnx (cpu)"
cd ../onnx/eq
# The vendored libonnxruntime here is a CPU-ONLY build -- it reports only
# CPUExecutionProvider despite living under eq/cuda/lib. Building with
# ONNX_EP_CUDA therefore made every session creation fail, and because the
# catch returned without setting the session handle it surfaced as a silently
# null session rather than an error. Link a CUDA-enabled onnxruntime before
# switching this back to cuda.
#
# Only an x86-64 runtime is vendored (cuda/lib/x64). Build and ship ONNX only
# for an architecture that has one: on arm64 the link fails, and the copies
# below put a 16 MB x86-64 libonnxruntime into the aarch64 tree -- which is what
# the v2026.9.1 linux-arm64 tarball carries, with no libobjk_onnx beside it.
# verify_native_libs.sh declares onnx optional on arm64 for as long as that holds.
if [ "$1" = "arm64" ]; then
	ORT_ARCH=arm64
else
	ORT_ARCH=x64
fi
if [ -d cuda/lib/$ORT_ARCH/lib ]; then
	./build.sh cpu || ui_fail
	cp libobjk_onnx.so ../../../release/deploy/lib/native/libobjk_onnx.so

	# copy ONNX Runtime shared libraries
	cp cuda/lib/$ORT_ARCH/lib/libonnxruntime.so.1.19.0 ../../../release/deploy/lib/native/libonnxruntime.so.1.19.0
	cd ../../../release/deploy/lib/native
	ln -sf libonnxruntime.so.1.19.0 libonnxruntime.so.1
	ln -sf libonnxruntime.so.1 libonnxruntime.so
	cd ../../../../lib/onnx/eq

	cp cuda/lib/$ORT_ARCH/lib/libonnxruntime_providers_shared.so ../../../release/deploy/lib/native/libonnxruntime_providers_shared.so
else
	echo "NOTE: no vendored ONNX Runtime for $ORT_ARCH (cuda/lib/$ORT_ARCH/lib) - libobjk_onnx is not built"
fi
ui_step "library: sdl"
cd ../../sdl
./build_linux.sh sdl || ui_fail
cp sdl.so ../../release/deploy/lib/native/libobjk_sdl.so
cp lib/fonts/*.ttf ../../release/deploy/lib/sdl/fonts

ui_step "library: diags"
cd ../diags
./build_linux.sh diags || ui_fail
cp diags.so ../../release/deploy/lib/native/libobjk_diags.so

ui_step "verify native libraries"
# Every native library this script builds must be present (and, on Linux,
# resolvable) before the tree is packaged. Without this a failed build was
# dropped from the release with a green exit -- the obu incident, again. The
# result has to be acted on: this script has no 'set -e', and before the
# '|| exit 1' below v2026.9.1 printed "native-library verification failure(s)"
# on both Linux legs and shipped anyway. verify_native_libs.sh records which
# libraries each platform requires, and why onnx is optional on arm64.
if [ "$1" = "arm64" ]; then
	NATIVE_LIBS="crypto diags lame ml odbc opencv sdl --optional onnx"
else
	NATIVE_LIBS="crypto diags lame ml odbc onnx opencv sdl"
fi
sh ../../release/verify_native_libs.sh ../../release/deploy/lib/native so $NATIVE_LIBS || exit 1

ui_step "launchers (obb, obn)"
cd ../../utils/launcher
if [ ! -z "$1" ] && [ "$1" = "arm64" ]; then
	make -f make/Makefile.obb.arm64 clean; make -f make/Makefile.obb.arm64 -j3 || ui_fail
else
	make -f make/Makefile.obb.amd64 clean; make -f make/Makefile.obb.amd64 -j3 || ui_fail
fi
cp obb ../../release/deploy/bin

if [ ! -z "$1" ] && [ "$1" = "arm64" ]; then
	make -f make/Makefile.obn.arm64 clean; make -f make/Makefile.obn.arm64 -j3 || ui_fail
else
	make -f make/Makefile.obn.amd64 clean; make -f make/Makefile.obn.amd64 -j3 || ui_fail
fi
cp obn ../../release/deploy/lib/native/misc/

cp ../../vm/misc/config.prop ../../release/deploy/lib/native/misc

ui_step "updater (obu)"
# build updater
# obu ships in bin/ like every other user-facing tool; without this the release
# advertises 'obu check/update/rollback' and delivers no binary.
cd ../updater
if [ ! -z "$1" ] && [ "$1" = "arm64" ]; then
	make -f make/Makefile.arm64 clean; make -f make/Makefile.arm64 -j3 || ui_fail
else
	make -f make/Makefile.amd64 clean; make -f make/Makefile.amd64 -j3 || ui_fail
fi
cp obu ../../release/deploy/bin
# This script does not use 'set -e', so a failed make or cp would otherwise be
# ignored and produce a tree with no obu -- which is exactly how v2026.8.0
# shipped without it. Fail loudly instead.
if [ ! -f ../../release/deploy/bin/obu ]; then
	echo "ERROR: obu was not built or copied - aborting deploy"
	exit 1
fi

cd ../../release

ui_step "documentation"
# copy docs
cd ../..
cp -R docs/syntax core/release/deploy/doc/syntax
cp docs/readme.html core/release/deploy
cp docs/style/readme.css core/release/deploy/doc

cp LICENSE core/release/deploy

# Ship the dependency installer INSIDE the distribution. Linux links
# libobjk_sdl.so against the system SDL2 and libGL and ships neither, so the
# person who needs this script is precisely the person who downloaded a tarball
# and never cloned the repo.
cp tools/install_deps.sh core/release/deploy
chmod +x core/release/deploy/install_deps.sh
unzip -q docs/api.zip -d core/release/deploy/doc

ui_step "examples"
# copy examples
mkdir core/release/deploy/examples
mkdir core/release/deploy/examples/media
cp programs/deploy/*.obs core/release/deploy/examples
cp programs/deploy/README.md core/release/deploy/examples
# The OpenGL examples live in programs/examples, which nothing copied, so no
# distribution ever carried them.
mkdir -p core/release/deploy/examples/opengl
cp programs/examples/gl_*.obs core/release/deploy/examples/opengl
cp programs/examples/cube_gl.obs core/release/deploy/examples/opengl
cp programs/examples/gl_crystal.obj core/release/deploy/examples/opengl
# The data files two examples read. Windows has copied these since forever
# (deploy_windows.cmd), but neither POSIX script did, so json_stream_23.obs
# ("data/weather.json") and neural_21.obs ("data/gender.csv") failed with a
# file-not-found on every Linux and macOS distribution.
mkdir -p core/release/deploy/examples/data
cp programs/deploy/data/* core/release/deploy/examples/data
cp programs/deploy/media/*.png core/release/deploy/examples/media
cp programs/deploy/media/*.wav core/release/deploy/examples/media

cd core/release

# deploy
if [ ! -z "$2" ] && [ "$2" = "deploy" ]; then
	ui_step "package tarball"
	mkdir -p ~/Desktop
	rm -rf ~/Desktop/objeck*
	cp -rf deploy ~/Desktop/objeck-lang
	cd ~/Desktop
	
	rm -f objeck.tar objeck.tgz
	tar cf objeck.tar objeck-lang
	gzip objeck.tar

	if [ ! -z "$1" ] && [ "$1" = "arm64" ]; then
		mv objeck.tar.gz objeck-linux-arm64_0.0.0.tgz
	else
		mv objeck.tar.gz objeck-linux-x64_0.0.0.tgz
	fi
fi

ui_ok "deploy complete"
