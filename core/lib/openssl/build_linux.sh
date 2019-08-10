#/bin/sh
rm -rf *.o
rm -rf *.so
# g++ -g -Wall -fPIC -c *$1.cpp -lssl -lcrypto; g++ -g -shared -D_DEBUG -Wl,-soname,$1.so.1 -o $1.so *.o -lssl -lcrypto
g++ -O3 -Wno-unused-function -std=c++11 -Wall -fPIC -c *$1.cpp
g++ -O3 -shared -Wl,-soname,$1.so.1 -o $1.so *.o -lssl -lcrypto
