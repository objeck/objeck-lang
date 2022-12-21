#/bin/sh

rm -rf *.o *.dylib
clang++ -D_X64 -D_OSX -D_DIAG_LIB -Wno-unused-function -std=c++11 -Wno-deprecated-declarations -fPIC -c -O3 -I../openssl/macos/include $1.cpp ../../compiler/scanner.cpp ../../compiler/parser.cpp ../../compiler/context.cpp ../../compiler/tree.cpp ../../compiler/types.cpp -Wno-unused-variable -Wno-unused-function
clang++ -dynamiclib -L/usr/local/lib -Wl,-headerpad_max_install_names,-undefined,dynamic_lookup,-compatibility_version,1.0,-current_version,1.0 -o $1.dylib $1.o
