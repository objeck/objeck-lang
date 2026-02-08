# Objeck Crypto Library

Cryptographic support for the Objeck language, providing hash functions, AES-256 encryption, and Base64 encoding/decoding.

The current implementation uses OpenSSL-3.4.0 (released 22-Oct-2024) as the underlying crypto provider, built for x64 and arm64 Windows targets.

## Building OpenSSL for Windows

### For Windows x64
1.  Install [Perl](https://strawberryperl.com/) and [NASM](https://www.nasm.us/)
1.  Download the OpenSSL [source code](https://openssl-library.org/source/)
1.  Go to the root directory and run ``perl Configure VC-WIN64A no-shared``
1.  Run ``nmake`` and copy libraries 

### For Windows arm64
1.  Install [Perl](https://strawberryperl.com/) and Visual Studio [clang-cl](https://learn.microsoft.com/en-us/cpp/build/clang-support-msbuild?view=msvc-170) support
1.  Download the OpenSSL [source code](https://openssl-library.org/source/)
1.  Go to the root directory and run ``perl Configure VC-CLANG-WIN64-CLANGASM-ARM no-shared``
1.  Run ``nmake`` and copy libraries 