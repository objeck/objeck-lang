#/bin/sh

CC=g++
LIB=lib_foo
DEPLOY_DIR=deploy-msys2-ucrt

rm -rf *.o *.dll *.obe
rm ../../../core/release/$DEPLOY_DIR/lib/native/$LIB.dll

$CC -O3 -Wall -D_DIAG_LIB -D_X64 -D_WIN32 -D_DIAG_LIB -D_MSYS2 -std=c++17 -fPIC -Wno-unused-function -Wno-unused-variable -c *lib_foo.cpp
$CC -O3 -shared -Wl,-soname,$LIB.dll.1 -o $LIB.dll *.o -lssl -lcrypto -lz -pthread
cp $LIB.dll ../../../core/release/$DEPLOY_DIR/lib/native/

obc foo && obr foo