#/bin/sh

rm -rf *.o
rm -rf *.so

# OpenCV's own headers raise 35 -Wdeprecated-enum-enum-conversion warnings that
# no change here can fix. -isystem marks them as system headers so their
# warnings are suppressed while ours are not -- which is the point, as
# -Wno-deprecated-enum-enum-conversion would have hidden the same mistake in
# opencv.cpp too. --libs is dropped: this is a compile-only line.
OPENCV_CFLAGS=`pkg-config --cflags opencv4 | sed 's/-I/-isystem /g'`
g++ -O3 -std=c++20 -Wall -fPIC -c $OPENCV_CFLAGS *$1.cpp -Wno-unused-function -Wno-deprecated-declarations
g++ -O3 -shared -Wl,-soname,$1.so.1 -o $1.so *.o `pkg-config --libs opencv4` -lmbedtls -lmbedx509 -lmbedcrypto
