#/bin/sh
rm -rf *.o
rm -rf *.dll

clang++ -O3 -Wall -D_DIAG_LIB -D_X64 -D_WIN32 -D_DIAG_LIB -D_MSYS2 -std=c++17 -fPIC -c *$1.cpp ../../compiler/scanner.cpp ../../compiler/parser.cpp ../../compiler/context.cpp ../../compiler/tree.cpp ../../compiler/types.cpp ../../compiler/linker.cpp -Wno-unused-function -Wno-unused-variable
clang++ -O3 -shared -Wl,-undefined,dynamic_lookup -o $1.dll *.o -lmbedtls -lmbedx509 -lmbedcrypto -lz -pthread