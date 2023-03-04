# GTK Binding Library

## Approach

* Leverage XML [generated](https://github.com/gtk-rs/gir-files) from GObject Introspection. Either leverage existing XML files (i.e. Rust) or generate our own. Here's a good reference [article](https://viruta.org/the-magic-of-gobject-introspection.html). 
	- Create binding for GTK3 and GTK4
	- Start with Gdk and Gtk files

* From the XML files, produce C++ and Object bindings (similar to SDL2)
	- Include API documentation in Objeck generate code
	- Ensure C++ code is portable between POSIX and Windows environments

* Create tests programs
	- Start with ["Hello World!"](https://docs.gtk.org/gtk3/getting_started.html)
	- Find an online tutorial and implement the equivalent in Objeck

* Empower the community to report issues and bugs along the way

## Current Plan

* Map GObject basic types (gint, gdouble, utf8, etc.)
* Code generation for Application and Window. GObject code might need to be written by hand.
* Basic [PoC](poc) done. Tested on Linux and Windows, macOS is next.
* Updating the code generator per PoC effort.

## Targets
Solution and PoCs will need to run on macOS, Linux and Windows (VS and MSYS2).

### Linux
```
apt-get install libgtk-3-dev
```
[PoC build script](poc/build_linux.sh)

### Windows
Repository contains required headers, libraries and DLLs.

However, to build GTK from source on Windows
```
cd \
git clone https://github.com/Microsoft/vcpkg
cd vcpkg
.\bootstrap-vcpkg.bat
vcpkg install gtk3:x64-windows gtk4:x64-windows
vcpkg integrate install
```
[PoC build script](poc/build_win.cmd)

### macOS
```
TBD
```

## Code Generation

Sample output [C++](bindings/gtk3/gtk3_bindings.cpp) and [Objeck](bindings/gtk3//gtk3_bindings.obs)
