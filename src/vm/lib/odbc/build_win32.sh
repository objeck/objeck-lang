#/bin/sh
rm -rf *.o
rm -rf *.so
g++ -O3 -Wall -D_MINGW -I"../openssl/win32/include" -static -static-libstdc++ -c *$1.cpp 
g++ -O3 -shared -D_MINGW -static -static-libstdc++ -Wl,-soname,$1.so.1 -o $1.so *.o -L"../openssl/win32/lib_mingw" -lodbc32 -lssl -lcrypto -lgdi32 -lws2_32
# g++ -shared -fPIC $1.o $1.cpp 
# g++ -fPIC -O3 -c -Wno-deprecated -Wall -D_X64 $1.cpp -o $1.so
# g++ -shared -fPIC -O3 -c -Wno-deprecated -Wall $1.cpp -o $1.so
# g++ -g -shared -fPIC `pkg-config -Wl,-soname,$1.so.1 -D_DEBUG -D_X64 -o $1.so.1.0.1 $1.cpp -lc
