#!/bin/sh

if [ $# -eq 0 ]; then
	echo "Linux targets: x64 or arm64"
	exit 1
fi

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

# build compiler
cd ../compiler
if [ ! -z "$1" ] && [ "$1" = "arm64" ]; then
	cp make/Makefile.arm64 Makefile
else
	cp make/Makefile.amd64 Makefile
fi
make clean; make -j3 OBJECK_LIB_PATH=///".///"
cp obc ../release/deploy/bin
cp ../lib/*.obl ../release/deploy/lib
cp ../lib/*.ini ../release/deploy/lib
cp ../vm/misc/*.pem ../release/deploy/lib

# build VM
cd ../vm
if [ ! -z "$1" ] && [ "$1" = "arm64" ]; then
	cp make/Makefile.arm64 Makefile
else 
	cp make/Makefile.amd64 Makefile
fi
make clean; make -j3
cp obr ../release/deploy/bin

# build debugger
cd ../debugger
if [ ! -z "$1" ] && [ "$1" = "arm64" ]; then
	cp make/Makefile.arm64 Makefile
else
	cp make/Makefile.amd64 Makefile
fi
make clean; make -j3
cp obd ../release/deploy/bin

# build repl
cd ../repl
if [ ! -z "$1" ] && [ "$1" = "arm64" ]; then
	cp make/Makefile.arm64 Makefile
else
	cp make/Makefile.amd64 Makefile
fi
make clean; make -j3
cp obi ../release/deploy/bin

# build libraries
cd ../lib/odbc
./build_linux.sh odbc
cp odbc.so ../../release/deploy/lib/native/libobjk_odbc.so

cd ../crypto
./build_linux.sh crypto
cp crypto.so ../../release/deploy/lib/native/libobjk_crypto.so

cd ../lame
./build_linux.sh lame
cp lame.so ../../release/deploy/lib/native/libobjk_lame.so

cd ../matrix
./build_linux.sh matrix
cp matrix.so ../../release/deploy/lib/native/libobjk_ml.so

cd ../opencv
./build_linux.sh opencv
cp opencv.so ../../release/deploy/lib/native/libobjk_opencv.so

cd ../onnx/eq/cuda
./build_linux.sh onnx_cuda
cp onnx_cuda.so ../../../../release/deploy/lib/native/libobjk_onnx.so

cd ../../../sdl
./build_linux.sh sdl
cp sdl.so ../../release/deploy/lib/native/libobjk_sdl.so
cp lib/fonts/*.ttf ../../release/deploy/lib/sdl/fonts

cd ../diags
./build_linux.sh diags
cp diags.so ../../release/deploy/lib/native/libobjk_diags.so

cd ../../utils/launcher
if [ ! -z "$1" ] && [ "$1" = "arm64" ]; then
	make -f make/Makefile.obb.arm64 clean; make -f make/Makefile.obb.arm64 -j3
else
	make -f make/Makefile.obb.amd64 clean; make -f make/Makefile.obb.amd64 -j3
fi
cp obb ../../release/deploy/bin

if [ ! -z "$1" ] && [ "$1" = "arm64" ]; then
	make -f make/Makefile.obn.arm64 clean; make -f make/Makefile.obn.arm64 -j3
else
	make -f make/Makefile.obn.amd64 clean; make -f make/Makefile.obn.amd64 -j3
fi
cp obn ../../release/deploy/lib/native/misc/

cp ../../vm/misc/config.prop ../../release/deploy/lib/native/misc
cd ../../release

# copy docs
cd ../..
cp -R docs/syntax core/release/deploy/doc/syntax
cp docs/readme.html core/release/deploy

cp LICENSE core/release/deploy
unzip docs/api.zip -d core/release/deploy/doc

# copy examples
mkdir core/release/deploy/examples
mkdir core/release/deploy/examples/media
cp programs/deploy/*.obs core/release/deploy/examples
cp programs/deploy/media/*.png core/release/deploy/examples/media
cp programs/deploy/media/*.wav core/release/deploy/examples/media

cd core/release

# deploy
if [ ! -z "$2" ] && [ "$2" = "deploy" ]; then
	rm -rf ~/Desktop/objeck*
	cp -rf ../release/deploy ~/Desktop/objeck-lang
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
