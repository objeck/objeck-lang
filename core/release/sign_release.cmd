@echo off
setlocal

REM ============================================
REM Sign Windows MSI Release Artifacts
REM ============================================
REM Usage: sign_release.cmd <version>
REM Example: sign_release.cmd 2026.2.0
REM
REM Prerequisites:
REM   - SafeNet eToken USB plugged in
REM   - GitHub CLI (gh) authenticated
REM   - Windows SDK signtool in PATH
REM ============================================

if [%1]==[] (
    echo Usage: sign_release.cmd ^<version^>
    echo Example: sign_release.cmd 2026.2.0
    exit /b 1
)

set VERSION=%1
set STAGING=sign-staging
set SIGNTOOL="C:\Program Files (x86)\Windows Kits\10\bin\10.0.26100.0\x64\signtool.exe"
set GH="C:\Program Files\GitHub CLI\gh.exe"
set TIMESTAMP=http://timestamp.sectigo.com

echo.
echo ============================================
echo  Signing Release v%VERSION%
echo ============================================
echo.

REM Clean staging
if exist %STAGING% rmdir /s /q %STAGING%
mkdir %STAGING%

REM Download MSI from release
echo Downloading MSI from GitHub release v%VERSION%...
%GH% release download v%VERSION% --pattern "*.msi" --dir %STAGING%
if errorlevel 1 (
    echo.
    echo ERROR: Failed to download MSI from release v%VERSION%
    echo Make sure the release exists and has MSI files.
    exit /b 1
)

echo.
echo Downloaded files:
dir /b %STAGING%\*.msi
echo.

REM Sign each MSI
for %%f in (%STAGING%\*.msi) do (
    echo Signing %%~nxf...
    echo Please enter your SafeNet token password when prompted.
    echo.
    %SIGNTOOL% sign /tr %TIMESTAMP% /td SHA256 /fd SHA256 /a "%%f"
    if errorlevel 1 (
        echo.
        echo ERROR: Failed to sign %%~nxf
        echo Make sure your SafeNet eToken USB is plugged in.
        exit /b 1
    )
    echo.

    REM Verify signature
    echo Verifying signature on %%~nxf...
    %SIGNTOOL% verify /pa "%%f"
    if errorlevel 1 (
        echo ERROR: Signature verification failed for %%~nxf
        exit /b 1
    )
    echo Verified: %%~nxf
    echo.
)

REM Upload signed MSI back to release
echo Uploading signed MSI files to release v%VERSION%...
for %%f in (%STAGING%\*.msi) do (
    %GH% release upload v%VERSION% "%%f" --clobber
    if errorlevel 1 (
        echo ERROR: Failed to upload %%~nxf
        exit /b 1
    )
    echo Uploaded: %%~nxf
)

REM Cleanup
rmdir /s /q %STAGING%

echo.
echo ============================================
echo  Signing complete for v%VERSION%
echo ============================================
echo.
echo Release: https://github.com/objeck/objeck-lang/releases/tag/v%VERSION%
echo.
