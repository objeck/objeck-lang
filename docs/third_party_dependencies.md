# Third-party dependencies: where each platform gets them, and how to check versions

Objeck pins almost nothing itself. The TLS, HTTP/2, HTTP/3, audio, vision and
inference libraries come from each platform's package channel at build time, so
security fixes arrive with that channel — and a channel that is behind is behind
for every release built on it. This page records the provenance so an audit is a
lookup, not an archaeology dig (the only version string in the tree, "mbedTLS
3.6.3" in `deploy_windows.cmd`, is a comment about a hand-built ARM64 copy from
before vcpkg carried it).

| Library | Windows x64 / ARM64 | Linux x64 / ARM64 | macOS ARM64 | Used by |
|---|---|---|---|---|
| mbedTLS | vcpkg `mbedtls:<arch>-windows` (`ci-build.yml`, "Install vcpkg packages") | apt `libmbedtls-dev` | brew `mbedtls` | TLS sockets, HTTPS, `libobjk_crypto` hashes/ciphers |
| nghttp2 | vcpkg `nghttp2` | apt `libnghttp2-dev` | brew `nghttp2` | HTTP/2 (`net_h2`) |
| ngtcp2 / nghttp3 / GnuTLS | not built (HTTP/3 is POSIX-only; see `http3_windows_gap`) | apt `libngtcp2-dev`, `libnghttp3-dev`, GnuTLS | brew | HTTP/3 (`net_quic`) |
| SDL2, SDL2_image, SDL2_mixer, SDL2_ttf | DLLs shipped in `bin\` from the SDL release archives `deploy_windows.cmd` downloads | apt `libsdl2-*-dev` | brew `sdl2*` | `sdl2`, `sdl_game`, `sdl_gl` |
| OpenCV | `opencv_world4120` — 4.12.0, the one explicit pin (`deploy_windows.cmd`) | apt `libopencv-dev` | brew `opencv` | `libobjk_opencv`, `libobjk_onnx` preprocessing |
| ONNX Runtime | `onnxruntime.dll` from the Microsoft release archive named in `deploy_windows.cmd` | release archive | release archive | `libobjk_onnx` |
| LAME | `libmp3lame.dll` shipped in `bin\` | apt `libmp3lame-dev` | brew `lame` | `libobjk_lame` |
| ODBC | Windows SDK | apt `unixodbc-dev` | brew `unixodbc` | `libobjk_odbc` |
| zlib | vendored (`core/lib/zlib`, see `windows_vcpkg_msbuild` for why the vcpkg one must not shadow it) | vendored | vendored | compression traps |
| VC runtime | `vcruntime140*.dll`, `msvcp140*.dll`, `concrt140.dll` copied from the toolset (`deploy_windows.cmd`) | — | — | every Windows binary |

## Checking what a build actually used

- **Windows**: `vcpkg list` in the CI log's "Install vcpkg packages" step prints `mbedtls:x64-windows <version>`; the SDL/ONNX/OpenCV archives are named with their versions in `deploy_windows.cmd`.
- **Linux**: `dpkg -l libmbedtls-dev libnghttp2-dev libngtcp2-dev libsdl2-dev libopencv-dev` on the runner image (`ubuntu-latest` — the image, not the tree, decides the version).
- **macOS**: `brew list --versions mbedtls nghttp2 sdl2 opencv` in the build log.
- **Shipped tree**: `ldd lib/native/libobjk_crypto.so` / `otool -L` / `dumpbin /dependents` names the exact runtime libraries a native module links, which is also what `verify_native_libs.sh|.ps1` checks resolve.

## What "audit" means here

1. For the two libraries with a security surface exposed to the network — mbedTLS and nghttp2 (ngtcp2/GnuTLS on POSIX) — compare the version the runner image installs against upstream's supported branches (mbedTLS 3.6 LTS at the time of writing; 4.x is not API-compatible and is a migration, not a bump).
2. For the pinned archives (OpenCV, ONNX Runtime, SDL2 on Windows) compare the pinned version against the latest patch release and bump the archive name in `deploy_windows.cmd`.
3. Anything vendored (zlib) is our responsibility to update by hand.

A scheduled job that prints these versions into the CI summary would turn step 1 into a glance; it is not built yet.
