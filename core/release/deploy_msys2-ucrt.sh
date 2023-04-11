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
cp ../vm/misc/*.pem ../release/deploy-msys2-ucrt/lib

# build VM
cd ../vm
cp make/Makefile.msys2-ucrt.amd64 Makefile

make clean; make -j3
cp obr ../release/deploy-msys2-ucrt/bin

make clean; make -j3

# build debugger
cd ../debugger
cp make/Makefile.msys2-ucrt.amd64 Makefile

make clean; make -j3
cp obd ../release/deploy-msys2-ucrt/bin

# build libraries
cd ../lib/odbc
./build_msys2-ucrt.sh odbc
cp odbc.dll ../../release/deploy-msys2-ucrt/lib/native/libobjk_odbc.dll

cd ../openssl
./build_msys2-ucrt.sh openssl
cp openssl.dll ../../release/deploy-msys2-ucrt/lib/native/libobjk_openssl.dll

cd ../sdl
./build_msys2-ucrt.sh sdl
cp sdl.dll ../../release/deploy-msys2-ucrt/lib/native/libobjk_sdl.dll
cp lib/fonts/*.ttf ../../release/deploy-msys2-ucrt/lib/sdl/fonts

cd ../diags
./build_msys2-ucrt.sh diags
cp diags.dll ../../release/deploy-msys2-ucrt/lib/native/libobjk_diags.dll

cd ../../utils/launcher
make -f make/Makefile.obb.msys2-ucrt.amd64 clean; make -f make/Makefile.obb.msys2-ucrt.amd64
cp obb.exe ../../release/deploy-msys2-ucrt/bin

make -f make/Makefile.obn.msys2-ucrt.amd64 clean; make -f make/Makefile.obn.msys2-ucrt.amd64
cp obn.exe ../../release/deploy-msys2-ucrt/lib/native/misc

cp ../../vm/misc/config.prop ../../release/deploy-msys2-ucrt/lib/native/misc
cd ../../release

# copy docs
cd ../..
cp -R docs/syntax core/release/deploy-msys2-ucrt/doc/syntax
cp docs/readme.html core/release/deploy-msys2-ucrt
cp docs/doc/readme.css core/release/deploy-msys2-ucrt/doc

cp LICENSE core/release/deploy-msys2-ucrt
unzip docs/api.zip -d core/release/deploy-msys2-ucrt/doc

# copy examples
mkdir core/release/deploy-msys2-ucrt/examples
mkdir core/release/deploy-msys2-ucrt/examples/media
cp programs/deploy/*.obs core/release/deploy-msys2-ucrt/examples
cp programs/deploy/media/*.png core/release/deploy-msys2-ucrt/examples/media
cp programs/deploy/media/*.wav core/release/deploy-msys2-ucrt/examples/media

# copy dlls
cp /ucrt64/bin/libgcc_s_seh-1.dll core/release/deploy-msys2-ucrt/bin
cp /ucrt64/bin/libstdc++-6.dll core/release/deploy-msys2-ucrt/bin
cp /ucrt64/bin/libwinpthread-1.dll core/release/deploy-msys2-ucrt/bin
cp /ucrt64/bin/zlib1.dll core/release/deploy-msys2-ucrt/bin
cp /ucrt64/bin/libcrypto-3-x64.dll core/release/deploy-msys2-ucrt/bin
cp /ucrt64/bin/libssl-3-x64.dll core/release/deploy-msys2-ucrt/bin
cp /ucrt64/bin/libodbc-2.dll core/release/deploy-msys2-ucrt/bin
cp /ucrt64/bin/libiconv-2.dll core/release/deploy-msys2-ucrt/bin
cp /ucrt64/bin/libltdl-7.dll core/release/deploy-msys2-ucrt/bin
cp /ucrt64/bin/SDL2.dll core/release/deploy-msys2-ucrt/bin
cp /ucrt64/bin/SDL2_image.dll core/release/deploy-msys2-ucrt/bin
cp /ucrt64/bin/SDL2_mixer.dll core/release/deploy-msys2-ucrt/bin
cp /ucrt64/bin/SDL2_ttf.dll core/release/deploy-msys2-ucrt/bin
cp /ucrt64/bin/libjpeg-8.dll core/release/deploy-msys2-ucrt/bin
cp /ucrt64/bin/libjxl.dll core/release/deploy-msys2-ucrt/bin
cp /ucrt64/bin/libpng16-16.dll core/release/deploy-msys2-ucrt/bin
cp /ucrt64/bin/libtiff-6.dll core/release/deploy-msys2-ucrt/bin
cp /ucrt64/bin/libwebp-7.dll core/release/deploy-msys2-ucrt/bin
cp /ucrt64/bin/libmpg123-0.dll core/release/deploy-msys2-ucrt/bin
cp /ucrt64/bin/libopusfile-0.dll core/release/deploy-msys2-ucrt/bin
cp /ucrt64/bin/libfreetype-6.dll core/release/deploy-msys2-ucrt/bin
cp /ucrt64/bin/libharfbuzz-0.dll core/release/deploy-msys2-ucrt/bin
cp /ucrt64/bin/libbrotlidec.dll core/release/deploy-msys2-ucrt/bin
cp /ucrt64/bin/libbrotlienc.dll core/release/deploy-msys2-ucrt/bin
cp /ucrt64/bin/libhwy.dll core/release/deploy-msys2-ucrt/bin
cp /ucrt64/bin/liblcms2-2.dll core/release/deploy-msys2-ucrt/bin
cp /ucrt64/bin/libdeflate.dll core/release/deploy-msys2-ucrt/bin
cp /ucrt64/bin/libjbig-0.dll core/release/deploy-msys2-ucrt/bin
cp /ucrt64/bin/libLerc.dll core/release/deploy-msys2-ucrt/bin
cp /ucrt64/bin/liblzma-5.dll core/release/deploy-msys2-ucrt/bin
cp /ucrt64/bin/libzstd.dll core/release/deploy-msys2-ucrt/bin
cp /ucrt64/bin/libsharpyuv-0.dll core/release/deploy-msys2-ucrt/bin
cp /ucrt64/bin/libogg-0.dll core/release/deploy-msys2-ucrt/bin
cp /ucrt64/bin/libopus-0.dll core/release/deploy-msys2-ucrt/bin
cp /ucrt64/bin/libbz2-1.dll core/release/deploy-msys2-ucrt/bin
cp /ucrt64/bin/libglib-2.0-0.dll core/release/deploy-msys2-ucrt/bin
cp /ucrt64/bin/libgraphite2.dll core/release/deploy-msys2-ucrt/bin
cp /ucrt64/bin/libintl-8.dll core/release/deploy-msys2-ucrt/bin
cp /ucrt64/bin/libbrotlicommon.dll core/release/deploy-msys2-ucrt/bin
cp /ucrt64/bin/libpcre2-8-0.dll core/release/deploy-msys2-ucrt/bin

cd core/release

# deploy
if [ ! -z "$2" ] && [ "$2" = "deploy" ]; then
	rm -rf ~/Desktop/objeck*
	cp -rf ../release/deploy-msys2-ucrt ~/Desktop/objeck-lang
	cd ~/Desktop
	
	rm -f objeck.tar objeck.tgz
	tar cf objeck.tar objeck-lang
	gzip objeck.tar

	mv objeck.tar.gz objeck-utils-msys2-x64_0.0.0.tgz
fi
