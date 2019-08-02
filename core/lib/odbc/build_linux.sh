#/bin/sh
rm -rf *.o
rm -rf *.so
g++ -O3 -Wno-int-to-pointer-cast -Wno-unused-function -Wall -std=c++11 -fPIC -c *$1.cpp
g++ -O3 -shared -Wl,-soname,$1.so.1 -o $1.so *.o -lodbc
# g++ -g -D_DEBUG -Wno-unused-function -Wall -fPIC -c *$1.cpp
# g++ -g -D_DEBUG -shared -Wl,-soname,$1.so.1 -o $1.so *.o -lodbc
