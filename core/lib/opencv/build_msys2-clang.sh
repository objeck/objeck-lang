#!/bin/sh
# deploy_msys2-clang.sh has invoked this script since the MSYS2 deploy was
# written; it did not exist, so every MSYS2 archive shipped without
# libobjk_opencv.dll. Mirrors build_linux.sh's flags for the MSYS2 toolchain.
set -e
rm -rf *.o
rm -rf *.dll

clang++ -O3 -std=c++20 -Wall -fPIC -c $(pkg-config --cflags opencv4) *$1.cpp -Wno-unused-function -Wno-deprecated-declarations -D_MSYS2_CLANG
clang++ -O3 -shared -Wl,-undefined,dynamic_lookup -o $1.dll *.o $(pkg-config --libs opencv4)
