#/bin/sh

CC=g++

rm -rf *.o
rm -rf *.dll

$CC -O3 -Wall -D_DIAG_LIB -D_X64 -D_WIN32 -D_DIAG_LIB -D_MSYS2 -std=c++17  -fPIC -Wno-unused-function -Wno-unused-variable -c *lib_foo.cpp
$CC -O3 -shared -Wl,-soname,$1.dll.1 -o lib_foo.dll *.o -lssl -lcrypto -lz -pthread