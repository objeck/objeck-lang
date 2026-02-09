# Quick Start: Building Objeck for Windows ARM64

## Prerequisites
1. Windows ARM64 device or Windows 11 with ARM64 emulation
2. Visual Studio 2022 or later with:
   - ARM64 build tools
   - C++ desktop development workload
   - clang-cl for ARM64 (optional, for optimizations)
3. Git (for vcpkg)

## One-Time Setup: Install mbedTLS ARM64

The repository is missing mbedTLS libraries for ARM64. Run this **one time**:

```cmd
cd core\lib\crypto
build_mbedtls_arm64.cmd
```

This will:
- Download and build mbedTLS 3.6.3 for ARM64
- Copy libraries to the correct location
- Take 5-10 minutes depending on your machine

## Building Objeck

### Option 1: Full Build (Recommended for distribution)

Open a **Visual Studio ARM64 Native Tools Command Prompt**:

```cmd
cd core\release
deploy_windows.cmd arm64
```

This will:
- Build the compiler (obc.exe)
- Build the VM (obr.exe)
- Build the REPL (obi.exe)
- Build all native libraries
- Build the debugger
- Copy examples and documentation
- Create deployment package in `deploy-arm64/`

Build time: ~15-30 minutes

### Option 2: Core Build Only (Quick development)

Open `core\release\objeck.sln` in Visual Studio:
1. Select **Release** configuration
2. Select **ARM64** platform
3. Build Solution (Ctrl+Shift+B)

This builds only the core components:
- Compiler
- VM
- REPL

Build time: ~2-5 minutes

## Testing the Build

After building, test the installation:

```cmd
cd core\release\deploy-arm64\bin

REM Test the compiler
obc.exe -version

REM Test the VM
obr.exe -version

REM Test a simple program
echo bundle Default { class Test { function : Main(args : String[]) ~ Nil { "Hello ARM64!"->PrintLine(); } } } > test.obs
..\bin\obc.exe -src test.obs -dest test.obe
..\bin\obr.exe test.obe
```

Expected output:
```
Hello ARM64!
```

## Common Issues

### Issue: "Cannot find mbedtls.lib"
**Solution:** Run `core\lib\crypto\build_mbedtls_arm64.cmd`

### Issue: "vcruntime140.dll not found"
**Solution:** Install Visual C++ Redistributables for ARM64 from Microsoft

### Issue: Build errors in crypto library
**Solution:**
1. Ensure mbedTLS ARM64 libraries are installed
2. Check that `core\lib\openssl\win\arm64\` contains:
   - mbedtls.lib
   - mbedcrypto.lib
   - mbedx509.lib

### Issue: "LINK : fatal error LNK1181: cannot open input file 'libz-static.lib'"
**Solution:** The zlib ARM64 library should already be in the repo at `core\lib\zlib\win\arm64\libz-static.lib`. If missing, rebuild zlib or check repo integrity.

## Build Configurations

All libraries support these configurations:
- **Debug|ARM64** - Debug symbols, no optimization
- **Release|ARM64** - Optimized, no debug symbols
- **Debug|x64** - x64 debug build
- **Release|x64** - x64 release build

## Directory Structure

After full build:
```
core/release/
├── deploy-arm64/           # ARM64 distribution
│   ├── bin/               # Executables and DLLs
│   ├── lib/               # Objeck libraries (.obl)
│   │   └── native/        # Native wrapper DLLs
│   ├── examples/          # Example programs
│   └── doc/               # Documentation
└── objeck.sln            # Visual Studio solution
```

## Next Steps

1. ✅ Run `build_mbedtls_arm64.cmd` (one-time setup)
2. ✅ Run `deploy_windows.cmd arm64` (full build)
3. ✅ Test the build with sample programs
4. ✅ Set environment variables (see `deploy-arm64/readme.html`)
5. ✅ Start developing!

## Additional Resources

- [mbedTLS Build Instructions](../lib/crypto/BUILD_MBEDTLS_ARM64.md)
- [ARM64 Build Fix Details](../lib/crypto/WINDOWS_ARM64_BUILD_FIX.md)
- [Main README](../../readme.md)
