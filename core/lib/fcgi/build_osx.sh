#/bin/sh
rm -rf *.o *.dylib
# g++ -D_X64 -D_OSX -shared -fPIC -c -g -D_DEBUG -Wall $1.cpp
g++ -D_X64 -D_OSX -shared -fPIC -c -O3 -Wall -I/usr/include/apache2 -I/usr/include/apr-1.0 -I/usr/include/openssl -I/usr/include/xmltok $1.cpp
g++ -D_X64 -dynamiclib -lodbc -Wl,-headerpad_max_install_names,-undefined,dynamic_lookup,-compatibility_version,1.0,-current_version,1.0 -o $1.dylib $1.o
