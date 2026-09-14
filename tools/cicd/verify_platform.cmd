@echo off
setlocal
REM ============================================================================
REM verify_platform.cmd <VERSION> [x64^|arm64] [--skip-build]
REM
REM Release-candidate verification for ONE Windows machine. The Windows twin of
REM tools/cicd/verify_platform.sh: same steps, same result format, so
REM tools/cicd/pre_release.sh can gate a release on every machine alike.
REM
REM Run from a CLEAN checkout of the commit to be released:
REM   1. build the deploy tree with deploy_windows.cmd (under vcvarsall)
REM   2. regression suite, default JIT, and with OBJECK_JIT_THRESHOLD=1
REM   3. VM flag tests, debugger tests, DAP tests
REM
REM Writes rc-results\<VERSION>\windows-<arch>.txt and .log, restores the tracked
REM files deploy_windows.cmd rewrites (version stamps, api.zip), and exits 0 only
REM when every step passed. Tests run only when <arch> matches this machine;
REM an x64 host can cross-build arm64 but cannot run it.
REM
REM Lessons kept here: Claude Code shells set NoDefaultCurrentDirectoryInExePath,
REM which breaks unqualified calls; vcvarsall's output must never go to the same
REM log as the deploy (it locks the file and the deploy silently never runs).
REM ============================================================================

set "NoDefaultCurrentDirectoryInExePath="
set "VERSION=%~1"
if "%VERSION%"=="" (
    echo usage: verify_platform.cmd ^<VERSION^> [x64^|arm64] [--skip-build]
    exit /b 2
)

set "HOSTARCH=x64"
if /i "%PROCESSOR_ARCHITECTURE%"=="ARM64" set "HOSTARCH=arm64"
set "ARCH=%~2"
if "%ARCH%"=="" set "ARCH=%HOSTARCH%"
if "%ARCH%"=="--skip-build" set "ARCH=%HOSTARCH%"
set "SKIP_BUILD=0"
if "%~2"=="--skip-build" set "SKIP_BUILD=1"
if "%~3"=="--skip-build" set "SKIP_BUILD=1"

for /f "delims=" %%r in ('git rev-parse --show-toplevel 2^>nul') do set "ROOT=%%r"
if not defined ROOT (
    echo not inside a git checkout
    exit /b 2
)
set "ROOT=%ROOT:/=\%"
cd /d "%ROOT%"
for /f "delims=" %%c in ('git rev-parse HEAD') do set "COMMIT=%%c"

set "PLATFORM=windows-%ARCH%"
set "OUT=%ROOT%\rc-results\%VERSION%"
set "RES=%OUT%\%PLATFORM%.txt"
set "LOG=%OUT%\%PLATFORM%.log"
set "BODY=%OUT%\%PLATFORM%.body"
if not exist "%OUT%" mkdir "%OUT%"
type nul > "%LOG%"
type nul > "%BODY%"
set "VERDICT=PASS"

echo == verify_platform %PLATFORM%, v%VERSION% @ %COMMIT:~0,10% on %COMPUTERNAME%

REM ---- preconditions ----------------------------------------------------------
for /f %%n in ('git status --porcelain --untracked-files^=no ^| %SystemRoot%\System32\find.exe /c /v ""') do set "DIRTY=%%n"
if not "%DIRTY%"=="0" (
    call :record tree "FAIL (uncommitted changes to tracked files; verify a clean checkout)"
    goto finish
)
call :record tree PASS
findstr /c:"VERSION_STRING L\"%VERSION%\"" core\shared\version.h >nul
if errorlevel 1 (
    call :record version "FAIL (core\shared\version.h is not %VERSION%)"
    goto finish
)
call :record version PASS

REM ---- build ---------------------------------------------------------------------
if "%SKIP_BUILD%"=="1" (
    call :record build SKIPPED
    goto tests
)
set "VCVARS="
set "VSWHERE=%ProgramFiles(x86)%\Microsoft Visual Studio\Installer\vswhere.exe"
if exist "%VSWHERE%" for /f "usebackq delims=" %%i in (`"%VSWHERE%" -latest -products * -property installationPath`) do set "VCVARS=%%i\VC\Auxiliary\Build\vcvarsall.bat"
if not defined VCVARS set "VCVARS=C:\Program Files\Microsoft Visual Studio\18\Professional\VC\Auxiliary\Build\vcvarsall.bat"
if not exist "%VCVARS%" (
    call :record build "FAIL (vcvarsall.bat not found)"
    goto finish
)
set "VCARG=amd64"
if "%HOSTARCH%"=="arm64" if "%ARCH%"=="arm64" set "VCARG=arm64"
if "%HOSTARCH%"=="x64" if "%ARCH%"=="arm64" set "VCARG=amd64_arm64"
call "%VCVARS%" %VCARG% >nul 2>&1
if errorlevel 1 (
    call :record build "FAIL (vcvarsall %VCARG% failed)"
    goto finish
)
echo ---- deploy_windows.cmd %ARCH% >> "%LOG%"
REM CI mode streams plain output instead of relaunching the live progress UI.
set "CI=1"
pushd "%ROOT%\core\release"
call "%ROOT%\core\release\deploy_windows.cmd" %ARCH% >> "%LOG%" 2>&1
set "RC=%ERRORLEVEL%"
popd
set "CI="
set "BIN=%ROOT%\core\release\deploy-%ARCH%\bin"
if not "%RC%"=="0" (
    call :record build "FAIL (deploy_windows.cmd exit %RC%)"
) else (
    call :record build PASS
)
for %%t in (obc obr obd obi obb obu) do if not exist "%BIN%\%%t.exe" call :record tool_%%t "FAIL (missing from %BIN%)"
REM The tree was clean at the start, so restore what the deploy rewrote.
git checkout -- . >> "%LOG%" 2>&1
if "%VERDICT%"=="FAIL" goto finish

:tests
set "BIN=%ROOT%\core\release\deploy-%ARCH%\bin"
if not "%ARCH%"=="%HOSTARCH%" (
    call :record tests "SKIPPED (%ARCH% cannot run on a %HOSTARCH% host)"
    goto finish
)
set "PY=python"
where python >nul 2>&1 || set "PY=py -3"
pushd "%ROOT%\programs\regression"

echo ---- regression (default JIT) >> "%LOG%"
call run_regression.cmd %ARCH% >> "%LOG%" 2>&1
if errorlevel 1 (call :record regression FAIL) else (call :record regression PASS)

echo ---- regression (OBJECK_JIT_THRESHOLD=1) >> "%LOG%"
set "OBJECK_JIT_THRESHOLD=1"
call run_regression.cmd %ARCH% >> "%LOG%" 2>&1
if errorlevel 1 (call :record regression_jit1 FAIL) else (call :record regression_jit1 PASS)
set "OBJECK_JIT_THRESHOLD="

REM A step that reports it skipped its tests fails here even when it exits 0.
set "STEP_OUT=%OUT%\%PLATFORM%.step"

echo ---- VM flag tests >> "%LOG%"
%PY% run_vm_flag_tests.py "%BIN%" > "%STEP_OUT%" 2>&1
set "RC=%ERRORLEVEL%"
call :step_result vm_flags

echo ---- debugger tests >> "%LOG%"
call run_debugger_tests.cmd %ARCH% > "%STEP_OUT%" 2>&1
set "RC=%ERRORLEVEL%"
call :step_result debugger

echo ---- DAP tests >> "%LOG%"
%PY% run_dap_tests.py "%BIN%" > "%STEP_OUT%" 2>&1
set "RC=%ERRORLEVEL%"
call :step_result dap
del "%STEP_OUT%" >nul 2>&1
popd

:finish
> "%RES%" echo platform=%PLATFORM%
>> "%RES%" echo host=%COMPUTERNAME%
>> "%RES%" echo version=%VERSION%
>> "%RES%" echo commit=%COMMIT%
for /f "delims=" %%d in ('powershell -NoProfile -Command "(Get-Date).ToUniversalTime().ToString('yyyy-MM-ddTHH:mm:ssZ')"') do >> "%RES%" echo date=%%d
type "%BODY%" >> "%RES%"
>> "%RES%" echo verdict=%VERDICT%
del "%BODY%" >nul 2>&1
echo == verdict: %VERDICT%  (%RES%)
if "%VERDICT%"=="PASS" exit /b 0
exit /b 1

REM ---- step_result <key>: judge %RC% and %STEP_OUT%, append the output to the log ----
:step_result
type "%STEP_OUT%" >> "%LOG%"
if not "%RC%"=="0" (
    call :record %~1 "FAIL (exit %RC%)"
    exit /b 0
)
findstr /i /c:"skipping" "%STEP_OUT%" >nul
if not errorlevel 1 (
    call :record %~1 "FAIL (reported skipping its tests)"
    exit /b 0
)
call :record %~1 PASS
exit /b 0

REM ---- record <key> <value>: one result line; any FAIL value fails the verdict ----
:record
>> "%BODY%" echo %~1=%~2
echo   %~1 = %~2
echo %~2 | findstr /b /c:"FAIL" >nul && set "VERDICT=FAIL"
exit /b 0
