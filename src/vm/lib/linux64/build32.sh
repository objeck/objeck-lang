#/bin/sh
rm -rf *.o
g++ -fPIC -g -c -Wno-deprecated -Wall $1.cpp
g++ -shared -fPIC -Wl,-soname,$1.so.1 -o $1.so.1.0.1 $1.o -lc
