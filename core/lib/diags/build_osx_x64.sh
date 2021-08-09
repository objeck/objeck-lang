#/bin/sh
rm -rf *.o *.dylib
g++ -D_X64 -D_OSX -shared -Wno-unused-function -Wno-deprecated-declarations -fPIC -c -O3 -Wall -Imacos/include $1.cpp  ../../compiler/scanner.cpp ../../compiler/parser.cpp ../../compiler/context.cpp ../../compiler/tree.cpp ../../compiler/types.cpp
g++ -D_X64 -D_OSX -dynamiclib -L/usr/local/lib -Wl,-headerpad_max_install_names,-undefined,dynamic_lookup,-compatibility_version,1.0,-current_version,1.0 -o $1.dylib $1.o