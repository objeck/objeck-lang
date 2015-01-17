#/bin/sh
rm -rf *.o
rm -rf *.so
g++ -g -Wall -D_MINGW -D_DEBUG -c $1.cpp `pkg-config --cflags gtk+-2.0 pkg-config --libs gtk+-2.0` -I"../openssl/win32/include"
g++ -g -shared -D_MINGW -D_DEBUG -m32 -D_MINGW -L"../openssl/win32/lib_mingw" -lssl -lcrypto -lgdi32 -lws2_32 -static -static-libstdc++ -Wl,-soname,$1.so.1 -o $1.so $1.o `pkg-config --cflags gtk+-2.0 pkg-config --libs gtk+-2.0` 
