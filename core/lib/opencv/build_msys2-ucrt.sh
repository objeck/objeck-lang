#!/bin/sh
# set -e: a failed compile must not fall through to the link, which then
# reports `cannot find *.o` and names the glob instead of the real error.
set -e

# deploy_msys2-ucrt.sh has invoked this script since the MSYS2 deploy was
# written; it did not exist, so every MSYS2 archive shipped without
# libobjk_opencv.dll. Mirrors build_linux.sh's flags for the MSYS2 toolchain.
rm -rf *.o
rm -rf *.dll

# MSYS2 moved to OpenCV 5 (module `opencv5`, headers in include/opencv5), and
# this asked for `opencv4` -- so a correctly installed OpenCV read as missing and
# installing it again changed nothing. Ask pkg-config which one is here instead
# of hardcoding a version that only ages.
OPENCV_PC=""
for m in opencv5 opencv4 opencv; do
  if pkg-config --exists "$m" 2>/dev/null; then OPENCV_PC="$m"; break; fi
done
if [ -z "$OPENCV_PC" ]; then
  echo "ERROR: no OpenCV pkg-config module (tried opencv5, opencv4, opencv)" >&2
  exit 1
fi

g++ -O3 -std=c++20 -Wall -fPIC -c $(pkg-config --cflags "$OPENCV_PC") *$1.cpp -Wno-unused-function -Wno-address -Wno-deprecated-declarations
g++ -O3 -shared -Wl,-soname,$1.so.1 -o $1.dll *.o $(pkg-config --libs "$OPENCV_PC")
