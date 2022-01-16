#/bin/sh

BUILDDIR="/tmp/objeck-lang"
CWD=$(pwd)

# clean up
rm -rf $BUILDDIR

# make directories
mkdir $BUILDDIR
mkdir $BUILDDIR/src
mkdir $BUILDDIR/src/doc
mkdir $BUILDDIR/src/man
mkdir $BUILDDIR/src/lib
mkdir $BUILDDIR/src/lib/odbc
mkdir $BUILDDIR/src/lib/openssl
mkdir $BUILDDIR/src/objk_lib
mkdir $BUILDDIR/src/shared
mkdir $BUILDDIR/src/utilities
mkdir $BUILDDIR/src/compiler
mkdir $BUILDDIR/src/vm
mkdir $BUILDDIR/src/vm/debugger
mkdir $BUILDDIR/src/vm/os
mkdir $BUILDDIR/src/vm/os/posix
mkdir $BUILDDIR/src/vm/jit
if [ ! -z "$1" ] && [ "$1" = "32" ]; then
	mkdir $BUILDDIR/src/vm/jit/ia32
else
	mkdir $BUILDDIR/src/vm/jit/amd64
fi

# copy shared files
if [ ! -z "$1" ] && [ "$1" = "32" ]; then
    cp Makefile.32 $BUILDDIR/src/Makefile
else
    cp Makefile.64 $BUILDDIR/src/Makefile
fi

cp ../src/shared/*.h $BUILDDIR/src/shared

# copy utility files
cp ../src/utilities/*.cpp $BUILDDIR/src/utilities
if [ ! -z "$1" ] && [ "$1" = "32" ]; then
	cp ../src/utilities/Makefile.32 $BUILDDIR/src/utilities/Makefile
else
	cp ../src/utilities/Makefile.64 $BUILDDIR/src/utilities/Makefile
fi

# copy compiler files
cp ../src/compiler/*.h $BUILDDIR/src/compiler
cp ../src/compiler/*.cpp $BUILDDIR/src/compiler
if [ ! -z "$1" ] && [ "$1" = "32" ]; then
	cp ../src/compiler/Makefile.Debian.32 $BUILDDIR/src/compiler/Makefile
else
	cp ../src/compiler/Makefile.Debian.64 $BUILDDIR/src/compiler/Makefile
fi

# copy vm files
cp ../src/vm/*.h $BUILDDIR/src/vm
cp ../src/vm/*.cpp $BUILDDIR/src/vm
if [ ! -z "$1" ] && [ "$1" = "32" ]; then
	cp ../src/vm/Makefile.Debian.32 $BUILDDIR/src/vm/Makefile
else
	cp ../src/vm/Makefile.Debian.64 $BUILDDIR/src/vm/Makefile
fi
cp ../src/vm/Makefile.obd32 $BUILDDIR/src/vm
cp ../src/vm/Makefile.obd64 $BUILDDIR/src/vm

cp ../src/vm/os/posix/*.h $BUILDDIR/src/vm/os/posix
cp ../src/vm/os/posix/*.cpp $BUILDDIR/src/vm/os/posix
if [ ! -z "$1" ] && [ "$1" = "32" ]; then
	cp ../src/vm/os/posix/Makefile.32 $BUILDDIR/src/vm/os/posix/Makefile
else
	cp ../src/vm/os/posix/Makefile.64 $BUILDDIR/src/vm/os/posix/Makefile
fi

if [ ! -z "$1" ] && [ "$1" = "32" ]; then
	cp ../src/vm/jit/ia32/*.h $BUILDDIR/src/vm/jit/ia32
	cp ../src/vm/jit/ia32/*.cpp $BUILDDIR/src/vm/jit/ia32
	cp ../src/vm/jit/ia32/Makefile.32 $BUILDDIR/src/vm/jit/ia32/Makefile
else
	cp ../src/vm/jit/amd64/*.h $BUILDDIR/src/vm/jit/amd64
	cp ../src/vm/jit/amd64/*.cpp $BUILDDIR/src/vm/jit/amd64
	cp ../src/vm/jit/amd64/Makefile.64 $BUILDDIR/src/vm/jit/amd64/Makefile
fi

# copy debugger files
cp ../src/vm/debugger/*.h $BUILDDIR/src/vm/debugger
cp ../src/vm/debugger/*.cpp $BUILDDIR/src/vm/debugger
if [ ! -z "$1" ] && [ "$1" = "32" ]; then
	cp ../src/vm/debugger/Makefile.Debian.32 $BUILDDIR/src/vm/debugger/Makefile
else
	cp ../src/vm/debugger/Makefile.Debian.64 $BUILDDIR/src/vm/debugger/Makefile
fi

# copy odbc library
cp ../src/lib/odbc/*.h $BUILDDIR/src/lib/odbc
cp ../src/lib/odbc/*.cpp $BUILDDIR/src/lib/odbc
cp ../src/lib/odbc/Makefile.Debian $BUILDDIR/src/lib/odbc/Makefile

# copy openssl library
cp ../src/lib/openssl/*.cpp $BUILDDIR/src/lib/openssl
cp ../src/lib/openssl/Makefile.Debian $BUILDDIR/src/lib/openssl/Makefile

# copy language libraries
cp ../src/compiler/lang.obl $BUILDDIR/src/objk_lib
cp ../src/compiler/collect.obl $BUILDDIR/src/objk_lib
cp ../src/compiler/regex.obl $BUILDDIR/src/objk_lib
cp ../src/compiler/encrypt.obl $BUILDDIR/src/objk_lib
cp ../src/compiler/odbc.obl $BUILDDIR/src/objk_lib

# man pages
cp ../docs/man/*1 $BUILDDIR/src/man

# api and examples
cp ../docs/readme.htm $BUILDDIR/src/doc
unzip ../docs/api.zip -d $BUILDDIR/src/doc
cd $BUILDDIR/src/doc
tar cf api.tar api
gzip api.tar
mv api.tar.gz api.tgz
rm -rf api
cd $CWD
cd ../src/compiler/rc
tar cf examples.tar *
mv examples.tar $BUILDDIR/src/doc
cd $CWD
gzip $BUILDDIR/src/doc/examples.tar
mv $BUILDDIR/src/doc/examples.tar.gz $BUILDDIR/src/doc/examples.tgz

# create upstream archive
cd $BUILDDIR
tar cf objeck-lang.tar *
gzip objeck-lang.tar
bzr dh-make objeck-lang 6.3-0 objeck-lang.tar.gz 
cd objeck-lang
rm debian/README.Debian

cp -rf $CWD/files/* debian
if [ ! -z "$1" ] && [ "$1" = "32" ]; then
	mv debian/control.32 debian/control
	rm debian/control.64
	mv debian/changelog.32 debian/changelog
	rm debian/changelog.64
else
	mv debian/control.64 debian/control
	rm debian/control.32
	mv debian/changelog.64 debian/changelog
	rm debian/changelog.32
fi

bzr add debian/source/format
bzr commit -m "Initial commit"
bzr builddeb -- -us -uc
cp -f ../*.deb $CWD
