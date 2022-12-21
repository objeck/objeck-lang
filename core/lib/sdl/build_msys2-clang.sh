#/bin/sh
rm -rf *.o
rm -rf *.dll

clang++ -O3 -Wall -std=c++11 -fPIC -c *$1.cpp SDL2_framerate.c SDL2_gfxPrimitives.c SDL2_imageFilter.c SDL2_rotozoom.c -I/clang64/include/sdl2 -Wno-uninitialized -Wno-unused-but-set-variable -Wno-unused-function
clang++ -O3 -shared -undefined dynamic_lookup -o $1.dll *.o -lSDL2_ttf -lSDL2_mixer -lSDL2_image -lSDL2 -lSDL2main