#/bin/sh
rm -rf *.o
rm -rf *.so
g++ -O3 -Wno-unused-function -std=c++11 -Wall -fPIC -c *$1.cpp 
g++ -O3 -shared -Wl,-soname,libobjk_fcgi.so.1 -o libobjk_fcgi.so *.o -lfcgi -luuid
# g++ -g -D_DEBUG -Wno-unused-function -Wall -fPIC -c *$1.cpp 
# g++ -g -D_DEBUG -shared -luuid -Wl,-soname,libobjk_fcgi.so.1 -o libobjk_fcgi.so *.o -lfcgi
