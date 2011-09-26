#/bin/sh
rm -rf *.o *.so*
g++  -g -c -Wno-deprecated -Wall -D_MINGW $1.cpp
g++ -shared -Wl,-soname,$1.so.1 -o $1.so $1.o
