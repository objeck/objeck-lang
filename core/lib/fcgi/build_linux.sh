#/bin/sh

rm -rf *.o
rm -rf *.so

g++ -O3 -std=c++11 -Wall -fPIC -c *$1.cpp -Wno-unused-function
g++ -O3 -shared -Wl,-soname,libobjk_fcgi.so.1 -o libobjk_fcgi.so *.o -lfcgi -luuid\