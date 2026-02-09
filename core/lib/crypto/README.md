# Objeck Crypto Library

Cryptographic support for the Objeck language, providing hash functions, AES-256 encryption, and Base64 encoding/decoding.

The current implementation uses **Mbed TLS 3.6.3** as the underlying crypto provider, built for x64 and arm64 Windows targets.

## Building mbedTLS for Windows

### For Windows x64
The x64 libraries are already included in the repository at `objeck-lang/core/lib/openssl/win/x64/`.

### For Windows ARM64
**IMPORTANT:** The ARM64 libraries are NOT included in the repository. You must build or download them.

See [BUILD_MBEDTLS_ARM64.md](BUILD_MBEDTLS_ARM64.md) for detailed instructions on how to:
- Build mbedTLS 3.6.3 for ARM64 using vcpkg (recommended)
- Build mbedTLS 3.6.3 for ARM64 from source

Required libraries:
- `mbedtls.lib`
- `mbedcrypto.lib`
- `mbedx509.lib`

These must be placed in: `objeck-lang/core/lib/openssl/win/arm64/` 