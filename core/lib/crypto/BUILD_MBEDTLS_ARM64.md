# Building mbedTLS for Windows ARM64

The crypto library requires mbedTLS 3.6.3 for Windows ARM64. Follow these steps to build the required libraries.

## Prerequisites

1. Visual Studio 2022 or later with ARM64 build tools
2. CMake (install via Visual Studio Installer or from https://cmake.org/)
3. Git

## Option 1: Using the Automated Script (Easiest)

Simply run the provided build script:

```cmd
cd objeck-lang\core\lib\crypto
build_mbedtls_arm64.cmd
```

This script will:
- Download and install vcpkg if needed
- Build mbedTLS 3.6.3 for ARM64
- Copy the Release libraries to the correct location

## Option 2: Using vcpkg Manually

For more control or to build Debug libraries:

```cmd
REM 1. Install vcpkg if you haven't already
git clone https://github.com/Microsoft/vcpkg.git
cd vcpkg
bootstrap-vcpkg.bat

REM 2. Build mbedTLS for ARM64 (Release)
vcpkg install mbedtls:arm64-windows-static

REM 3. Build mbedTLS for ARM64 (Debug) - optional
vcpkg install mbedtls:arm64-windows-static-dbg

REM 4. Copy the Release libraries to the project
copy vcpkg\installed\arm64-windows-static\lib\mbedtls.lib "C:\Users\objec\Documents\Code\objeck-lang\core\lib\openssl\win\arm64\"
copy vcpkg\installed\arm64-windows-static\lib\mbedcrypto.lib "C:\Users\objec\Documents\Code\objeck-lang\core\lib\openssl\win\arm64\"
copy vcpkg\installed\arm64-windows-static\lib\mbedx509.lib "C:\Users\objec\Documents\Code\objeck-lang\core\lib\openssl\win\arm64\"

REM 5. For Debug builds, copy debug libraries (optional)
REM Note: Debug libraries from vcpkg are typically in the same directory with 'd' suffix or in debug/lib
REM If needed, you can build both configurations and organize them accordingly
```

## Option 3: Building from Source

If you prefer to build from source with full control:

```cmd
REM 1. Download mbedTLS 3.6.3
git clone --branch v3.6.3 https://github.com/Mbed-TLS/mbedtls.git
cd mbedtls

REM 2. Open a Visual Studio ARM64 Developer Command Prompt
REM Start -> Visual Studio 2022 -> ARM64 Native Tools Command Prompt

REM 3. Build for ARM64
mkdir build-arm64
cd build-arm64
cmake -G "Visual Studio 17 2022" -A ARM64 -DCMAKE_BUILD_TYPE=Release -DENABLE_PROGRAMS=OFF -DENABLE_TESTING=OFF -DUSE_STATIC_MBEDTLS_LIBRARY=ON ..
cmake --build . --config Release

REM 4. Copy the libraries
REM Libraries will be in: build-arm64\library\Release\
copy library\Release\mbedtls.lib "C:\Users\objec\Documents\Code\objeck-lang\core\lib\openssl\win\arm64\"
copy library\Release\mbedcrypto.lib "C:\Users\objec\Documents\Code\objeck-lang\core\lib\openssl\win\arm64\"
copy library\Release\mbedx509.lib "C:\Users\objec\Documents\Code\objeck-lang\core\lib\openssl\win\arm64\"
```

## Verification

After copying the libraries, verify they exist:

```cmd
dir "C:\Users\objec\Documents\Code\objeck-lang\core\lib\openssl\win\arm64\*.lib"
```

You should see:
- mbedtls.lib
- mbedcrypto.lib
- mbedx509.lib

Now you can run `deploy_windows.cmd arm64` successfully.
