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

* Map GObject basic types
* Code generation for Gdk records
        - Prototype
        - Script
        - Optimize
* [PoC](poc) mostly done. Need to test on Linux, macOS and MSYS2 targets (will need a [beer](https://belgianfamilybrewers.be/belgian-beers/delirium-nocturnum/))
* Coding binding PoC done, validated however generated code will need to implemented per PoC

## Targets
Solution and PoCs will need to run on macOS, Linux and Windows (VS and MSYS2).

### Linux
```
apt-get install libgtk-3-dev
```
[PoC build script](poc/build_linux.sh)

### Windows
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

[Sample output](code_gen/debug/debug.txt)
