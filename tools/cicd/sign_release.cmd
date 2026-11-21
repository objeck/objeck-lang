@echo off
setlocal enabledelayedexpansion

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

REM How to sign. If tools\cicd\token_pin.ps1 -Set was run, the token password is read from its
REM DPAPI-protected file and signing needs no one at the keyboard (sign_with_saved_pin.ps1 also checks the
REM installers against the CI-built SHA256SUMS first). Otherwise fall back to the interactive prompt.
if exist "%USERPROFILE%\.objeck\token_pin.dpapi" goto sign_saved_pin

REM ---- interactive: ALL the MSIs in ONE signtool run, so the token asks for its password once per release.
REM The "Token Logon" prompt is a signtool window that can open behind everything; the helper raises it.
REM It never reads or types the password: you do that.
set MSIS=
for %%f in (%STAGING%\*.msi) do set MSIS=!MSIS! "%%f"
start "" /b powershell -NoProfile -ExecutionPolicy Bypass -File "%~dp0focus_token_logon.ps1"
echo Signing all installers in one run.
echo Enter your SafeNet token password in the "Token Logon" window (it is brought to the front).
echo.
%SIGNTOOL% sign /tr %TIMESTAMP% /td SHA256 /fd SHA256 /sha1 %THUMBPRINT% !MSIS!
if errorlevel 1 (
    echo.
    echo ERROR: Failed to sign the installers
    echo Make sure your SafeNet eToken USB is plugged in and the password was accepted.
    exit /b 1
)
echo.
goto verify_signatures

:sign_saved_pin
echo Using the saved token password (tools\cicd\token_pin.ps1); no prompt.
%GH% release download v%VERSION% --pattern SHA256SUMS --dir %STAGING%
if errorlevel 1 (
    echo ERROR: could not download SHA256SUMS to check the installers against; not signing unattended.
    exit /b 1
)
powershell -NoProfile -ExecutionPolicy Bypass -File "%~dp0sign_with_saved_pin.ps1" -Dir "%STAGING%" -ManifestFile "%STAGING%\SHA256SUMS"
if errorlevel 1 (
    echo.
    echo ERROR: unattended signing did not complete. The message above says why.
    exit /b 1
)
echo.

:verify_signatures
REM Verify every signature
for %%f in (%STAGING%\*.msi) do (
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
    echo ERROR: the post-signing manifest step FAILED. It does two things and
    echo        the output above says which one stopped:
    echo.
    echo          1. regenerate SHA256SUMS over the signed MSIs and upload it
    echo          2. re-sign it in CI, because the signature published with
    echo             the release describes the manifest as it was before 1
    echo.
    echo        Stopping at 1 leaves hashes that do not match the signed
    echo        installers. Stopping at 2 leaves correct hashes that obu
    echo        REFUSES, so "obu update" will not install this release at
    echo        all. Do not announce until one of these reports success:
    echo.
    echo          powershell -File tools\cicd\update_sha256sums.ps1 %VERSION%
    echo          gh workflow run resign-manifest.yml -f version=%VERSION%
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
