# Objeck Crypto Library

Cryptographic support for the Objeck language, providing hash functions, AES-256 encryption, and Base64 encoding/decoding.

The underlying crypto provider is **Mbed TLS**. The Objeck API is `Cipher.Hash`, `Cipher.Encrypt` and `Cipher.Decrypt` in `core/compiler/lib_src/cipher.obs` (`-lib cipher`); the native entry points are in `crypto.cpp`.

## What it implements

- **Hashes** (`Cipher.Hash`): SHA-1, SHA-256, SHA-512, MD5 and RIPEMD-160 over a `Byte[]`, and `SHA256File`, which hashes a file a megabyte at a time without holding it in memory. MD5 and SHA-1 are kept for compatibility, not for new security uses.
- **AES-256** (`Cipher.Encrypt->AES256`, `Cipher.Decrypt->AES256`): authenticated encryption with AES-256-GCM. The key argument is a password of any length (all of it is used, with no truncation); the 256-bit key is derived with PBKDF2-HMAC-SHA256 (210,000 iterations) from a random 16-byte salt, and a random 12-byte nonce is drawn per message from mbedTLS CTR_DRBG seeded by the OS entropy source, so encrypting the same plaintext twice gives different output. Output layout: `OBKC` magic (4 bytes), format version (1, currently 1), PBKDF2 iteration count (4, big-endian), salt (16), nonce (12), ciphertext, GCM tag (16); the header is authenticated as additional data. `Decrypt` returns `Nil` for a wrong password, a modified or truncated message, or an unknown version; it never returns unauthenticated plaintext. The iteration count is stored in the header so it can be raised later without breaking old messages; a decrypt refuses a count above 4,000,000. The PBKDF2 routine is built on `mbedtls_md_hmac_*` so it compiles against mbedTLS 2.x and 3.x. **Legacy format (decrypt only, deprecated):** data written by earlier releases (AES-256-CBC with PKCS#7 padding, the key being the raw password bytes zero-padded or truncated to 32, the IV derived by `EVP_BytesToKey` with SHA-512 and a fixed 8-byte salt) is still decrypted when the input does not start with the `OBKC` magic. That format has no authentication tag, so a modified ciphertext is not detected; the same key and plaintext always gave the same ciphertext; and only the first 32 password bytes mattered. `Encrypt` no longer produces it, so re-encrypt old data to move it to the new format. A legacy ciphertext that happens to begin with the four magic bytes (probability about 2^-32) would be read as the new format and rejected.
- **Base64** (`Cipher.Encrypt->Base64`, `Cipher.Decrypt->Base64`).

## How to check

Runnable examples are `programs/examples/sha_hash.obs` and `programs/examples/encrypt.obs`; compile with `obc -src programs/examples/encrypt.obs -lib cipher`. The regression suite uses `Cipher.Hash` in `ai_hub_client.obs` (SHA-256 and SHA-1 verification of downloads) and `http_stream_to_file.obs`; `programs/regression/cipher_aes_versioned.obs` covers `AES256` (round trips, long passwords, tamper and wrong-password rejection, and a fixture from the legacy format).

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
