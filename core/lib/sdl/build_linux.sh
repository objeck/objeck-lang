#/bin/sh
rm -rf *.o
rm -rf *.so
g++ -O3 -Wno-unused-function -Wall -fPIC -c *$1.cpp `pkg-config --cflags --libs sdl2`
g++ -O3 -shared -Wl,-soname,$1.so.1 -o $1.so *.o -lSDL2 -lSDL2main
