#/bin/sh
rm -rf *.o
# g++ -shared -fPIC `pkg-config --cflags --libs gtk+-3.0` -c $1.o $1.cpp 
g++ -g -shared -fPIC -Wl,-soname,$1.so.1 -D_DEBUG -D_X64 -o $1.so $1.cpp -lc
# g++ -g -shared -fPIC `pkg-config --cflags --libs gtk+-3.0` -Wl,-soname,$1.so.1 -D_DEBUG -D_X64 -o $1.so.1.0.1 $1.cpp -lc
