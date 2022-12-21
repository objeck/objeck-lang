#/bin/sh

rm -rf *.o
rm -rf *.so

g++ -O3 -Wall -std=c++11 -fPIC -c *$1.cpp SDL2_framerate.c SDL2_gfxPrimitives.c SDL2_imageFilter.c SDL2_rotozoom.c `pkg-config --cflags --libs sdl2` -Wno-unused-function -Wno-unused-but-set-variable
g++ -O3 -shared -Wl,-soname,$1.so.1 -o $1.so *.o -lSDL2_ttf -lSDL2_mixer -lSDL2_image -lSDL2 -lSDL2main
