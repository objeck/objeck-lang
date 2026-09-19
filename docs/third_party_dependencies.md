# Third-party dependencies: where each platform gets them, and how to check versions

Objeck pins little itself. The HTTP/2 and HTTP/3 stack (nghttp2, ngtcp2, nghttp3,
AWS-LC) is built from pinned sources by `tools/deps/build_quic_deps.sh` and linked
statically on Linux and macOS. The TLS, audio, vision and inference libraries come
from each platform's package channel at build time, so
security fixes arrive with that channel — and a channel that is behind is behind
for every release built on it. This page records the provenance so an audit is a
lookup, not an archaeology dig (the only version string in the tree, "mbedTLS
3.6.3" in `deploy_windows.cmd`, is a comment about a hand-built ARM64 copy from
before vcpkg carried it).

| Library | Windows x64 / ARM64 | Linux x64 / ARM64 | macOS ARM64 | Used by |
|---|---|---|---|---|
| mbedTLS | vcpkg `mbedtls:<arch>-windows` (`ci-build.yml`, "Install Windows dependencies") | apt `libmbedtls-dev` | 3.6.4, static, `tools/deps/build_macos_deps.sh` (in-tree `mbedtls_config.h`) | TLS sockets, HTTPS, `libobjk_crypto` hashes/ciphers |
| nghttp2 | vcpkg `nghttp2` | v1.70.0, static, `tools/deps/build_quic_deps.sh` | v1.70.0, static, same script | HTTP/2 (`net_h2`) |
| ngtcp2 / nghttp3 / AWS-LC | not used: HTTP/3 goes through WinHTTP (Windows 11 and later) | ngtcp2 v1.25.0, nghttp3 v1.18.0, AWS-LC v5.8.0, static, `tools/deps/build_quic_deps.sh` | same versions, static, same script | HTTP/3 (`net_quic`) |
| SDL2, SDL2_image, SDL2_mixer, SDL2_ttf | DLLs vendored in `core/lib/sdl/lib/<arch>` and copied to `bin\` by `deploy_windows.cmd`; their version resources say SDL2 2.31.0, SDL2_image 2.9.0, SDL2_mixer 2.8.0, SDL2_ttf 2.25.0 | apt `libsdl2-*-dev` | brew `sdl2*` | `sdl2`, `sdl_game`, `sdl_gl` |
| OpenCV | x64: `opencv_world4120.dll` from the OpenCV 4.12.0 Windows release, pinned as `$opencvVersion` in the workflows that download it (`ci-build.yml`, `release-build.yml`, `release-publish.yml`); ARM64: the modular `opencv_*4.dll` from vcpkg `opencv4:arm64-windows` (4.12.0 committed in `core/lib/opencv/win/arm64`, reinstalled by `release-build.yml`), with its codec DLLs from vcpkg | apt `libopencv-dev` | 4.12.0, static, `tools/deps/build_macos_deps.sh` | `libobjk_opencv`, `libobjk_onnx` preprocessing |
| ONNX Runtime | x64: NuGet `Microsoft.ML.OnnxRuntime.DirectML` 1.22.1 (`core/lib/onnx/vs/packages.config`); ARM64: a QNN build vendored in `core/lib/onnx/eq/qnn/win/onnx/arm64/bin` | x64: 1.19.0, a CPU-only build vendored in `core/lib/onnx/eq/cuda/lib/x64/lib`; ARM64: none, so no ONNX | 1.30.0 Microsoft prebuilt, shipped as `lib/native/libonnxruntime.1.dylib` (`tools/deps/build_macos_deps.sh`) | `libobjk_onnx` |
| LAME | `libmp3lame.dll` shipped in `bin\` | apt `libmp3lame-dev` | 3.100, shared (LGPL), shipped as `lib/native/libmp3lame.0.dylib` (`tools/deps/build_macos_deps.sh`) | `libobjk_lame` |
| ODBC | Windows SDK | apt `unixodbc-dev` | libiodbc 3.52.12, static, `tools/deps/build_macos_deps.sh` | `libobjk_odbc` |
| zlib | vendored (`core/lib/zlib`, see `windows_vcpkg_msbuild` for why the vcpkg one must not shadow it) | vendored | vendored | compression traps |
| VC runtime | `vcruntime140*.dll`, `msvcp140*.dll`, `concrt140.dll` copied from the toolset (`deploy_windows.cmd`) | — | — | every Windows binary |

## Checking what a build actually used

- **Windows**: in the CI log's "Install Windows dependencies" step, `vcpkg install` names each port with its version (`mbedtls:x64-windows@<version>`). OpenCV x64's version is `$opencvVersion` in the workflows (and the `4120` in its DLL name); ONNX Runtime x64's is the NuGet version in `core/lib/onnx/vs/packages.config`. The vendored DLLs, SDL2 and the ARM64 QNN ONNX Runtime (1.23.0), carry theirs in their file-version resources (`(Get-Item SDL2.dll).VersionInfo`).
- **Linux**: `dpkg -l libmbedtls-dev libsdl2-dev libopencv-dev libmp3lame-dev` on the runner image (`ubuntu-latest` — the image, not the tree, decides the version). The HTTP/2 and HTTP/3 versions are pinned in `tools/deps/build_quic_deps.sh` and recorded in its prefix's `.quic-deps-stamp`.
- **macOS**: every third-party library is pinned in `tools/deps/build_macos_deps.sh` (version and sha256) and recorded in its prefix's `macos-bundle/.macos-deps-stamp`; HTTP/2 and HTTP/3 as on Linux (`.quic-deps-stamp`). The licenses ship under each library's `licenses/`.
- **Shipped tree**: `ldd lib/native/libobjk_crypto.so` / `otool -L` / `dumpbin /dependents` names the exact runtime libraries a native module links, which is also what `verify_native_libs.sh|.ps1` checks resolve.

## What "audit" means here

1. For the two libraries with a security surface exposed to the network — mbedTLS and nghttp2 (plus ngtcp2/AWS-LC on POSIX) — compare the version the runner image installs, or the version pinned in the table above, against upstream's supported branches (mbedTLS 3.6 LTS at the time of writing; 4.x is not API-compatible and is a migration, not a bump).
2. For the Windows pins, OpenCV x64 (`$opencvVersion` and the cache keys in the three workflows, and the `4120` in the DLL and import-library names) and ONNX Runtime x64 (the NuGet version in `core/lib/onnx/vs/packages.config`, repeated in `vs/vs.vcxproj` and `deploy_windows.cmd`), compare the pinned version against the latest patch release and bump it everywhere it is named. The vendored SDL2 DLLs and ARM64 QNN runtime are bumped by replacing the files; ARM64 OpenCV follows vcpkg.
3. Anything vendored (zlib) is our responsibility to update by hand.

A scheduled job that prints these versions into the CI summary would turn step 1 into a glance; it is not built yet.
