#/bin/sh
rm -rf *.o *.dylib
# g++ -D_X64 -shared -fPIC -c -g -D_DEBUG -Wall $1.cpp
g++ -D_X64 -D_OSX -fPIC -c -O3  -shared -Wno-unused-function -Wno-deprecated-declarations -Wall -Imacos/include $1.cpp
g++ -D_X64 -D_OSX -dynamiclib -L/usr/local/lib -Wl,-headerpad_max_install_names,-undefined,dynamic_lookup,-compatibility_version,1.0,-current_version,1.0 -o $1.dylib $1.o
