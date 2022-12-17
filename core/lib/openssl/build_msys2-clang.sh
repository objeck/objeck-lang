#/bin/sh
rm -rf *.o
rm -rf *.dll

clang++ -O3 -std=c++11 -Wall -fPIC -c *$1.cpp -Wno-unused-function -D_MSYS2_CLANG
clang++ -O3 -shared -Wl,-undefined,dynamic_lookup -o $1.dll *.o -lssl -lcrypto