#!/bin/sh
# deploy_msys2-ucrt.sh has invoked this script since the MSYS2 deploy was
# written; it did not exist, so every MSYS2 archive shipped without
# libobjk_opencv.dll. Mirrors build_linux.sh's flags for the MSYS2 toolchain.
set -e
rm -rf *.o
rm -rf *.dll

g++ -O3 -std=c++20 -Wall -fPIC -c $(pkg-config --cflags opencv4) *$1.cpp -Wno-unused-function -Wno-address -Wno-deprecated-declarations
g++ -O3 -shared -Wl,-soname,$1.so.1 -o $1.dll *.o $(pkg-config --libs opencv4)
