# Updating OpenSSL for Windows (x64 and amd64)

## For Windows x64
1.  Install [Perl](https://strawberryperl.com/) and [NASM](https://www.nasm.us/)
1.  Download the OpenSSL [source code.](https://openssl-library.org/source/)
1.  Go to the root directory and run ``perl Configure VC-WIN64A no-shared``
1.  Run ``nmake`` and copy libraries 

## For Windows arm64
1.  Install [Perl](https://strawberryperl.com/) and Visual Studio [clang-cl](https://learn.microsoft.com/en-us/cpp/build/clang-support-msbuild?view=msvc-170) support
1.  Download the OpenSSL [source code.](https://openssl-library.org/source/)
1.  Go to the root directory and run ``perl Configure VC-CLANG-WIN64-CLANGASM-ARM no-shared``
1.  Run ``nmake`` and copy libraries 