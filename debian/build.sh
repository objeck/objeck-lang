#/bin/sh

# clean up
rm -rf ~/Documents/tmp

# make directories
mkdir ~/Documents/tmp
mkdir ~/Documents/tmp/src
mkdir ~/Documents/tmp/src/man
mkdir ~/Documents/tmp/src/shared
mkdir ~/Documents/tmp/src/utilities
mkdir ~/Documents/tmp/src/compiler
mkdir ~/Documents/tmp/src/vm
mkdir ~/Documents/tmp/src/vm/debugger
mkdir ~/Documents/tmp/src/vm/os
mkdir ~/Documents/tmp/src/vm/os/posix
mkdir ~/Documents/tmp/src/vm/jit
mkdir ~/Documents/tmp/src/vm/jit/amd64
mkdir ~/Documents/tmp/src/vm/jit/ia32

# copy shared files
if [ ! -z "$1" ] && [ "$1" = "32" ]; then
    cp Makefile.32 ~/Documents/tmp/src/Makefile
else
    cp Makefile.64 ~/Documents/tmp/src/Makefile
fi

cp ../src/shared/*.h ~/Documents/tmp/src/shared

# copy utility files
cp ../src/utilities/*.cpp ~/Documents/tmp/src/utilities
if [ ! -z "$1" ] && [ "$1" = "32" ]; then
	cp ../src/utilities/Makefile.32 ~/Documents/tmp/src/utilities/Makefile
else
	cp ../src/utilities/Makefile.64 ~/Documents/tmp/src/utilities/Makefile
fi

# copy compiler files
cp ../src/compiler/*.h ~/Documents/tmp/src/compiler
cp ../src/compiler/*.cpp ~/Documents/tmp/src/compiler
if [ ! -z "$1" ] && [ "$1" = "32" ]; then
	cp ../src/compiler/Makefile.32 ~/Documents/tmp/src/compiler/Makefile
else
	cp ../src/compiler/Makefile.64 ~/Documents/tmp/src/compiler/Makefile
fi

# copy vm files
cp ../src/vm/*.h ~/Documents/tmp/src/vm
cp ../src/vm/*.cpp ~/Documents/tmp/src/vm
if [ ! -z "$1" ] && [ "$1" = "32" ]; then
	cp ../src/vm/Makefile.32 ~/Documents/tmp/src/vm/Makefile
else
	cp ../src/vm/Makefile.64 ~/Documents/tmp/src/vm/Makefile
fi
cp ../src/vm/Makefile.obd32 ~/Documents/tmp/src/vm
cp ../src/vm/Makefile.obd64 ~/Documents/tmp/src/vm

cp ../src/vm/os/posix/*.h ~/Documents/tmp/src/vm/os/posix
cp ../src/vm/os/posix/*.cpp ~/Documents/tmp/src/vm/os/posix
if [ ! -z "$1" ] && [ "$1" = "32" ]; then
	cp ../src/vm/os/posix/Makefile.32 ~/Documents/tmp/src/vm/os/posix/Makefile
else
	cp ../src/vm/os/posix/Makefile.64 ~/Documents/tmp/src/vm/os/posix/Makefile
fi

if [ ! -z "$1" ] && [ "$1" = "32" ]; then
	cp ../src/vm/jit/ia32/*.h ~/Documents/tmp/src/vm/jit/ia32
	cp ../src/vm/jit/ia32/*.cpp ~/Documents/tmp/src/vm/jit/ia32
	cp ../src/vm/jit/ia32/Makefile.32 ~/Documents/tmp/src/vm/jit/ia32/Makefile
else
	cp ../src/vm/jit/amd64/*.h ~/Documents/tmp/src/vm/jit/amd64
	cp ../src/vm/jit/amd64/*.cpp ~/Documents/tmp/src/vm/jit/amd64
	cp ../src/vm/jit/amd64/Makefile.64 ~/Documents/tmp/src/vm/jit/amd64/Makefile
fi

# copy debugger files
cp ../src/vm/debugger/*.h ~/Documents/tmp/src/vm/debugger
cp ../src/vm/debugger/*.cpp ~/Documents/tmp/src/vm/debugger
if [ ! -z "$1" ] && [ "$1" = "32" ]; then
	cp ../src/vm/debugger/Makefile.32 ~/Documents/tmp/src/vm/debugger/Makefile
else
	cp ../src/vm/debugger/Makefile.64 ~/Documents/tmp/src/vm/debugger/Makefile
fi

# libraries

# man pages
cp ../docs/man/*1 ~/Documents/tmp/src/man

# create upstream archive
cd ~/Documents/tmp
tar cf objeck-lang.tar *
gzip objeck-lang.tar
bzr dh-make objeck-lang 3.3.5-2 objeck-lang.tar.gz
cd objeck-lang
# cp ~/Documents/Code/objeck-lang/debian/rules debian
rm debian/*ex debian/*EX
cp -rf ~/Documents/Code/objeck-lang/debian/files/* debian
bzr add debian/source/format
bzr commit -m "Initial commit of Debian packaging."
bzr builddeb -- -us -uc
