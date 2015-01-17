#/bin/sh
rm -rf *.o
rm -rf *.so
g++ -g -Wall -D_X64 -D_DEBUG -fPIC -c $1.cpp `pkg-config --cflags gtk+-2.0 pkg-config --libs gtk+-2.0`
g++ -g -shared -D_X64 -D_DEBUG -Wl,-soname,$1.so.1 -o $1.so $1.o `pkg-config --cflags gtk+-2.0 pkg-config --libs gtk+-2.0`
