#!/bin/sh

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
xcodebuild -project xcode/Compiler.xcodeproj clean build
cp xcode/build/Release/obc ../release/deploy/bin
cp ../lib/*.obl ../release/deploy/lib
cp ../lib/*.ini ../release/deploy/lib
cp ../vm/misc/*.pem ../release/deploy/lib

# build VM
cd ../vm
xcodebuild -project xcode/VM.xcodeproj clean build
cp xcode/build/Release/obr ../release/deploy/bin

# build debugger
cd ../debugger
xcodebuild -project xcode/Debugger.xcodeproj clean build
cp xcode/build/Release/obd ../release/deploy/bin

# build module library
cd ../module
xcodebuild -project xcode/module.xcodeproj clean build

# build repl
cd ../repl
xcodebuild -project xcode/repl.xcodeproj clean build
cp xcode/build/Release/obi ../release/deploy/bin

# build native launcher
cd ../utils/launcher
xcodebuild -project "xcode/Native Launcher.xcodeproj" -target obb clean build
cp xcode/build/Release/obb ../../release/deploy/bin

xcodebuild -project "xcode/Native Launcher.xcodeproj" -target obn clean build
cp xcode/build/Release/obn ../../release/deploy/lib/native/misc
cp ../../vm/misc/config.prop ../../release/deploy/lib/native/misc

# build libraries
cd ../../lib/openssl
xcodebuild -project macos/xcode/objk_openssl.xcodeproj clean build
cp macos/xcode/build/Release/libobjk_openssl.dylib ../../release/deploy/lib/native/libobjk_openssl.dylib

cd ../sdl
xcodebuild -project macos/xcode/sdl.xcodeproj build
cp macos/xcode/build/Release/libxcode.dylib ../../release/deploy/lib/native/libobjk_sdl.dylib
cp macos/sdl2_arm64.tgz ../../release/deploy/lib/native
cp lib/fonts/*.ttf ../../release/deploy/lib/sdl/fonts

cd ../odbc
xcodebuild -project macos/xcode/ODBC.xcodeproj clean build
cp macos/xcode/build/Release/libobjk_odbc.dylib ../../release/deploy/lib/native/libobjk_odbc.dylib

cd ../lame
xcodebuild -project macos/lame.xcodeproj clean build
cp macos//build/Release/libobjk_lame.dylib ../../release/deploy/lib/native/libobjk_lame.dylib

cd ../opencv
xcodebuild -project macos/objk_opencv.xcodeproj -target objk_opencv clean build
cp macos/build/Release/libobjk_opencv.dylib ../../release/deploy/lib/native/libobjk_opencv.dylib

cd ../matrix
xcodebuild -project macos/xcode/matrix.xcodeproj clean build
cp macos/xcode/build/Release/libxcode.dylib ../../release/deploy/lib/native/libobjk_ml.dylib

cd ../diags
xcodebuild -project macos/xcode/objk_diags.xcodeproj clean build
cp macos/xcode/build/Release/libobjk_diags.dylib ../../release/deploy/lib/native/libobjk_diags.dylib
Architecturesg
# copy docs
cd ../../..
cp -R docs/syntax core/release/deploy/doc/syntax
cp docs/readme.html core/release/deploy
cp docs/doc/readme.css core/release/deploy/doc
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
if [ ! -z "$1" ] && [ "$1" = "deploy" ]; then
	rm -rf ~/Desktop/objeck-lang
	cp -rf ../release/deploy ~/Desktop/objeck-lang
	rm ~/Desktop/objeck-lang/.*
	cd ~/Desktop
	
	rm -f objeck.tar objeck.tgz
	tar cf objeck.tar objeck-lang
	gzip objeck.tar
	mv objeck.tar.gz objeck-macos-arm64_0.0.0.tgz	
fi;
