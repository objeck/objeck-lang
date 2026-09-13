#!/bin/sh

# setup directories
rm -rf deploy-msys2-ucrt
mkdir deploy-msys2-ucrt
mkdir deploy-msys2-ucrt/bin
mkdir deploy-msys2-ucrt/lib
mkdir deploy-msys2-ucrt/lib/sdl
mkdir deploy-msys2-ucrt/lib/sdl/fonts
mkdir deploy-msys2-ucrt/lib/native
mkdir deploy-msys2-ucrt/lib/native/misc
mkdir deploy-msys2-ucrt/doc

# build compiler
cd ../compiler
cp make/Makefile.msys2-ucrt.amd64 Makefile

make clean; make -j3 OBJECK_LIB_PATH=///".///"
cp obc ../release/deploy-msys2-ucrt/bin
cp ../lib/*.obl ../release/deploy-msys2-ucrt/lib
cp ../lib/*.ini ../release/deploy-msys2-ucrt/lib
cp ../vm/misc/*.pem ../release/deploy-msys2-ucrt/lib

# build VM
cd ../vm
cp make/Makefile.msys2-ucrt.amd64 Makefile

make clean; make -j3
cp obr ../release/deploy-msys2-ucrt/bin

# build debugger
cd ../debugger
cp make/Makefile.msys2-ucrt.amd64 Makefile

make clean; make -j3
cp obd ../release/deploy-msys2-ucrt/bin

# build repl
cd ../repl
cp make/Makefile.msys2-ucrt.amd64 Makefile

make clean; make -j3
cp obi ../release/deploy-msys2-ucrt/bin

# build libraries
cd ../lib/odbc
./build_msys2-ucrt.sh odbc
cp odbc.dll ../../release/deploy-msys2-ucrt/lib/native/libobjk_odbc.dll

cd ../crypto
./build_msys2-ucrt.sh crypto
cp crypto.dll ../../release/deploy-msys2-ucrt/lib/native/libobjk_crypto.dll

cd ../lame
./build_msys2-ucrt.sh lame
cp lame.dll ../../release/deploy-msys2-ucrt/lib/native/libobjk_lame.dll

cd ../matrix
./build_msys2-ucrt.sh matrix
cp matrix.dll ../../release/deploy-msys2-ucrt/lib/native/libobjk_ml.dll

cd ../opencv
./build_msys2-ucrt.sh opencv
cp opencv.dll ../../release/deploy-msys2-ucrt/lib/native/libobjk_opencv.dll

cd ../sdl
./build_msys2-ucrt.sh sdl
cp sdl.dll ../../release/deploy-msys2-ucrt/lib/native/libobjk_sdl.dll
cp lib/fonts/*.ttf ../../release/deploy-msys2-ucrt/lib/sdl/fonts

cd ../diags
./build_msys2-ucrt.sh diags
cp diags.dll ../../release/deploy-msys2-ucrt/lib/native/libobjk_diags.dll

# Every native library this script builds must be present (and, on Linux,
# resolvable) before the tree is packaged. Without this a failed build was
# dropped from the release with a green exit -- the obu incident, again.
sh ../../release/verify_native_libs.sh ../../release/deploy-msys2-ucrt/lib/native dll crypto diags lame ml odbc onnx opencv sdl

cd ../onnx/eq
# The vendored libonnxruntime here is a CPU-ONLY build -- it reports only
# CPUExecutionProvider despite living under eq/cuda/lib. Building with
# ONNX_EP_CUDA therefore made every session creation fail, and because the
# catch returned without setting the session handle it surfaced as a silently
# null session rather than an error. Link a CUDA-enabled onnxruntime before
# switching this back to cuda.
./build.sh cpu
cp libobjk_onnx.dll ../../../release/deploy-msys2-ucrt/lib/native/libobjk_onnx.dll

cd ../../utils/launcher
make -f make/Makefile.obb.msys2-ucrt.amd64 clean; make -f make/Makefile.obb.msys2-ucrt.amd64 -j3
cp obb.exe ../../release/deploy-msys2-ucrt/bin

make -f make/Makefile.obn.msys2-ucrt.amd64 clean; make -f make/Makefile.obn.msys2-ucrt.amd64 -j3
cp obn.exe ../../release/deploy-msys2-ucrt/lib/native/misc

cp ../../vm/misc/config.prop ../../release/deploy-msys2-ucrt/lib/native/misc

# build updater
# obu needs no msys2-specific makefile: it links no zlib and carries no windres
# resource, which is all the msys2 obb/obn variants add. MinGW appends .exe to
# the makefile's EXE=obu on its own, exactly as it does for obb.
cd ../updater
make -f make/Makefile.amd64 clean; make -f make/Makefile.amd64 -j3
cp obu.exe ../../release/deploy-msys2-ucrt/bin
# No 'set -e' here, so a failed make or cp would silently yield a tree with no
# obu -- how v2026.8.0 shipped without it. Fail loudly instead.
if [ ! -f ../../release/deploy-msys2-ucrt/bin/obu.exe ]; then
	echo "ERROR: obu was not built or copied - aborting deploy"
	exit 1
fi

cd ../../release

# copy docs
cd ../..
cp -R docs/syntax core/release/deploy-msys2-ucrt/doc/syntax
cp docs/readme.html core/release/deploy-msys2-ucrt
cp docs/style/readme.css core/release/deploy-msys2-ucrt/doc

cp LICENSE core/release/deploy-msys2-ucrt
unzip docs/api.zip -d core/release/deploy-msys2-ucrt/doc

# copy examples
mkdir core/release/deploy-msys2-ucrt/examples
mkdir core/release/deploy-msys2-ucrt/examples/media
cp programs/deploy/*.obs core/release/deploy-msys2-ucrt/examples
cp programs/deploy/README.md core/release/deploy-msys2-ucrt/examples
# This script builds libobjk_sdl against opengl32 and ships its fonts, but copied
# none of the examples that use them, nor the data two examples read. Ship the
# same set as the Windows and POSIX deploys.
mkdir -p core/release/deploy-msys2-ucrt/examples/opengl
cp programs/examples/gl_*.obs core/release/deploy-msys2-ucrt/examples/opengl
cp programs/examples/cube_gl.obs core/release/deploy-msys2-ucrt/examples/opengl
cp programs/examples/gl_crystal.obj core/release/deploy-msys2-ucrt/examples/opengl
mkdir -p core/release/deploy-msys2-ucrt/examples/data
cp programs/deploy/data/* core/release/deploy-msys2-ucrt/examples/data
cp programs/deploy/media/*.png core/release/deploy-msys2-ucrt/examples/media
cp programs/deploy/media/*.wav core/release/deploy-msys2-ucrt/examples/media
sh core/release/verify_example_assets.sh core/release/deploy-msys2-ucrt/examples || exit 1

cd core/release

# deploy
if [ ! -z "$2" ] && [ "$2" = "deploy" ]; then
	mkdir -p ~/Desktop
	rm -rf ~/Desktop/objeck*
	cp -rf ../release/deploy-msys2-ucrt ~/Desktop/objeck-lang
	cd ~/Desktop

	rm -f objeck.tar objeck.tgz
	tar cf objeck.tar objeck-lang
	gzip objeck.tar

	mv objeck.tar.gz objeck-utils-msys2-x64_0.0.0.tgz
fi
