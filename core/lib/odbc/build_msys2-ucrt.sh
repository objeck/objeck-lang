#!/bin/sh
# set -e: a failed compile must not fall through to the link, which then
# reports `cannot find *.o` and names the glob instead of the real error.
set -e

rm -rf *.o
rm -rf *.dll

g++ -O3 -Wno-int-to-pointer-cast -Wall -std=c++20 -fPIC -c *$1.cpp -Wno-unused-function -Wno-address
g++ -O3 -shared -Wl,-soname,$1.dll.1 -o $1.dll *.o -lodbc