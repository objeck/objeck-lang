#!/bin/sh

if [ $# -eq 0 ]; then
	echo "os: 64(linux amd64); rpi(linux arm64); macos (arm64)"
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
if [ ! -z "$1" ] && [ "$1" = "rpi" ]; then
	cp make/Makefile.arm64 Makefile
elif [ ! -z "$1" ] && [ "$1" = "macos" ]; then
	cp make/Makefile.macos.amd64 Makefile
else
	cp make/Makefile.amd64 Makefile
fi
make clean; make -j3 OBJECK_LIB_PATH=///".///"
cp obc ../release/deploy/bin
cp ../lib/*.obl ../release/deploy/lib
cp ../vm/misc/*.pem ../release/deploy/lib
rm ../release/deploy/lib/gtk2.obl

# build VM
cd ../vm
if [ ! -z "$1" ] && [ "$1" = "rpi" ]; then
	cp make/Makefile.arm64 Makefile
elif [ ! -z "$1" ] && [ "$1" = "macos" ]; then
	cp make/Makefile.macos.amd64 Makefile
else 
	cp make/Makefile.amd64 Makefile
fi
make clean; make -j3
cp obr ../release/deploy/bin

make clean; make -j3

# build debugger
cd ../debugger
if [ ! -z "$1" ] && [ "$1" = "rpi" ]; then
	cp make/Makefile.arm64 Makefile
elif [ ! -z "$1" ] && [ "$1" = "macos" ]; then
	cp make/Makefile.macos.amd64 Makefile
else
	cp make/Makefile.amd64 Makefile
fi
make clean; make -j3
cp obd ../release/deploy/bin

# build libraries
cd ../lib/odbc
if [ ! -z "$1" ] && [ "$1" = "macos" ]; then
	./build_osx_x64.sh odbc
	cp odbc.dylib ../../release/deploy/lib/native/libobjk_odbc.dylib
else
	./build_linux.sh odbc
	cp odbc.so ../../release/deploy/lib/native/libobjk_odbc.so
fi

cd ../openssl

if [ ! -z "$1" ] && [ "$1" = "macos" ]; then
	./build_osx_x64.sh openssl
	cp openssl.dylib ../../release/deploy/lib/native/libobjk_openssl.dylib
else
	./build_linux.sh openssl
	cp openssl.so ../../release/deploy/lib/native/libobjk_openssl.so
fi

cd ../sdl

if [ ! -z "$1" ] && [ "$1" = "macos" ]; then
	./build_osx_x64.sh sdl
	cp sdl.dylib ../../release/deploy/lib/native/libobjk_sdl.dylib
else
	./build_linux.sh sdl
	cp sdl.so ../../release/deploy/lib/native/libobjk_sdl.so
fi
cp lib/fonts/*.ttf ../../release/deploy/lib/sdl/fonts

cd ../diags

if [ ! -z "$1" ] && [ "$1" = "macos" ]; then
	./build_osx_x64.sh diags
	cp diags.dylib ../../release/deploy/lib/native/libobjk_diags.dylib
else
	./build_linux.sh diags
	cp diags.so ../../release/deploy/lib/native/libobjk_diags.so
fi

cd ../../native_launcher
if [ ! -z "$1" ] && [ "$1" = "rpi" ]; then
	make -f make/Makefile.obb.arm64 clean; make -f make/Makefile.obb.arm64
else
	make -f make/Makefile.obb.amd64 clean; make -f make/Makefile.obb.amd64
fi
cp obb ../release/deploy/bin

if [ ! -z "$1" ] && [ "$1" = "rpi" ]; then
	make -f make/Makefile.obn.arm64 clean; make -f make/Makefile.obn.arm64
else
	make -f make/Makefile.obn.amd64 clean; make -f make/Makefile.obn.amd64
fi
cp obn ../release/deploy/lib/native/misc/

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

	if [ ! -z "$1" ] && [ "$1" = "rpi" ]; then
		mv objeck.tar.gz objeck-linux-arm64_0.0.0.tgz
	elif [ ! -z "$1" ] && [ "$1" = "macos" ]; then
		mv objeck.tar.gz objeck-macos-x64_0.0.0.tgz
	else
		mv objeck.tar.gz objeck-linux-x64_0.0.0.tgz
	fi
fi
