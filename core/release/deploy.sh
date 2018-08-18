#!/bin/sh

# setup directories
rm -rf deploy
mkdir deploy
mkdir deploy/bin
mkdir deploy/lib
mkdir deploy/lib/native
mkdir deploy/doc

rm -rf deploy_fcgi
mkdir deploy_fcgi
mkdir deploy_fcgi/bin
mkdir deploy_fcgi/lib
mkdir deploy_fcgi/lib/native
mkdir deploy_fcgi/doc

# build compiler
cd ../compiler
if [ ! -z "$1" ] && [ "$1" = "32" ]; then
	cp make/Makefile.32 Makefile
elif [ ! -z "$1" ] && [ "$1" = "osx" ]; then
	cp make/Makefile.OSX.64 Makefile
else
	cp make/Makefile.64 Makefile
fi
make clean; make -j3 OBJECK_LIB_PATH=\\\".\\\"
cp obc ../release/deploy/bin
cp ../lib/*.obl ../release/deploy/lib
cp ../vm/misc/*.pem ../release/deploy/lib
rm ../release/deploy/lib/gtk2.obl
# rm ../release/deploy/lib/query.obl

# build VM
cd ../vm
if [ ! -z "$1" ] && [ "$1" = "32" ]; then
	cp make/Makefile.32 Makefile
elif [ ! -z "$1" ] && [ "$1" = "osx" ]; then
	cp make/Makefile.OSX.64 Makefile
else 
	cp make/Makefile.64 Makefile
fi
make clean; make -j3
cp obr ../release/deploy/bin

if [ ! -z "$1" ] && [ "$1" = "32" ]; then
	cp make/Makefile.FCGI32 Makefile
elif [ ! -z "$1" ] && [ "$1" = "64" ]; then	
	cp make/Makefile.FCGI64 Makefile
fi
make clean; make -j3

# build debugger
cd ../debugger
if [ ! -z "$1" ] && [ "$1" = "32" ]; then
	cp make/Makefile.32 Makefile
elif [ ! -z "$1" ] && [ "$1" = "osx" ]; then
	cp make/Makefile.OSX.64 Makefile
else
	cp make/Makefile.64 Makefile
fi
make clean; make -j3
cp obd ../release/deploy/bin

# build libraries
cd ../lib/odbc
if [ ! -z "$1" ] && [ "$1" = "osx" ]; then
	./build_osx_x64.sh odbc
	cp odbc.dylib ../../release/deploy/lib/native/libobjk_odbc.dylib
else
	./build_linux.sh odbc
	cp odbc.so ../../release/deploy/lib/native/libobjk_odbc.so
fi

cd ../openssl

if [ ! -z "$1" ] && [ "$1" = "osx" ]; then
	./build_osx_x64.sh openssl
	cp openssl.dylib ../../release/deploy/lib/native/libobjk_openssl.dylib
else
	./build_linux.sh openssl
	cp openssl.so ../../release/deploy/lib/native/libobjk_openssl.so
fi

cd ../sdl

if [ ! -z "$1" ] && [ "$1" = "osx" ]; then
	./build_osx_x64.sh sdl
	cp sdl.dylib ../../release/deploy/lib/native/libobjk_sdl.dylib
else
	./build_linux.sh sdl
	cp sdl.so ../../release/deploy/lib/native/libobjk_sdl.so
fi

if [ ! -z "$1" ] && [ "$1" != "osx" ]; then
	cd ../fcgi
	./build_linux.sh
fi

# copy docs
cd ../../..
cp docs/guide/objeck_lang.pdf core/release/deploy/doc
cp -R docs/syntax core/release/deploy/doc/syntax
cp docs/readme.htm core/release/deploy
unzip docs/api.zip -d core/release/deploy/doc

# copy examples
mkdir core/release/deploy/examples
cp programs/deploy/*.obs core/release/deploy/examples
cp -aR programs/doc core/release/deploy/examples
cp -aR programs/tiny core/release/deploy/examples

cd core/release
if [ ! -z "$1" ] && [ "$1" != "osx" ]; then
	# create and build fcgi
	cp ../lib/fcgi.obl deploy/lib
	cp -Rfu deploy/* deploy_fcgi
	rm deploy_fcgi/bin/obc
	rm deploy_fcgi/bin/obd
	rm -rf deploy_fcgi/doc
	rm -rf deploy_fcgi/examples
	cp ../vm/obr_fcgi deploy_fcgi/bin
	cp ../lib/fcgi/*.so deploy_fcgi/lib/native
	mkdir deploy_fcgi/examples
	cp -R ../../programs/web/* deploy_fcgi/examples
	cp ../../docs/fcgi_readme.htm deploy_fcgi/readme.htm
	mkdir deploy_fcgi/fcgi_readme_files
	cp ../../docs/fcgi_readme_files/* deploy_fcgi/fcgi_readme_files
fi

# deploy
if [ ! -z "$2" ] && [ "$2" = "deploy" ]; then
	rm -rf ~/Desktop/objeck-lang
	cp -rf ../release/deploy ~/Desktop/objeck-lang
	cd ~/Desktop
	
	rm -f objeck.tar objeck.tgz
	tar cf objeck.tar objeck-lang
	gzip objeck.tar
	mv objeck.tar.gz objeck.tgz	
fi;
