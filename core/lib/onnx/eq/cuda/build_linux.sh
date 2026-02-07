#/bin/sh

rm -rf *.o
rm -rf *.so

g++ -O3 -std=c++17 -Wall -fPIC -I./lib/include -L./lib/lib -c `pkg-config --cflags --libs opencv4` -Wl,-rpath=$(pwd)/onnxruntime-linux-x64-1.19.0/lib -c *$1.cpp -Wno-unused-function -Wno-deprecated-declarations
g++ -O3 -shared -Wl,-soname,$1.so.1 -o $1.so *.o -lmbedtls -lmbedx509 -lmbedcrypto
