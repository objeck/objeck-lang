#/bin/sh
rm -rf *.o
rm -rf *.so
g++ -O3 -Wall -fPIC -c *$1.cpp -lodbc
g++ -O3 -shared -D_DEBUG -Wl,-soname,$1.so.1 -o $1.so *.o -lodbc
# g++ -shared -fPIC $1.o $1.cpp 
# g++ -fPIC -O3 -c -Wno-deprecated -Wall -D_X64 $1.cpp -o $1.so
# g++ -shared -fPIC -O3 -c -Wno-deprecated -Wall $1.cpp -o $1.so
# g++ -g -shared -fPIC `pkg-config -Wl,-soname,$1.so.1 -D_DEBUG -D_X64 -o $1.so.1.0.1 $1.cpp -lc
