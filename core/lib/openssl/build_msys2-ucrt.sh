#/bin/sh
rm -rf *.o
rm -rf *.dll

g++ -O3 -Wno-unused-function -std=c++11 -Wall -fPIC -c *$1.cpp
g++ -O3 -shared -Wl,-soname,$1.so.1 -o $1.dll *.o -lssl -lcrypto