#/bin/sh
rm -rf *.o
rm -rf *.dll

g++ -O3 -Wall -std=c++11 -fPIC -c *$1.cpp SDL2_framerate.c SDL2_gfxPrimitives.c SDL2_imageFilter.c SDL2_rotozoom.c -I/ucrt64/include/sdl2 -Wno-maybe-uninitialized -Wno-unused-but-set-variable -Wno-unused-function
g++ -O3 -shared -Wl,-soname,$1.dll.1 -o $1.dll *.o -lSDL2_ttf -lSDL2_mixer -lSDL2_image -lSDL2 -lSDL2main