#/bin/sh
rm -rf *.o
# g++ -shared -fPIC `pkg-config --cflags --libs gtk+-2.0` -c $1.o $1.cpp 
# g++ -fPIC -g -c -Wno-deprecated -Wall -D_X64 $1.cpp
g++ -shared -fPIC `pkg-config --cflags --libs gtk+-2.0` -Wl,-soname,$1.so.1 -D_X64 -o $1.so.1.0.1 $1.cpp -lc
