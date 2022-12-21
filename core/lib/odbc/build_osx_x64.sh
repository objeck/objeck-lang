#/bin/sh
rm -rf *.o *.dylib
# g++ -D_X64 -shared -fPIC -c -g -D_DEBUG -Wall $1.cpp
g++ -D_X64 -D_OSX -shared -fPIC -c -O3 -Wall -I./macos/include -I./../openssl/macos/include $1.cpp -Wno-unused-function -Wno-unused-command-line-argument -Wno-int-to-void-pointer-cast
g++ -D_X64 -D_OSX -dynamiclib -Wl,-headerpad_max_install_names,-undefined,dynamic_lookup,-compatibility_version,1.0,-current_version,1.0 -o $1.dylib $1.o
