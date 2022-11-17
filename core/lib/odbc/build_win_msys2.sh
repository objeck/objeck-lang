#/bin/sh
rm -rf *.o
rm -rf *.dll

g++ -O3 -Wno-int-to-pointer-cast -Wno-unused-function -Wall -std=c++17 -fPIC -c *$1.cpp
g++ -O3 -shared -Wl,-soname,$1.dll.1 -o $1.dll *.o -lodbc