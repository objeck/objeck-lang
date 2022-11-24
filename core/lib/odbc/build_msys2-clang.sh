#/bin/sh
rm -rf *.o
rm -rf *.dll

clang++ -O3 -Wall -std=c++17 -fPIC -c *$1.cpp -Wno-unused-function -Wno-int-to-void-pointer-cast
clang++ -O3 -shared -undefined dynamic_lookup -o $1.dll *.o -lodbc