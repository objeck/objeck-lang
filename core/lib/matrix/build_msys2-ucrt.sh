#/bin/sh
rm -rf *.o
rm -rf *.dll

g++ $(pkg-config --cflags eigen3) -O3 -D_MSYS2 -std=c++11 -Wall -fPIC -c *$1.cpp -Wno-unused-function -Wno-address
g++ -O3 -shared -Wl,-soname,$1.so.1 -o $1.dll *.o -lmbedtls -lmbedx509 -lmbedcrypto