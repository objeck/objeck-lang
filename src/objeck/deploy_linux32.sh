#!/bin/sh

# setup directories
rm -rf deploy
mkdir deploy
mkdir deploy/bin
mkdir deploy/bin/lib
mkdir deploy/doc
mkdir deploy/examples
# build compiler
cd ../compiler

if [ ! -z "$1" ] && [ "$1" = "32" ]; then
	cp Makefile.32 Makefile
else
	cp Makefile.64 Makefile
fi

make clean; make -j3
cp obc ../objeck/deploy/bin
cp *.obl ../objeck/deploy/bin
rm ../objeck/deploy/fcgi.obl
# build utilities
cd ../utilities

if [ ! -z "$1" ] && [ "$1" = "32" ]; then
	cp Makefile.32 Makefile
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
else
	cp Makefile.64 Makefile
	cp os/posix/Makefile.64 os/posix/Makefile
	cp jit/amd64/Makefile.64 jit/amd64/Makefile
fi

make clean; make -j3
cp obr ../objeck/deploy/bin
# build debugger

if [ ! -z "$1" ] && [ "$1" = "32" ]; then
	cp Makefile.obd32 Makefile
else
	cp Makefile.obd64 Makefile
fi

cd debugger

if [ ! -z "$1" ] && [ "$1" = "32" ]; then
	cp Makefile.32 Makefile
else
	cp Makefile.64 Makefile
fi

make clean; make -j3
cp obd ../../objeck/deploy/bin
# build libaries
cd ../lib/odbc
./build_linux.sh odbc
cp odbc.so ../../../objeck/deploy/bin/lib
cd ../openssl
./build_linux.sh openssl
cp openssl.so ../../../objeck/deploy/bin/lib
# copy guide
cd ../../../..
cp docs/guide/objeck_lang.pdf src/objeck/deploy/doc
cp docs/readme.rtf src/objeck/deploy
# copy examples
cp -rf src/compiler/rc/* src/objeck/deploy/examples

# deploy
if [ ! -z "$2" ] && [ "$2" = "deploy" ]; then
	rm -rf ~/Desktop/objeck-lang
	cp -rf src/objeck/deploy ~/Desktop/objeck-lang
	cd ~/Desktop
	rm -f objeck.tar objeck.tgz
	tar cf objeck.tar objeck-lang
	gzip objeck.tar
	mv objeck.tar.gz objeck.tgz
fi;


