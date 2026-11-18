# Objeck Crypto Library

Cryptographic support for the Objeck language, providing hash functions, AES-256 encryption, and Base64 encoding/decoding.

The underlying crypto provider is **Mbed TLS**. The Objeck API is `Cipher.Hash`, `Cipher.Encrypt` and `Cipher.Decrypt` in `core/compiler/lib_src/cipher.obs` (`-lib cipher`); the native entry points are in `crypto.cpp`.

## What it implements

- **Hashes** (`Cipher.Hash`): SHA-1, SHA-256, SHA-512, MD5 and RIPEMD-160 over a `Byte[]`, and `SHA256File`, which hashes a file a megabyte at a time without holding it in memory. MD5 and SHA-1 are kept for compatibility, not for new security uses.
- **AES-256** (`Cipher.Encrypt->AES256`, `Cipher.Decrypt->AES256`): CBC mode with PKCS#7 padding. The key argument is the raw password bytes, zero-padded or truncated to 32 bytes. The IV is derived from the password: OpenSSL's `EVP_BytesToKey` with SHA-512, a fixed 8-byte salt compiled into `crypto.cpp` (marked `TODO: add salt` there) and an iteration count of 8. It is kept this way so data encrypted by the earlier OpenSSL-based build still decrypts. Consequences: the same key and plaintext always give the same ciphertext, and there is no authentication tag, so a modified ciphertext is not detected. It is not an AEAD interface.
- **Base64** (`Cipher.Encrypt->Base64`, `Cipher.Decrypt->Base64`).

## How to check

Runnable examples are `programs/examples/sha_hash.obs` and `programs/examples/encrypt.obs`; compile with `obc -src programs/examples/encrypt.obs -lib cipher`. The regression suite uses `Cipher.Hash` in `ai_hub_client.obs` (SHA-256 and SHA-1 verification of downloads) and `http_stream_to_file.obs`; no regression test covers `AES256` directly.

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
