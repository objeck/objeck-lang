#/bin/sh

CC=g++
LIB=lib_foo

export PATH=$PATH:../../../core/release/deploy/bin
export OBJECK_LIB_PATH=../../../core/release/deploy/lib

rm -rf *.o *.so *.obe
rm ../../../core/release/deploy/lib/native/$LIB.so

$CC -O3 -Wall -D_DIAG_LIB -D_X64 -D_DIAG_LIB -D_MSYS2 -std=c++17 -fPIC -Wno-unused-function -Wno-unused-variable -c *$LIB.cpp
$CC -O3 -shared -Wl,-soname,$LIB.so.1 -o $LIB.so *.o -lssl -lcrypto -ldl -lz -pthread
cp $LIB.so ../../../core/release/deploy/lib/native/

obc foo; obr foo
