#/bin/sh
rm -rf *.o *.dylib
g++ -D_X64 -shared -fPIC -g -c -Wall $1.cpp
g++ -D_X64 -dynamiclib -Wl,-headerpad_max_install_names,-undefined,dynamic_lookup,-compatibility_version,1.0,-current_version,1.0 -o $.dylib $1.o
