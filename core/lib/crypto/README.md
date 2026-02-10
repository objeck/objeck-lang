# Objeck Crypto Library

Cryptographic support for the Objeck language, providing hash functions, AES-256 encryption, and Base64 encoding/decoding.

The current implementation uses **Mbed TLS 3.6.3** as the underlying crypto provider, built for x64 and arm64 Windows targets.

## Building mbedTLS for Windows

### For Windows x64
The x64 libraries are already included in the repository at `objeck-lang/core/lib/openssl/win/x64/`.

### For Windows ARM64
**AUTOMATED:** The ARM64 libraries are automatically built when running `deploy_windows.cmd arm64`.

If you need to build them manually, run:
```cmd
cd objeck-lang\core\lib\crypto
build_mbedtls_arm64.cmd
```

Or see [BUILD_MBEDTLS_ARM64.md](BUILD_MBEDTLS_ARM64.md) for detailed instructions on:
- Automated build using the provided script (easiest)
- Manual build using vcpkg (recommended)
- Building from source (advanced)

Required libraries:
- `mbedtls.lib`
- `mbedcrypto.lib`
- `mbedx509.lib`

These are automatically placed in: `objeck-lang/core/lib/openssl/win/arm64/` 