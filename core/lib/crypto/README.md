# Objeck Crypto Library

Cryptographic support for the Objeck language, providing hash functions, AES-256 encryption, and Base64 encoding/decoding.

The underlying crypto provider is **Mbed TLS**.

## Getting mbedTLS

- **Windows (x64 and ARM64):** from vcpkg, together with nghttp2. Run this
  outside any directory that holds a `vcpkg.json`, or vcpkg switches to
  manifest mode and rejects the package arguments:
  ```cmd
  vcpkg install mbedtls:x64-windows nghttp2:x64-windows
  vcpkg install mbedtls:arm64-windows nghttp2:arm64-windows
  ```
  `core/build/vcpkg.props` finds the install for every project: it checks
  `%VCPKG_ROOT%`, `C:\vcpkg` and `%USERPROFILE%\vcpkg`, and uses the first one
  that has the package. `deploy_windows.cmd` checks the same places and stops
  with the install command if none has it.
- **Linux:** the distribution's package, e.g. `sudo apt-get install libmbedtls-dev`.
- **macOS:** a static build with the in-tree `mbedtls_config.h`:
  `bash tools/deps/build_macos_deps.sh`.
