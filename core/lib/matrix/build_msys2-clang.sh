#!/bin/sh
# set -e: a failed compile must not fall through to the link, which then
# reports `cannot find *.o` and names the glob instead of the real error.
set -e

rm -rf *.o
rm -rf *.dll

clang++ $(pkg-config --cflags eigen3) -O3 -D_MSYS2 -std=c++17 -Wall -fPIC -c *$1.cpp -Wno-unused-function -D_MSYS2_CLANG
clang++ -O3 -shared -Wl,-undefined,dynamic_lookup -o $1.dll *.o -lmbedtls -lmbedx509 -lmbedcrypto