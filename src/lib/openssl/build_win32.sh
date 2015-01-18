#/bin/sh
rm -rf *.o
rm -rf *.so
# g++ -g -Wall -c *$1.cpp -lssl -lcrypto; g++ -g -shared -D_DEBUG -Wl,-soname,$1.so.1 -o $1.so *.o -lssl -lcrypto
g++ -O3 -Wall -D_MINGW -I"../openssl/win32/include" -static -static-libstdc++ -c *$1.cpp  
g++ -O3 -shared -D_MINGW -static -static-libstdc++ -Wl,-soname,$1.so.1 -o $1.so *.o -L"../openssl/win32/lib_mingw" -lssl -lcrypto -lgdi32 -lws2_32 
