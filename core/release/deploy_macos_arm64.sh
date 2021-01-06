#!/bin/sh

# setup directories
rm -rf deploy
mkdir deploy
mkdir deploy/bin
mkdir deploy/lib
mkdir deploy/lib/sdl
mkdir deploy/lib/sdl/fonts
mkdir deploy/lib/native
mkdir deploy/doc

# build compiler
cd ../compiler
xcodebuild -project xcode/Compiler.xcodeproj clean build
cp xcode/build/Release/obc ../release/deploy/bin
cp ../lib/*.obl ../release/deploy/lib
cp ../vm/misc/*.pem ../release/deploy/lib
rm ../release/deploy/lib/gtk2.obl

# build VM
cd ../vm
xcodebuild -project xcode/VM.xcodeproj clean build
cp xcode/build/Release/obr ../release/deploy/bin

# build debugger
cd ../debugger
xcodebuild -project xcode/Debugger.xcodeproj clean build
cp xcode/build/Release/obd ../release/deploy/bin

# build libraries
cd ../lib/openssl
xcodebuild -project macos/xcode/objk_openssl.xcodeproj clean build
cp macos/xcode/build/Release/libobjk_openssl.dylib ../../release/deploy/lib/native/libobjk_openssl.dylib

cd ../sdl
xcodebuild -project macos/xcode/SDL2.xcodeproj clean build
cp macos/xcode/build/Release/libobjk_openssl.dylib ../../release/deploy/lib/native/libobjk_sdl.dylib
cp lib/fonts/*.ttf ../../release/deploy/lib/sdl/fonts

cd ../odbc
xcodebuild -project macos/xcode/ODBC.xcodeproj clean build
cp macos/xcode/build/Release/libobjk_odbc.dylib ../../release/deploy/lib/native/libobjk_odbc.dylib

# copy docs
cd ../../..
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
if [ ! -z "$1" ] && [ "$1" = "deploy" ]; then
	rm -rf ~/Desktop/objeck-lang
	cp -rf ../release/deploy ~/Desktop/objeck-lang
	cd ~/Desktop
	
	rm -f objeck.tar objeck.tgz
	tar cf objeck.tar objeck-lang
	gzip objeck.tar
	mv objeck.tar.gz objeck.tgz	
fi;
