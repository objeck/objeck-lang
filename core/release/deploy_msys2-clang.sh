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

cd ../matrix
./build_msys2-clang.sh matrix
cp matrix.dll ../../release/deploy-msys2-clang/lib/native/libobjk_ml.dll

cd ../sdl
./build_msys2-clang.sh sdl
cp sdl.dll ../../release/deploy-msys2-clang/lib/native/libobjk_sdl.dll
cp lib/fonts/*.ttf ../../release/deploy-msys2-clang/lib/sdl/fonts

cd ../diags
./build_msys2-clang.sh diags
cp diags.dll ../../release/deploy-msys2-clang/lib/native/libobjk_diags.dll

cd ../../utils/launcher
make -f make/Makefile.obb.msys2-clang.amd64 clean; make -f make/Makefile.obb.msys2-clang.amd64 -j3
cp obb.exe ../../release/deploy-msys2-clang/bin

make -f make/Makefile.obn.msys2-clang.amd64 clean; make -f make/Makefile.obn.msys2-clang.amd64 -j3
cp obn.exe ../../release/deploy-msys2-clang/lib/native/misc

cp ../../vm/misc/config.prop ../../release/deploy-msys2-clang/lib/native/misc
cd ../../release

# copy docs
cd ../..
cp -R docs/syntax core/release/deploy-msys2-clang/doc/syntax
cp docs/readme.html core/release/deploy-msys2-clang
cp docs/doc/readme.css core/release/deploy-msys2-clang/doc

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
	rm -rf ~/Desktop/objeck*
	cp -rf ../release/deploy-msys2-clang ~/Desktop/objeck-lang
	cd ~/Desktop
	
	rm -f objeck.tar objeck.tgz
	tar cf objeck.tar objeck-lang
	gzip objeck.tar

	mv objeck.tar.gz objeck-utils-msys2-x64_0.0.0.tgz
fi
