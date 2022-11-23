#!/bin/sh

# setup directories
rm -rf deploy
mkdir deploy
mkdir deploy/bin
mkdir deploy/lib
mkdir deploy/lib/sdl
mkdir deploy/lib/sdl/fonts
mkdir deploy/lib/native
mkdir deploy/lib/msys2-ucrt64
mkdir deploy/lib/native/misc
mkdir deploy/doc

# build compiler
cd ../compiler
cp make/Makefile.msys2-ucrt.amd64 Makefile

make clean; make -j3 OBJECK_LIB_PATH=///".///"
cp obc ../release/deploy/bin
cp ../lib/*.obl ../release/deploy/lib
cp ../vm/misc/*.pem ../release/deploy/lib
rm ../release/deploy/lib/gtk2.obl

# build VM
cd ../vm
cp make/Makefile.msys2-ucrt.amd64 Makefile

make clean; make -j3
cp obr ../release/deploy/bin

make clean; make -j3

# build debugger
cd ../debugger
cp make/Makefile.msys2-ucrt.amd64 Makefile

make clean; make -j3
cp obd ../release/deploy/bin

# copy msys2 libs
unzip ../lib/msys2-ucrt64/msys2-ucrt64.zip -d ../release/deploy/lib/msys2-ucrt64

# build libraries

cd ../lib/odbc
./build_win_msys2.sh odbc
cp odbc.dll ../../release/deploy/lib/native/libobjk_odbc.dll

cd ../openssl
./build_win_msys2.sh openssl
cp openssl.dll ../../release/deploy/lib/native/libobjk_openssl.dll

cd ../sdl
./build_win_msys2.sh sdl
cp sdl.dll ../../release/deploy/lib/native/libobjk_sdl.dll
cp lib/fonts/*.ttf ../../release/deploy/lib/sdl/fonts

cd ../diags
./build_win_msys2.sh diags
cp diags.dll ../../release/deploy/lib/native/libobjk_diags.dll

cd ../../native_launcher
make -f make/Makefile.obb.msys2-ucrt.amd64 clean; make -f make/Makefile.obb.msys2-ucrt.amd64
cp obb.exe ../release/deploy/bin

make -f make/Makefile.obn.msys2-ucrt.amd64 clean; make -f make/Makefile.obn.msys2-ucrt.amd64
cp obn.exe ../release/deploy/lib/native/misc

cp ../vm/misc/config.prop ../release/deploy/lib/native/misc
cd ../release

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
cp -aR programs/doc core/release/deploy/examples
cp -aR programs/tiny core/release/deploy/examples

cd core/release

# deploy
if [ ! -z "$2" ] && [ "$2" = "deploy" ]; then
	rm -rf ~/Desktop/objeck*
	cp -rf ../release/deploy ~/Desktop/objeck-lang
	cd ~/Desktop
	
	rm -f objeck.tar objeck.tgz
	tar cf objeck.tar objeck-lang
	gzip objeck.tar

	mv objeck.tar.gz objeck-windows-msys2-x64_0.0.0.tgz
fi
