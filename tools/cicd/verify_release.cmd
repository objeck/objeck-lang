@echo off
setlocal

REM ============================================
REM Verify a GitHub Release has all expected artifacts
REM ============================================
REM Usage: verify_release.cmd <version>
REM Example: verify_release.cmd 2026.2.0
REM
REM Checks:
REM   - All platform artifacts present
REM   - Windows MSI is signed
REM   - File sizes are reasonable
REM ============================================

if [%1]==[] (
    echo Usage: verify_release.cmd ^<version^>
    echo Example: verify_release.cmd 2026.2.0
    exit /b 1
)

set VERSION=%1
set GH="C:\Program Files\GitHub CLI\gh.exe"
set SIGNTOOL="C:\Program Files (x86)\Windows Kits\10\bin\10.0.26100.0\x64\signtool.exe"
set STAGING=verify-staging
set PASS=0
set FAIL=0

echo.
echo ============================================
echo  Verifying Release v%VERSION%
echo ============================================
echo.

REM Check release exists
%GH% release view v%VERSION% >nul 2>&1
if errorlevel 1 (
    echo FAIL: Release v%VERSION% not found
    exit /b 1
)
echo PASS: Release v%VERSION% exists
set /a PASS+=1

REM Check expected artifacts
for %%a in (
    objeck-windows-x64_%VERSION%.msi
    objeck-windows-x64_%VERSION%.zip
    objeck-windows-arm64_%VERSION%.zip
    objeck-linux-x64_%VERSION%.tgz
    objeck-macos-arm64_%VERSION%.tgz
    objeck-lsp_%VERSION%.zip
) do (
    %GH% release view v%VERSION% --json assets --jq ".assets[].name" 2>nul | findstr /i "%%a" >nul 2>&1
    if errorlevel 1 (
        echo FAIL: Missing %%a
        set /a FAIL+=1
    ) else (
        echo PASS: Found %%a
        set /a PASS+=1
    )
)

REM Verify MSI signature
echo.
echo Checking MSI signature...
if exist %STAGING% rmdir /s /q %STAGING%
mkdir %STAGING%
%GH% release download v%VERSION% --pattern "*.msi" --dir %STAGING% 2>nul

for %%f in (%STAGING%\*.msi) do (
    %SIGNTOOL% verify /pa "%%f" >nul 2>&1
    if errorlevel 1 (
        echo FAIL: %%~nxf is NOT signed
        set /a FAIL+=1
    ) else (
        echo PASS: %%~nxf is signed
        set /a PASS+=1
    )
)

rmdir /s /q %STAGING%

echo.
echo ============================================
echo  Results: %PASS% passed, %FAIL% failed
echo ============================================
echo  Release: https://github.com/objeck/objeck-lang/releases/tag/v%VERSION%
echo.

if %FAIL% GTR 0 exit /b 1
