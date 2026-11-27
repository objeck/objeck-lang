# Portable Linux releases

Status: **built** for v2026.11.4 (`tools/packaging/build_portable_linux.sh`, `tools/deps/build_linux_portable_deps.sh`, `tools/packaging/portable-linux/`, the Linux legs of `.github/workflows/release-build.yml`, `.github/workflows/portable-linux.yml`). **How to check:** `tools/cicd/check_docs_deps.py --portable <tree> --platform linux-x64` holds a built tree to the rules below, and `tools/packaging/test_portable_linux.sh <tarball>` runs the tarball in a clean container of every distribution listed in `core/release/runtime_deps.json` (`linux.release.tested_images`).

## The defect

The v2026.11.3 `objeck-linux-x64` tarball does not start on Ubuntu 26.04:

```
$ ./bin/obr --version
./bin/obr: error while loading shared libraries: libmbedtls.so.14: cannot open shared object file: No such file or directory
```

`obr`, `obd` and `obi` had `NEEDED` entries for `libmbedtls.so.14`, `libmbedx509.so.1` and `libmbedcrypto.so.7`, the sonames of mbedTLS 2.28. Ubuntu 26.04, Debian 13, Fedora and Arch ship mbedTLS 3 under other sonames, and no package provides the old ones. Every binary also asked for `GLIBC_2.38` and `GLIBCXX_3.4.32`, so on Debian 12 and Ubuntu 22.04, where the mbedTLS package does exist, the loader refused them for the C library instead. `obd` needed `libreadline.so.8`, and the OpenCV binding needed `libopencv_*.so.406`, which is OpenCV 4.6 exactly.

The cause is how the release was built, not what it does: `release-build.yml` compiled on the `ubuntu-24.04` runner against that runner's libraries. The result ran on Ubuntu 24.04 and its derivatives and nowhere else. The dependency gate (`check_docs_deps.py --artifacts`) could not see this, because it compared the tree with a manifest that described Ubuntu 24.04 too.

## What the release promises now

The x64 and arm64 tarballs, and the `.deb` made from each, run on any distribution with **glibc 2.28 or newer** and its C++ runtime: Ubuntu 22.04, 24.04 and 26.04, Debian 12 and 13, Fedora, Arch, and RHEL 8 and its rebuilds (the floor itself). Nothing has to be installed first. `obc`, `obr`, `obd`, `obi`, `obu` and `obb` start on a base container image of each, and compile and run programs, make and serve TLS connections, and load the self-contained native libraries.

musl distributions (Alpine) are not supported; `install_deps.sh` says so.

## How

### One old build environment

The Linux legs build inside a container, `almalinux:8.10` with `gcc-toolset-14` (`tools/packaging/portable-linux/Dockerfile`). A binary can only ask the system for symbol versions that existed where it was linked. AlmaLinux 8 has glibc 2.28 and libstdc++ 8, which is older than every supported distribution, so nothing newer can be asked for. This is the approach of Python's `manylinux_2_28` wheels, and RHEL 8 is maintained until 2029.

The runner (`ubuntu-24.04`, `ubuntu-24.04-arm`) only hosts docker. `build_portable_linux.sh` builds the image, starts it with the checkout mounted, builds the static libraries, and runs the same `core/release/deploy_posix.sh` every other Linux build runs. The same command produces the same tarball on a developer's machine. The macOS and Windows legs are unchanged.

### What is linked into the binaries

| Library | Version | Was | Why static |
|---|---|---|---|
| mbedTLS | 3.6.4 | system 2.28, dynamic | its soname changes between major versions; it is why `obr` did not start |
| AWS-LC, ngtcp2, nghttp3, nghttp2 | as before | static since v2026.9.3 | unchanged (`tools/deps/build_quic_deps.sh`) |
| OpenCV | 4.12.0 | system 4.6, dynamic | its soname carries the minor version, so no two distributions agree |

mbedTLS is the release and the configuration file (`core/lib/openssl/macos/mbedtls_config.h`) the macOS package has shipped since v2026.9.3, so the two platforms now run the same TLS library built the same way. The configuration matters: mbedTLS's default has no threading support, and `obr` handshakes on many threads. OpenCV is likewise the macOS version and module list, with its codecs built from its own tree.

`tools/deps/build_linux_portable_deps.sh` builds them once into a prefix that is cached between runs. It installs archives only. `build_portable_linux.sh` names that prefix in `OBJECK_LINK_DIRS`, which the image's compiler wrapper turns into the first `-L` of every link, and in `CPATH`, so the makefiles' ordinary `-lmbedtls` resolves to the archive and no makefile needs a second set of link lines. (`LIBRARY_PATH` does not do this: GCC searches it after its own directories.) Whether that worked is not assumed: the gate reads the result.

The license texts of everything linked in are copied to `doc/licenses`, and `deploy_posix.sh` refuses to package a tree whose binaries carry mbedTLS or OpenCV without them.

### zlib stays the system's

`libz.so.1` is the one library besides the C and C++ runtime that the tools link. It was going to be static, and was measured first: `obc` compiling `sql_tables_18.obs` with eight libraries took 0.56 s of CPU linked to the system's zlib and 0.70 s with zlib 1.3.1 or 1.2.11 linked in, whichever compiler flags built it (15 runs each, alternating, same machine). A fifth of the compiler's time is not worth a library that cannot be missing: `dpkg`, `rpm` and `pacman` link `libz.so.1` themselves, so every system with a package manager has it, and all eight tested base images do. Its soname has not changed since 1.0, and the tools use nothing newer than `ZLIB_1.2.11`, which the gate holds them to.

(The same measurement with zlib-ng in compatibility mode took 0.18 s. That is a change to what `obc` writes, byte for byte, and is left for a release that is not also changing how the build is made.)

### The C++ runtime stays shared

`libstdc++.so.6` and `libgcc_s.so.1` are still `NEEDED`, and are on every glibc system with a package manager (apt and dnf are C++ programs). `gcc-toolset` links them the way Red Hat designed it: symbols that libstdc++ 8 exports come from the system's library, and everything newer that GCC 14 needs is linked into the binary from `libstdc++_nonshared.a`. The result asks for at most `GLIBCXX_3.4.25`.

`-static-libstdc++`, which the first experiment used, was rejected. `obr` loads the native libraries with `dlopen`, and they are C++ too. With one static copy of the runtime in `obr` and another in each library, an exception or a `std::` object that crossed the boundary would meet two different runtimes. One shared runtime per process is what the system loader gives every other C++ program.

### readline is looked up, not linked

Only `obd` used readline, for line editing and history at its prompt. Its soname is `.so.8` on current distributions and `.so.7` on RHEL 8, and static linking would put GPL code into a BSD-licensed binary. `obd` now looks for `libreadline.so.8`, `.so.7` or `.so.6` when it shows its first prompt (`LineEditor` in `core/debugger/debugger.h`), and without one reads commands with `std::getline`, as it always has on Windows. This applies to every Linux build, not only the release, so CI's debugger tests exercise the code that ships. `obi` named `-lreadline` on its link line without using it; that is removed.

### Hardening is kept

Ubuntu's GCC hardens everything it compiles by default; `gcc-toolset` does not. The releases through v2026.11.3 were position-independent executables with full RELRO, stack protector, `_FORTIFY_SOURCE`, stack-clash protection and control-flow protection only because of where they were compiled. `tools/packaging/portable-linux/cc-wrapper.sh` stands in for `gcc` and `g++` in the image and applies the same defaults by the same rules, so moving the build does not quietly remove them. The gate checks PIE, RELRO with `BIND_NOW`, and a non-executable stack on every tool. `_FORTIFY_SOURCE` is 2 rather than Ubuntu 24.04's 3, because level 3 needs glibc 2.34's headers.

## Native libraries

| Library | Needs from the system | On a base image |
|---|---|---|
| `libobjk_crypto` | nothing (mbedTLS linked in) | loads |
| `libobjk_diags` | nothing beyond `libz.so.1` | loads |
| `libobjk_ml` | nothing (Eigen is header-only) | loads |
| `libobjk_onnx` | nothing (ONNX Runtime ships beside it, OpenCV linked in) | loads |
| `libobjk_odbc` | unixODBC, `libodbc.so.2` | refused until installed |
| `libobjk_lame` | LAME, `libmp3lame.so.0` | refused until installed |
| `libobjk_sdl` | SDL2, SDL2_image, SDL2_mixer, SDL2_ttf, `libGL.so.1` | refused until installed |
| `libobjk_opencv` | GTK 3, GStreamer 1.0 and their GLib (windows, video files) | refused until installed |

The last four stay the system's on purpose. They reach into the display server, the audio stack, GPU drivers and the ODBC driver manager's configuration, and a bundled copy would compete with the host's instead of using it. What makes that safe is that their sonames have not changed in a decade: `libSDL2-2.0.so.0`, `libgtk-3.so.0`, `libgstreamer-1.0.so.0`, `libodbc.so.2` and `libmp3lame.so.0` are the same file name on every distribution tested. FFmpeg is the counter-example and the reason OpenCV is built without it: `libavcodec`'s soname changes with every FFmpeg release. Video files go through GStreamer instead, and cameras through V4L2, which is a kernel interface.

`libobjk_sdl` is compiled against the headers of the oldest SDL2, SDL2_image, SDL2_mixer and SDL2_ttf among the supported distributions (Ubuntu 22.04's: 2.0.20, 2.0.5, 2.0.4 and 2.0.18), not the build image's. A newer header can turn a macro into a function: `Mix_LoadWAV` became one in SDL2_mixer 2.6, and a binding compiled against 2.8 did not load on Ubuntu 22.04. With the oldest headers it calls nothing a later release added. On RHEL 8 itself SDL2 is 2.0.10, older than the binding needs (`SDL_GetTicks64` is 2.0.18), so `libobjk_sdl` does not load there; everything else does.

A missing system library never stops `obr`. Native libraries are loaded when a program first uses one, and a failure is reported and ends that program with exit code 1:

```
>>> Runtime error loading shared library: libodbc.so.2: cannot open shared object file: No such file or directory <<<
```

`install_deps.sh`, which ships in the tarball, asks `ldd` what is unresolved and the package manager which package provides it; the `.deb` lists the same packages as `Recommends`.

## The rules, and where they are enforced

`core/release/runtime_deps.json` (`linux.release`) states them and `check_docs_deps.py --portable` reads a built tree against them. `build_portable_linux.sh` runs it after every build, so the release cannot be built without passing it.

1. **Symbol versions.** No ELF file in the tree may need more than `GLIBC_2.28`, `GLIBCXX_3.4.25`, `CXXABI_1.3.11`, `GCC_7.0.0` or `ZLIB_1.2.11`. This covers the vendored ONNX Runtime as well as what the build compiles.
2. **The tools link the C and C++ runtime and `libz.so.1` only.** Any other `NEEDED` entry in `bin/*` fails, whatever it is.
3. **The self-contained native libraries** (crypto, diags, ml, onnx) may add only files that ship in the tree.
4. **The other native libraries link exactly the listed system sonames.** A new one fails until it is listed, and so documented; a listed one nothing links fails too.
5. **Hardening**, as above.

`build_portable_linux.sh` also runs `ldd -r` on every native library inside the build image, where all the system libraries are installed, and fails on any undefined symbol. A shared library links with symbols left undefined, and the loader looks for a function only when it is first called, so a binding that names a function without naming its library passes every check above and fails in a user's program. This caught OpenCV's generated `opencv4.pc` omitting GTK and GStreamer.

A symbol-version check reads what a binary declares. It cannot see a library opened by name at run time, a kernel interface, or a file that is expected to exist. `test_portable_linux.sh` covers that side by running the tarball: in a container of each tested image, with no network and nothing installed, it starts every tool, compiles and runs a program, makes an HTTPS request to a TLS server it starts on the loopback (accepted with the certificate trusted, refused without), compiles and runs the first 20 shipped examples, checks that the self-contained native libraries resolve, checks that an ODBC program without unixODBC is refused with the message above, and drives `obd` through a run with no readline.

`test_portable_linux.sh --natives` is the same matrix with the network on: the tarball's own `install_deps.sh --yes` must find a package for every system library the optional bindings use, after which every native library must resolve to the last symbol (`ldd -r`) and the OpenCV, LAME and ONNX regression programs must pass. It runs on pull requests and not in the release build, which should not wait on eight distributions' package mirrors.

The gate and the first matrix run in three places: `release-build.yml` on every tag and dry run, `portable-linux.yml` on every pull request that touches the build image, the static libraries, a Linux makefile, the deploy script or the manifest, and by hand.

`ci-build.yml` still builds against the runner's own libraries and checks that tree with `--artifacts` against `linux.sonames`. That is the developer's build, and it is a different link from the release by design; the manifest now says which is which.

## The .deb

`tools/packaging/build_deb.sh` packages the released tarball unchanged, so the package is portable for the same reason the tarball is. `Depends` is computed from `bin/*` and comes to the C and C++ runtime and zlib (`libc6 (>= 2.34)`: `dpkg-shlibdeps` on Ubuntu 24.04 names the release that merged libpthread and libdl into libc, which every supported Debian and Ubuntu has). `Recommends` is computed from the optional native libraries on Ubuntu 24.04, and each name that differs elsewhere gets an alternative: the `t64` suffix of the 64-bit `time_t` transition (`libgtk-3-0t64 | libgtk-3-0`) and `libodbc2 | libodbc1` for Ubuntu 22.04. `test_deb.sh` installs it with apt on Ubuntu 22.04, 24.04 and 26.04 and Debian 12 and 13.

The file is still named `objeck-ubuntu24.04-<arch>_<version>.deb`. `obu`, the install pages and the release scripts ask for that name, and renaming an asset is a change for a release that is not also changing how it is built.

## ARM64

The arm64 leg is the same script on the `ubuntu-24.04-arm` runner; `almalinux:8.10` is a multi-architecture image, and the image, the static libraries and the tree are built natively. The CPU baseline is unchanged (`-march=armv8-a -moutline-atomics`, issue #893). The wrapper uses `-mbranch-protection=standard` where x64 uses `-fcf-protection`, as Ubuntu does.

## Cost

Building OpenCV is most of the time (five minutes on a 32-thread machine, several times that on a hosted runner), once, after which the prefix is restored from the Actions cache. The cache key covers the two dependency scripts, the Dockerfile, the compiler wrapper and the mbedTLS configuration, and the dependency script also stamps the prefix with its own versions, hash, compiler and glibc and rebuilds on any mismatch, so a stale cache cannot be linked.

Measured against `obc` and `obr` built from the same source on Ubuntu 24.04 the way v2026.11.3 was (interleaved runs, one machine, minimum of 7): `obr` start-up 6.8 ms against 7.0 ms; nbody 15.82 s against 15.81 s; binarytrees 3.12 s against 3.17 s; spectralnorm 2.89 s against 2.90 s; fannkuchredux 4.06 s against 4.08 s. No difference outside the noise.

## Not done

- **A `.rpm`**, and a package name that does not say Ubuntu. The tarball runs on Fedora and RHEL as it is.
- **musl.** A separate build, with its own floor.
- **A newer ONNX Runtime on Linux.** The vendored 1.19.0 needs `GLIBC_2.27`, so it is within the floor; replacing it is unrelated to this change.
- **A static or bundled SDL2, GTK or GStreamer.** See "Native libraries".
- **zlib-ng.** See "zlib stays the system's".
