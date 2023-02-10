#!/bin/sh

# setup directories
rm -rf deploy-cygwin
mkdir deploy-cygwin
mkdir deploy-cygwin/bin
mkdir deploy-cygwin/lib
mkdir deploy-cygwin/lib/sdl
mkdir deploy-cygwin/lib/sdl/fonts
mkdir deploy-cygwin/lib/native
mkdir deploy-cygwin/lib/native/misc
mkdir deploy-cygwin/doc

# build compiler
cd ../compiler
cp make/Makefile.amd64 Makefile

make clean; make -j1 OBJECK_LIB_PATH=///".///"
cp obc ../release/deploy-cygwin/bin
cp ../lib/*.obl ../release/deploy-cygwin/lib
cp ../vm/misc/*.pem ../release/deploy-cygwin/lib

# build VM
cd ../vm
cp make/Makefile.amd64 Makefile

make clean; make -j1
cp obr ../release/deploy-cygwin/bin

# build debugger
cd ../debugger
cp make/Makefile.amd64 Makefile

make clean; make -j1
cp obd ../release/deploy-cygwin/bin

# build libraries
cd ../lib/odbc
# ./build_cygwin.sh odbc
# cp odbc.dll ../../release/deploy-cygwin/lib/native/libobjk_odbc.dll

cd ../openssl
./build_cygwin.sh openssl
cp openssl.dll ../../release/deploy-cygwin/lib/native/libobjk_openssl.dll

cd ../sdl
# ./build_cygwin.sh sdl
# cp sdl.dll ../../release/deploy-cygwin/lib/native/libobjk_sdl.dll
# cp lib/fonts/*.ttf ../../release/deploy-cygwin/lib/sdl/fonts

cd ../diags
./build_cygwin.sh diags
cp diags.dll ../../release/deploy-cygwin/lib/native/libobjk_diags.dll

cd ../../utils/launcher

make -f make/Makefile.obb.amd64 clean; make -f make/Makefile.obb.amd64
cp obb ../../release/deploy-cygwin/bin

make -f make/Makefile.obn.amd64 clean; make -f make/Makefile.obn.amd64
cp obn ../../release/deploy-cygwin/lib/native/misc/

cp ../../vm/misc/config.prop ../../release/deploy-cygwin/lib/native/misc
cd ../../release

# copy docs
cd ../..
cp -R docs/syntax core/release/deploy-cygwin/doc/syntax
cp docs/readme.html core/release/deploy-cygwin
cp LICENSE core/release/deploy-cygwin
7z x docs/api.zip -ocore/release/deploy-cygwin/doc

# copy examples
mkdir core/release/deploy-cygwin/examples
mkdir core/release/deploy-cygwin/examples/media
cp programs/deploy/*.obs core/release/deploy-cygwin/examples
cp programs/deploy/media/*.png core/release/deploy-cygwin/examples/media
cp programs/deploy/media/*.wav core/release/deploy-cygwin/examples/media

cd core/release

# deploy
if [ ! -z "$2" ] && [ "$2" = "deploy-cygwin" ]; then
	rm -rf ~/Desktop/objeck*
	cp -rf ../release/deploy-cygwin ~/Desktop/objeck-lang
	cd ~/Desktop
	
	rm -f objeck.tar objeck.tgz
	tar cf objeck.tar objeck-lang
	gzip objeck.tar

	mv objeck.tar.gz objeck-cygwin-x64_0.0.0.tgz
fi
