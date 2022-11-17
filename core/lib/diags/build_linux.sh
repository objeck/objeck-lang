#/bin/sh
rm -rf *.o
rm -rf *.so

g++ -O3 -D_DIAG_LIB -std=c++17 -Wall -Wno-unused-function -fPIC -c *$1.cpp ../../compiler/scanner.cpp ../../compiler/parser.cpp ../../compiler/context.cpp ../../compiler/tree.cpp ../../compiler/types.cpp ../../compiler/linker.cpp
g++ -O3 -shared -Wl,-soname,$1.so.1 -o $1.so *.o -lssl -lcrypto -ldl -lz -pthread