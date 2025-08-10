#/bin/sh

rm -rf *.o
rm -rf *.so

g++ -O3 -std=c++17 -Wall -fPIC -c `pkg-config --cflags --libs opencv4` *$1.cpp -Wno-unused-function -Wno-deprecated-declarations
g++ -O3 -shared -Wl,-soname,$1.so.1 -o $1.so *.o -lssl -lcrypto
