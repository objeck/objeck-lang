#/bin/sh

rm -rf *.o *.dylib
g++ -D_X64 -D_OSX -fPIC -c -O3 -Wall -Imacos/include $1.cpp -Wno-unused-function -Wno-deprecated-declarations
g++ -D_X64 -D_OSX -shared -dynamiclib -L/usr/local/lib -Wl,-headerpad_max_install_names,-undefined,dynamic_lookup,-compatibility_version,1.0,-current_version,1.0 -o $1.dylib $1.o
