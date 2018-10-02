#/bin/sh
rm -rf *.o *.dylib
g++ -D_X64 -D_OSX -Wno-unused-function -Wno-deprecated-declarations -fPIC -c -O3 -Wall -F/Library/Frameworks -I/Library/Frameworks/SDL2.framework/Headers -I/Library/Frameworks/SDL2_ttf.framework/Headers -I/Library/Frameworks/SDL2_mixer.framework/Headers -I/Library/Frameworks/SDL2_image.framework/Headers -I/Library/Frameworks/SDL2_mixer.framework/Headers -I/usr/local/ssl/include $1.cpp
g++ -D_X64 -D_OSX -dynamiclib -L/usr/local/ssl/lib -lssl -Wl,-headerpad_max_install_names,-undefined,dynamic_lookup,-compatibility_version,1.0,-current_version,1.0 -o $1.dylib $1.o -v
