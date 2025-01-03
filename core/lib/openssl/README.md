# Updating OpenSSL for Windows (x64 and amd64)

## For Windows x64
0.  Install [Perl](https://strawberryperl.com/) and [NASM](https://www.nasm.us/)
0.  Download the OpenSSL [source code.](https://openssl-library.org/source/)
0.  Go to the root direcotry and run ``perl Configure VC-WIN64A no-shared``

## For Windows arm64
0.  Install [Perl](https://strawberryperl.com/)
0.  Install Visual Studio [clang-cl](https://learn.microsoft.com/en-us/cpp/build/clang-support-msbuild?view=msvc-170) from the Visual Studio installer
0.  Download the OpenSSL [source code.](https://openssl-library.org/source/)
0.  Go to the root directory and run ``perl Configure VC-CLANG-WIN64-CLANGASM-ARM no-shared``