#/bin/sh

rm -rf *.o
rm -rf *.so

g++ $(pkg-config --cflags eigen3) -O3 -std=c++11 -Wall -fPIC -c *$1.cpp -Wno-unused-function -Wno-deprecated-declarations -Wno-maybe-uninitialized
g++ -O3 -shared -Wl,-soname,$1.so.1 -o $1.so *.o -lssl -lcrypto
