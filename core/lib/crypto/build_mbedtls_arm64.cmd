@echo off
REM Automated script to build mbedTLS 3.6.3 for Windows ARM64 using vcpkg
REM This script requires Visual Studio 2022 or later with ARM64 tools

setlocal

set SCRIPT_DIR=%~dp0
set OPENSSL_ARM64_DIR=%SCRIPT_DIR%..\openssl\win\arm64
set VCPKG_DIR=%TEMP%\vcpkg-mbedtls-build

echo.
echo ============================================================
echo  Building mbedTLS 3.6.3 for Windows ARM64
echo ============================================================
echo.

REM Check if libraries already exist
if exist "%OPENSSL_ARM64_DIR%\mbedtls.lib" (
    if exist "%OPENSSL_ARM64_DIR%\mbedcrypto.lib" (
        if exist "%OPENSSL_ARM64_DIR%\mbedx509.lib" (
            echo mbedTLS ARM64 libraries already exist at:
            echo %OPENSSL_ARM64_DIR%
            echo.
            choice /C YN /M "Do you want to rebuild them?"
            if errorlevel 2 goto end
            echo.
        )
    )
)

REM Create ARM64 directory if it doesn't exist
if not exist "%OPENSSL_ARM64_DIR%" (
    echo Creating ARM64 library directory...
    mkdir "%OPENSSL_ARM64_DIR%"
)

REM Check for vcpkg
echo Checking for vcpkg...
if not exist "%VCPKG_DIR%" (
    echo vcpkg not found. Installing vcpkg to: %VCPKG_DIR%
    echo.

    git clone https://github.com/Microsoft/vcpkg.git "%VCPKG_DIR%"
    if errorlevel 1 (
        echo ERROR: Failed to clone vcpkg. Make sure git is installed.
        goto error
    )

    call "%VCPKG_DIR%\bootstrap-vcpkg.bat"
    if errorlevel 1 (
        echo ERROR: Failed to bootstrap vcpkg.
        goto error
    )
) else (
    echo vcpkg found at: %VCPKG_DIR%
)

echo.
echo Building mbedTLS for ARM64-Windows-Static...
echo This may take several minutes...
echo.

call "%VCPKG_DIR%\vcpkg.exe" install mbedtls:arm64-windows-static
if errorlevel 1 (
    echo ERROR: Failed to build mbedTLS with vcpkg.
    goto error
)

echo.
echo Copying libraries to project...
copy /Y "%VCPKG_DIR%\installed\arm64-windows-static\lib\mbedtls.lib" "%OPENSSL_ARM64_DIR%\"
copy /Y "%VCPKG_DIR%\installed\arm64-windows-static\lib\mbedcrypto.lib" "%OPENSSL_ARM64_DIR%\"
copy /Y "%VCPKG_DIR%\installed\arm64-windows-static\lib\mbedx509.lib" "%OPENSSL_ARM64_DIR%\"

echo.
echo ============================================================
echo  SUCCESS! mbedTLS ARM64 libraries installed
echo ============================================================
echo.
echo Libraries installed to:
echo %OPENSSL_ARM64_DIR%
echo.
dir "%OPENSSL_ARM64_DIR%\*.lib"
echo.
echo You can now run: deploy_windows.cmd arm64
echo.
goto end

:error
echo.
echo ============================================================
echo  BUILD FAILED
echo ============================================================
echo.
echo Please check the error messages above.
echo For manual build instructions, see: BUILD_MBEDTLS_ARM64.md
echo.
pause
exit /b 1

:end
pause
exit /b 0
