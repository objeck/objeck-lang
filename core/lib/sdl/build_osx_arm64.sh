#/bin/sh
rm -rf *.o *.dylib
clang -D_ARM64 -D_OSX -shared -Wno-unused-function -Wno-deprecated-declarations -g -fvisibility-inlines-hidden -fPIC -c -O3 -Wall -I../openssl/macos/include -I/opt/homebrew/include -I/opt/homebrew/include/SDL2 $1.cpp SDL2_framerate.c SDL2_gfxPrimitives.c SDL2_imageFilter.c SDL2_rotozoom.c 
clang -D_ARM64 -D_OSX -dynamiclib -L/opt/homebrew//lib -Wl,-headerpad_max_install_names,-undefined,dynamic_lookup,-compatibility_version,1.0,-current_version,1.0 `sdl2-config --cflags --libs` -o $1.dylib $1.o SDL2_framerate.o SDL2_gfxPrimitives.o SDL2_imageFilter.o SDL2_rotozoom.o -v
