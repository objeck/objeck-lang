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

REM Select the certificate by thumbprint, NOT with /a.
REM
REM /a picks the "best" VALID certificate, and this machine's store holds three
REM valid code-signing certs with private keys -- CN=Randy Hollines (the Sectigo
REM token cert), plus self-signed CN=LVS Dev and CN=localhost. /a worked when the
REM token was the only candidate; now it could pick a self-signed cert and produce
REM installers that carry a signature yet fail every trust check, which is worse
REM than shipping them unsigned.
REM
REM CN=Randy Hollines, issued by Sectigo Public Code Signing CA R36, expires
REM 2028-05-25. On renewal, get the new value with the token plugged in:
REM   powershell -c "Get-ChildItem Cert:\CurrentUser\My -CodeSigningCert | Format-List Subject,Thumbprint,NotAfter"
set THUMBPRINT=6DEAD3A9C58C82DD3CE979DBDFAF64CC964C0A46

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
    %SIGNTOOL% sign /tr %TIMESTAMP% /td SHA256 /fd SHA256 /sha1 %THUMBPRINT% "%%f"
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

REM Regenerate SHA256SUMS -- signing REWROTE the MSIs, so the manifest published
REM alongside them now describes bytes that no longer exist. Without this, every
REM user running `sha256sum -c SHA256SUMS` sees both Windows installers FAIL --
REM which is what v2026.8.3 shipped, because this step lived only in a human's
REM memory and in post_release.sh's gate 4. Signing is not finished until the
REM manifest describes the signed bytes.
echo.
echo Regenerating SHA256SUMS for the signed installers...
powershell -NoProfile -ExecutionPolicy Bypass -File "%~dp0update_sha256sums.ps1" %VERSION%
if errorlevel 1 (
    echo.
    echo ERROR: SHA256SUMS was NOT updated - the release now advertises hashes
    echo        that do not match the signed MSIs. Fix before announcing:
    echo          powershell -File tools\cicd\update_sha256sums.ps1 %VERSION%
    exit /b 1
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
