
                         Simple DirectMedia Layer

                                  (SDL)

                                Version 2.0

---
http://www.libsdl.org/

Simple DirectMedia Layer is a cross-platform development library designed
to provide low level access to audio, keyboard, mouse, joystick, and graphics
hardware via OpenGL and Direct3D. It is used by video playback software,
emulators, and popular games including Valve's award winning catalog
and many Humble Bundle games.

SDL officially supports Windows, Mac OS X, Linux, iOS, and Android.
Support for other platforms may be found in the source code.

SDL is written in C, works natively with C++, and there are bindings 
available for several other languages, including C# and Python.

This library is distributed under the zlib license, which can be found
in the file "COPYING.txt".

The best way to learn how to use SDL is to check out the header files in
the "include" subdirectory and the programs in the "test" subdirectory.
The header files and test programs are well commented and always up to date.
More documentation and FAQs are available online at:
	http://wiki.libsdl.org/

If you need help with the library, or just want to discuss SDL related
issues, you can join the developers mailing list:
	http://www.libsdl.org/mailing-list.php

If you want to report bugs or contribute patches, please submit them to
bugzilla:
    http://bugzilla.libsdl.org/

Enjoy!
	Sam Lantinga				(slouken@libsdl.org)

---


SDL_image 2.0

The latest version of this library is available from:
http://www.libsdl.org/projects/SDL_image/

This is a simple library to load images of various formats as SDL surfaces.
This library supports BMP, PNM (PPM/PGM/PBM), XPM, LBM, PCX, GIF, JPEG, PNG,
TGA, TIFF, and simple SVG formats.

API:
#include "SDL_image.h"

	SDL_Surface *IMG_Load(const char *file);
or
	SDL_Surface *IMG_Load_RW(SDL_RWops *src, int freesrc);
or
	SDL_Surface *IMG_LoadTyped_RW(SDL_RWops *src, int freesrc, char *type);

where type is a string specifying the format (i.e. "PNG" or "pcx").
Note that IMG_Load_RW cannot load TGA images.

To create a surface from an XPM image included in C source, use:

	SDL_Surface *IMG_ReadXPMFromArray(char **xpm);

An example program 'showimage' is included, with source in showimage.c

JPEG support requires the JPEG library: http://www.ijg.org/
PNG support requires the PNG library: http://www.libpng.org/pub/png/libpng.html
    and the Zlib library: http://www.gzip.org/zlib/
TIFF support requires the TIFF library: ftp://ftp.sgi.com/graphics/tiff/

If you have these libraries installed in non-standard places, you can
try adding those paths to the configure script, e.g.
sh ./configure CPPFLAGS="-I/somewhere/include" LDFLAGS="-L/somewhere/lib"
If this works, you may need to add /somewhere/lib to your LD_LIBRARY_PATH
so shared library loading works correctly.

This library is under the zlib License, see the file "COPYING.txt" for details.
