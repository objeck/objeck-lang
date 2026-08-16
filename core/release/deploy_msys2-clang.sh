#!/bin/sh

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

# build compiler
cd ../compiler
cp make/Makefile.msys2-clang.amd64 Makefile

make clean; make -j3 OBJECK_LIB_PATH=///".///"
cp obc ../release/deploy-msys2-clang/bin
cp ../lib/*.obl ../release/deploy-msys2-clang/lib
cp ../lib/*.ini ../release/deploy-msys2-clang/lib
cp ../vm/misc/*.pem ../release/deploy-msys2-clang/lib

# build VM
cd ../vm
cp make/Makefile.msys2-clang.amd64 Makefile

make clean; make -j3
cp obr ../release/deploy-msys2-clang/bin

# build debugger
cd ../debugger
cp make/Makefile.msys2-clang.amd64 Makefile

make clean; make -j3
cp obd ../release/deploy-msys2-clang/bin

# build repl
cd ../repl
cp make/Makefile.msys2-clang.amd64 Makefile

make clean; make -j3
cp obi ../release/deploy-msys2-clang/bin

# build libraries
cd ../lib/odbc
./build_msys2-clang.sh odbc
cp odbc.dll ../../release/deploy-msys2-clang/lib/native/libobjk_odbc.dll

cd ../crypto
./build_msys2-clang.sh crypto
cp crypto.dll ../../release/deploy-msys2-clang/lib/native/libobjk_crypto.dll

cd ../lame
./build_msys2-clang.sh lame
cp lame.dll ../../release/deploy-msys2-clang/lib/native/libobjk_lame.dll

cd ../matrix
./build_msys2-clang.sh matrix
cp matrix.dll ../../release/deploy-msys2-clang/lib/native/libobjk_ml.dll

cd ../opencv
./build_msys2-clang.sh opencv
cp opencv.dll ../../release/deploy-msys2-clang/lib/native/libobjk_opencv.dll

cd ../sdl
./build_msys2-clang.sh sdl
cp sdl.dll ../../release/deploy-msys2-clang/lib/native/libobjk_sdl.dll
cp lib/fonts/*.ttf ../../release/deploy-msys2-clang/lib/sdl/fonts

cd ../diags
./build_msys2-clang.sh diags
cp diags.dll ../../release/deploy-msys2-clang/lib/native/libobjk_diags.dll

cd ../onnx/eq
CXX=clang++ # The vendored libonnxruntime here is a CPU-ONLY build -- it reports only
# CPUExecutionProvider despite living under eq/cuda/lib. Building with
# ONNX_EP_CUDA therefore made every session creation fail, and because the
# catch returned without setting the session handle it surfaced as a silently
# null session rather than an error. Link a CUDA-enabled onnxruntime before
# switching this back to cuda.
./build.sh cpu
cp libobjk_onnx.dll ../../../release/deploy-msys2-clang/lib/native/libobjk_onnx.dll

cd ../../utils/launcher
make -f make/Makefile.obb.msys2-clang.amd64 clean; make -f make/Makefile.obb.msys2-clang.amd64 -j3
cp obb.exe ../../release/deploy-msys2-clang/bin

make -f make/Makefile.obn.msys2-clang.amd64 clean; make -f make/Makefile.obn.msys2-clang.amd64 -j3
cp obn.exe ../../release/deploy-msys2-clang/lib/native/misc

cp ../../vm/misc/config.prop ../../release/deploy-msys2-clang/lib/native/misc

# build updater
# obu needs no msys2-specific makefile: it links no zlib and carries no windres
# resource, which is all the msys2 obb/obn variants add. MinGW appends .exe to
# the makefile's EXE=obu on its own, exactly as it does for obb.
cd ../updater
make -f make/Makefile.amd64 clean; make -f make/Makefile.amd64 -j3
cp obu.exe ../../release/deploy-msys2-clang/bin
# No 'set -e' here, so a failed make or cp would silently yield a tree with no
# obu -- how v2026.8.0 shipped without it. Fail loudly instead.
if [ ! -f ../../release/deploy-msys2-clang/bin/obu.exe ]; then
	echo "ERROR: obu was not built or copied - aborting deploy"
	exit 1
fi

cd ../../release

# copy docs
cd ../..
cp -R docs/syntax core/release/deploy-msys2-clang/doc/syntax
cp docs/readme.html core/release/deploy-msys2-clang
cp docs/style/readme.css core/release/deploy-msys2-clang/doc

cp LICENSE core/release/deploy-msys2-clang
unzip docs/api.zip -d core/release/deploy-msys2-clang/doc

# copy examples
mkdir core/release/deploy-msys2-clang/examples
mkdir core/release/deploy-msys2-clang/examples/media
cp programs/deploy/*.obs core/release/deploy-msys2-clang/examples
cp programs/deploy/media/*.png core/release/deploy-msys2-clang/examples/media
cp programs/deploy/media/*.wav core/release/deploy-msys2-clang/examples/media

cd core/release

# deploy
if [ ! -z "$2" ] && [ "$2" = "deploy" ]; then
	mkdir -p ~/Desktop
	rm -rf ~/Desktop/objeck*
	cp -rf ../release/deploy-msys2-clang ~/Desktop/objeck-lang
	cd ~/Desktop

	rm -f objeck.tar objeck.tgz
	tar cf objeck.tar objeck-lang
	gzip objeck.tar

	mv objeck.tar.gz objeck-utils-msys2-x64_0.0.0.tgz
fi
