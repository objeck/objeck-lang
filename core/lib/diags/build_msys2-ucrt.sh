#/bin/sh
rm -rf *.o
rm -rf *.dll

g++ -O3 -D_DIAG_LIB -D_X64 -D_WIN32 -D_DIAG_LIB -D_MSYS2 -std=c++17 -Wall -Wno-unused-function -Wno-unused-variable -fPIC -c *$1.cpp ../../compiler/scanner.cpp ../../compiler/parser.cpp ../../compiler/context.cpp ../../compiler/tree.cpp ../../compiler/types.cpp ../../compiler/linker.cpp
g++ -O3 -shared -Wl,-soname,$1.dll.1 -o $1.dll *.o -lssl -lcrypto -lz -pthread