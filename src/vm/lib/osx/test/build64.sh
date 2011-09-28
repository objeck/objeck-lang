#/bin/sh
rm -rf *.o *.dylib
g++ -D_X64 -shared -D_X64 -fPIC -g -c -Wno-deprecated -Wall $1.cpp
g++ -D_X64 -dynamiclib -D_X64 -Wl,-headerpad_max_install_names,-undefined,dynamic_lookup,-compatibility_version,1.0,-current_version,1.0 -o $1.dylib $1.o
