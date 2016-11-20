#!/bin/sh

USER_HOME=C:\\Users\\rhollines
# set USER_HOME=C:\\Documents and Settings\\Administrator

# setup directories
rm -rf deploy
mkdir deploy
mkdir deploy/bin
mkdir deploy/lib
mkdir deploy/lib/objeck-lang
mkdir deploy/doc

rm -rf deploy_fcgi
mkdir deploy_fcgi
mkdir deploy_fcgi/bin
mkdir deploy_fcgi/lib
mkdir deploy_fcgi/lib/objeck-lang
mkdir deploy_fcgi/doc

# build compiler
cd ../compiler
if [ ! -z "$1" ] && [ "$1" = "32" ]; then
	cp Makefile.32 Makefile
elif [ ! -z "$1" ] && [ "$1" = "mingw" ]; then
	cp Makefile.MINGW Makefile
elif [ ! -z "$1" ] && [ "$1" = "osx" ]; then
	cp Makefile.OSX.64 Makefile
else
	cp Makefile.64 Makefile
fi
make clean; make -j3 OBJECK_LIB_PATH=\\\".\\\"
cp obc ../objeck/deploy/bin
cp *.obl ../objeck/deploy/bin
cp ../vm/*.pem ../objeck/deploy/bin
rm ../objeck/deploy/bin/gtk2.obl
rm ../objeck/deploy/bin/sdl.obl
rm ../objeck/deploy/bin/db.obl

# build utilities
cd ../utilities
if [ ! -z "$1" ] && [ "$1" = "32" ]; then
	cp Makefile.32 Makefile
elif [ ! -z "$1" ] && [ "$1" = "mingw" ]; then
	cp Makefile.MINGW Makefile
elif [ ! -z "$1" ] && [ "$1" = "osx" ]; then
	cp Makefile.OSX.64 Makefile
else
	cp Makefile.64 Makefile
fi
make clean; make -j3
cp obu ../objeck/deploy/bin

# build VM
cd ../vm
if [ ! -z "$1" ] && [ "$1" = "32" ]; then
	cp Makefile.32 Makefile
	cp os/posix/Makefile.32 os/posix/Makefile
	cp jit/ia32/Makefile.32 jit/ia32/Makefile
elif [ ! -z "$1" ] && [ "$1" = "osx" ]; then
	cp Makefile.OSX.64 Makefile
	cp os/posix/Makefile.OSX.64 os/posix/Makefile
	cp jit/amd64/Makefile.OSX.64 jit/amd64/Makefile
elif [ ! -z "$1" ] && [ "$1" = "mingw" ]; then
	cp Makefile.MINGW Makefile
	cp os/windows/Makefile.MINGW os/posix/Makefile
	cp jit/ia32/Makefile.MINGW jit/ia32/Makefile
else 
	cp Makefile.64 Makefile
	cp os/posix/Makefile.64 os/posix/Makefile
	cp jit/amd64/Makefile.64 jit/amd64/Makefile
fi
make clean; make -j3
cp obr ../objeck/deploy/bin

if [ ! -z "$1" ] && [ "$1" = "32" ]; then
	cp Makefile.FCGI32 Makefile
elif [ ! -z "$1" ] && [ "$1" = "64" ]; then	
	cp Makefile.FCGI64 Makefile
fi
make clean; make -j3

# build debugger
if [ ! -z "$1" ] && [ "$1" = "32" ]; then
	cp Makefile.obd32 Makefile
elif [ ! -z "$1" ] && [ "$1" = "osx" ]; then
	cp Makefile.OSX.obd64 Makefile	
elif [ ! -z "$1" ] && [ "$1" = "mingw" ]; then
	cp Makefile.obd.MINGW Makefile
else
	cp Makefile.obd64 Makefile
fi

cd debugger
if [ ! -z "$1" ] && [ "$1" = "32" ]; then
	cp Makefile.32 Makefile
elif [ ! -z "$1" ] && [ "$1" = "osx" ]; then
	cp Makefile.OSX.64 Makefile
elif [ ! -z "$1" ] && [ "$1" = "mingw" ]; then
	cp Makefile.MINGW Makefile
else
	cp Makefile.64 Makefile
fi
make clean; make -j3
cp obd ../../objeck/deploy/bin

# build libraries
cd ../../lib/odbc
if [ ! -z "$1" ] && [ "$1" = "osx" ]; then
	./build_osx_x64.sh odbc
	cp odbc.dylib ../../objeck/deploy/lib/objeck-lang/libobjk_odbc.dylib
elif [ ! -z "$1" ] && [ "$1" = "mingw" ]; then
	./build_win32.sh odbc
	cp odbc.so ../../objeck/deploy/lib/objeck-lang/libobjk_odbc.so
else
	./build_linux.sh odbc
	cp odbc.so ../../objeck/deploy/lib/objeck-lang/libobjk_odbc.so
fi

cd ../openssl

if [ ! -z "$1" ] && [ "$1" = "osx" ]; then
	./build_osx_x64.sh openssl
	cp openssl.dylib ../../objeck/deploy/lib/objeck-lang/libobjk_openssl.dylib
elif [ ! -z "$1" ] && [ "$1" = "mingw" ]; then
	./build_win32.sh openssl
	cp openssl.so ../../objeck/deploy/lib/objeck-lang/libobjk_openssl.so
else
	./build_linux.sh openssl
	cp openssl.so ../../objeck/deploy/lib/objeck-lang/libobjk_openssl.so
fi

cd ../fcgi
./build_linux.sh

# copy docs
cd ../../..
cp docs/guide/objeck_lang.pdf src/objeck/deploy/doc
cp -R docs/syntax src/objeck/deploy/doc/syntax
cp docs/readme.htm src/objeck/deploy
unzip docs/api.zip -d src/objeck/deploy/doc

# copy examples
cp -R src/compiler/rc src/objeck/deploy/examples

# create and build fcgi
cd src/objeck
cp ../compiler/fcgi.obl deploy/bin
cp -Rfu deploy/* deploy_fcgi
rm deploy_fcgi/bin/obc
rm deploy_fcgi/bin/obd
rm -rf deploy_fcgi/doc
rm -rf deploy_fcgi/examples
cp ../vm/obr_fcgi deploy_fcgi/bin
cp ../lib/fcgi/*.so deploy_fcgi/lib/objeck-lang
mkdir deploy_fcgi/examples
cp -R ../compiler/web/* deploy_fcgi/examples
cp ../../docs/fcgi_readme.htm deploy_fcgi/readme.htm
mkdir deploy_fcgi/fcgi_readme_files
cp ../../docs/fcgi_readme_files/* deploy_fcgi/fcgi_readme_files

# deploy
if [ ! -z "$2" ] && [ "$2" = "deploy" ]; then
	if [ ! -z "$1" ] && [ "$1" = "mingw" ]; then
		rm -rf "$USER_HOME\Desktop\objeck-lang"
		cp -rf src/objeck/deploy "$USER_HOME\Desktop\objeck-lang"
		cd "$USER_HOME\Desktop"
	else
		rm -rf ~/Desktop/objeck-lang
		cp -rf ../objeck/deploy ~/Desktop/objeck-lang
		cd ~/Desktop
	fi;

	if [ ! -z "$1" ] && [ "$1" = "mingw" ]; then
		rm -f objeck.zip
		"C:\Program Files\7-Zip\7z.exe" a -tzip objeck-lang
	else 
		rm -f objeck.tar objeck.tgz
		tar cf objeck.tar objeck-lang
		gzip objeck.tar
		mv objeck.tar.gz objeck.tgz
	fi;
fi;
