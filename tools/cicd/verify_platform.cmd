@echo off
setlocal
REM ============================================================================
REM verify_platform.cmd <VERSION> [x64^|arm64] [--skip-build ^| --build-only]
REM
REM Release-candidate verification for ONE Windows machine. The Windows twin of
REM tools/cicd/verify_platform.sh: same steps, same result format, so
REM tools/cicd/pre_release.sh can gate a release on every machine alike.
REM
REM Run from a CLEAN checkout of the commit to be released, in a native cmd:
REM   0. prerequisites: git, openssl, Python, Visual Studio, 7-Zip (arm64 builds)
REM   1. build the deploy tree with deploy_windows.cmd (under vcvarsall)
REM   2. regression suite, default JIT, and with OBJECK_JIT_THRESHOLD=1
REM   3. VM flag tests, debugger tests, DAP tests
REM
REM Writes rc-results\<VERSION>\windows-<arch>.txt and .log, restores the tracked
REM files deploy_windows.cmd rewrites (version stamps, api.zip), and exits 0 only
REM when every step passed. A skipped test phase is a FAIL. Tests run only when
REM <arch> is this machine's architecture, so verifying another one is refused;
REM --build-only builds without testing (cross-building arm64 on an x64 host,
REM say) and always ends verdict=FAIL. --skip-build tests the existing deploy tree.
REM
REM Lessons kept here: Claude Code shells set NoDefaultCurrentDirectoryInExePath,
REM which breaks unqualified calls, so system tools are called by path; vcvarsall's
REM output must never go to the same log as the deploy (it locks the file and the
REM deploy silently never runs); vcvarsall exports PLATFORM, so no variable here
REM may use that name; a cmd started from an x64-emulated parent (Git Bash on
REM Windows ARM64) reads PROCESSOR_ARCHITECTURE=AMD64, so the host architecture
REM comes from the OS.
REM ============================================================================

set "NoDefaultCurrentDirectoryInExePath="
set "SYS32=%SystemRoot%\System32"
set "PS=%SYS32%\WindowsPowerShell\v1.0\powershell.exe"

set "VERSION=%~1"
if "%VERSION%"=="" goto usage
if "%VERSION:~0,1%"=="-" goto usage
set "ARCH="
set "SKIP_BUILD=0"
set "BUILD_ONLY=0"
:parse_args
shift
if "%~1"=="" goto args_done
if /i "%~1"=="x64" (set "ARCH=x64" & goto parse_args)
if /i "%~1"=="arm64" (set "ARCH=arm64" & goto parse_args)
if /i "%~1"=="--skip-build" (set "SKIP_BUILD=1" & goto parse_args)
if /i "%~1"=="--build-only" (set "BUILD_ONLY=1" & goto parse_args)
echo unknown argument: %~1
goto usage
:args_done
if "%SKIP_BUILD%%BUILD_ONLY%"=="11" goto usage

REM ---- host architecture ---------------------------------------------------------
REM PROCESSOR_ARCHITECTURE describes the process that built this environment, not
REM the machine: a cmd launched from Git Bash on Windows ARM64 reads AMD64, which
REM once turned an arm64 run into "tests=SKIPPED" and verdict=PASS. Ask the OS as
REM well; any source that reports ARM64 wins, and on x64 hardware none can.
set "ENVARCH=x64"
if /i "%PROCESSOR_ARCHITECTURE%"=="ARM64" set "ENVARCH=arm64"
set "HOSTARCH=%ENVARCH%"
if /i "%PROCESSOR_ARCHITEW6432%"=="ARM64" set "HOSTARCH=arm64"
for /f "tokens=3" %%a in ('%SYS32%\reg.exe query "HKLM\SYSTEM\CurrentControlSet\Control\Session Manager\Environment" /v PROCESSOR_ARCHITECTURE 2^>nul') do if /i "%%a"=="ARM64" set "HOSTARCH=arm64"
for /f "delims=" %%a in ('%PS% -NoProfile -Command "[System.Runtime.InteropServices.RuntimeInformation]::OSArchitecture" 2^>nul') do if /i "%%a"=="Arm64" set "HOSTARCH=arm64"
if "%ARCH%"=="" set "ARCH=%HOSTARCH%"

git --version >nul 2>&1
if errorlevel 1 (
    echo missing prerequisite: git ^(not on PATH^)
    exit /b 2
)
for /f "delims=" %%r in ('git rev-parse --show-toplevel 2^>nul') do set "ROOT=%%r"
if not defined ROOT (
    echo not inside a git checkout
    exit /b 2
)
set "ROOT=%ROOT:/=\%"
cd /d "%ROOT%"
for /f "delims=" %%c in ('git rev-parse HEAD') do set "COMMIT=%%c"

REM Never PLATFORM: vcvarsall.bat exports its own (x64/arm64) over it.
set "VP_PLATFORM=windows-%ARCH%"
set "OUT=%ROOT%\rc-results\%VERSION%"
set "RES=%OUT%\%VP_PLATFORM%.txt"
set "LOG=%OUT%\%VP_PLATFORM%.log"
set "BODY=%OUT%\%VP_PLATFORM%.body"
if not exist "%OUT%" mkdir "%OUT%"
type nul > "%LOG%"
type nul > "%BODY%"
set "VERDICT=PASS"
set "TESTS_RAN=0"

echo == verify_platform %VP_PLATFORM%, v%VERSION% @ %COMMIT:~0,10% on %COMPUTERNAME% (%HOSTARCH% host)
if not "%ENVARCH%"=="%HOSTARCH%" echo   note: this cmd inherited PROCESSOR_ARCHITECTURE=%PROCESSOR_ARCHITECTURE% from an emulated parent on an %HOSTARCH% OS; start the gate from a native cmd

REM ---- preconditions ----------------------------------------------------------
for /f %%n in ('git status --porcelain --untracked-files^=no ^| %SYS32%\find.exe /c /v ""') do set "DIRTY=%%n"
if not "%DIRTY%"=="0" (
    call :record tree "FAIL (uncommitted changes to tracked files; verify a clean checkout)"
    goto finish
)
call :record tree PASS
%SYS32%\findstr.exe /c:"VERSION_STRING L\"%VERSION%\"" core\shared\version.h >nul
if errorlevel 1 (
    call :record version "FAIL (core\shared\version.h is not %VERSION%)"
    goto finish
)
call :record version PASS

REM Tests run only on the architecture they verify. Another one is refused before
REM anything is built; --build-only builds it and still cannot pass.
if "%BUILD_ONLY%"=="0" if not "%ARCH%"=="%HOSTARCH%" (
    call :record tests "FAIL (%ARCH% cannot be verified on this %HOSTARCH% host; run on an %ARCH% machine, or cross-build with --build-only)"
    goto finish
)

REM ---- prerequisites: stop now, not an hour into the build ----------------------
REM The TLS regression tests shell out to openssl for their certificate and FAIL
REM without it -- deliberately, a gate that skipped TLS coverage would be worse.
set "MISSING="
if "%BUILD_ONLY%"=="1" goto prereq_build
call :find_openssl
set "PY="
python --version >nul 2>&1 && set "PY=python"
if not defined PY py -3 --version >nul 2>&1 && set "PY=py -3"
if not defined PY call :missing Python "run_vm_flag_tests.py and run_dap_tests.py need Python 3; the WindowsApps python.exe stub does not count"

:prereq_build
if "%SKIP_BUILD%"=="1" goto prereq_done
set "VCVARS="
set "VSWHERE=%ProgramFiles(x86)%\Microsoft Visual Studio\Installer\vswhere.exe"
if exist "%VSWHERE%" for /f "usebackq delims=" %%i in (`"%VSWHERE%" -latest -products * -property installationPath`) do set "VCVARS=%%i\VC\Auxiliary\Build\vcvarsall.bat"
if not defined VCVARS set "VCVARS=C:\Program Files\Microsoft Visual Studio\18\Professional\VC\Auxiliary\Build\vcvarsall.bat"
if not exist "%VCVARS%" call :missing "Visual Studio" "vswhere found no vcvarsall.bat"
if not "%ARCH%"=="arm64" goto prereq_done
if exist "%ProgramFiles%\7-Zip\7z.exe" goto prereq_done
if exist "%ProgramW6432%\7-Zip\7z.exe" goto prereq_done
%SYS32%\where.exe 7z >nul 2>&1 && goto prereq_done
call :missing 7-Zip "deploy_windows.cmd unpacks the arm64 onnxruntime.7z with it"

:prereq_done
if defined MISSING (
    call :record prereqs "FAIL (missing prerequisite: %MISSING%)"
    goto finish
)
call :record prereqs PASS
if "%BUILD_ONLY%"=="0" (
    echo ---- prerequisites >> "%LOG%"
    %SYS32%\where.exe openssl >> "%LOG%" 2>&1
    openssl version >> "%LOG%" 2>&1
    %PY% --version >> "%LOG%" 2>&1
)

REM ---- build ---------------------------------------------------------------------
if "%SKIP_BUILD%"=="1" (
    call :record build SKIPPED
    goto tests
)
set "VCARG=amd64"
if "%HOSTARCH%"=="x64" if "%ARCH%"=="arm64" set "VCARG=amd64_arm64"
if "%HOSTARCH%"=="arm64" if "%ARCH%"=="arm64" set "VCARG=arm64"
if "%HOSTARCH%"=="arm64" if "%ARCH%"=="x64" set "VCARG=arm64_amd64"
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
if "%BUILD_ONLY%"=="1" (
    call :record tests "SKIPPED (--build-only)"
    goto finish
)

:tests
set "BIN=%ROOT%\core\release\deploy-%ARCH%\bin"
REM --skip-build trusts an existing tree; make sure there is one to test.
for %%t in (obc obr obd obi obb obu) do if not exist "%BIN%\%%t.exe" call :record tool_%%t "FAIL (missing from %BIN%)"
if "%VERDICT%"=="FAIL" goto finish
pushd "%ROOT%\programs\regression"

echo ---- regression (default JIT) >> "%LOG%"
call "%ROOT%\programs\regression\run_regression.cmd" %ARCH% >> "%LOG%" 2>&1
if errorlevel 1 (call :record regression FAIL) else (call :record regression PASS)

echo ---- regression (OBJECK_JIT_THRESHOLD=1) >> "%LOG%"
set "OBJECK_JIT_THRESHOLD=1"
call "%ROOT%\programs\regression\run_regression.cmd" %ARCH% >> "%LOG%" 2>&1
if errorlevel 1 (call :record regression_jit1 FAIL) else (call :record regression_jit1 PASS)
set "OBJECK_JIT_THRESHOLD="

REM A step that reports it skipped its tests fails here even when it exits 0.
set "STEP_OUT=%OUT%\%VP_PLATFORM%.step"

echo ---- VM flag tests >> "%LOG%"
%PY% run_vm_flag_tests.py "%BIN%" > "%STEP_OUT%" 2>&1
set "RC=%ERRORLEVEL%"
call :step_result vm_flags

echo ---- debugger tests >> "%LOG%"
call "%ROOT%\programs\regression\run_debugger_tests.cmd" %ARCH% > "%STEP_OUT%" 2>&1
set "RC=%ERRORLEVEL%"
call :step_result debugger

echo ---- DAP tests >> "%LOG%"
%PY% run_dap_tests.py "%BIN%" > "%STEP_OUT%" 2>&1
set "RC=%ERRORLEVEL%"
call :step_result dap
del "%STEP_OUT%" >nul 2>&1
popd
set "TESTS_RAN=1"

:finish
REM Whatever path led here, a run that tested nothing has verified nothing.
if "%VERDICT%"=="PASS" if not "%TESTS_RAN%"=="1" call :record tests "FAIL (no test phase ran)"
> "%RES%" echo platform=%VP_PLATFORM%
>> "%RES%" echo host=%COMPUTERNAME%
>> "%RES%" echo version=%VERSION%
>> "%RES%" echo commit=%COMMIT%
for /f "delims=" %%d in ('%PS% -NoProfile -Command "(Get-Date).ToUniversalTime().ToString('yyyy-MM-ddTHH:mm:ssZ')"') do >> "%RES%" echo date=%%d
type "%BODY%" >> "%RES%"
>> "%RES%" echo verdict=%VERDICT%
del "%BODY%" >nul 2>&1
echo == verdict: %VERDICT%  (%RES%)
if "%VERDICT%"=="PASS" exit /b 0
exit /b 1

:usage
echo usage: verify_platform.cmd ^<VERSION^> [x64^|arm64] [--skip-build ^| --build-only]
exit /b 2

REM ---- step_result <key>: judge %RC% and %STEP_OUT%, append the output to the log ----
:step_result
type "%STEP_OUT%" >> "%LOG%"
if not "%RC%"=="0" (
    call :record %~1 "FAIL (exit %RC%)"
    exit /b 0
)
%SYS32%\findstr.exe /i /c:"skipping" "%STEP_OUT%" >nul
if not errorlevel 1 (
    call :record %~1 "FAIL (reported skipping its tests)"
    exit /b 0
)
call :record %~1 PASS
exit /b 0

REM ---- record <key> <value>: one result line. Anything but PASS fails the verdict;
REM the one exception is build=SKIPPED, from --skip-build.
:record
>> "%BODY%" echo %~1=%~2
echo   %~1 = %~2
set "RECORD_VALUE=%~2"
if "%RECORD_VALUE:~0,4%"=="PASS" exit /b 0
if "%~1"=="build" if "%RECORD_VALUE%"=="SKIPPED" exit /b 0
set "VERDICT=FAIL"
exit /b 0

REM ---- find_openssl: openssl on PATH, else a standard install folder, added to
REM this run's PATH only (the TLS tests reach it through obr's shell).
:find_openssl
openssl version >nul 2>&1 && exit /b 0
for %%d in ("%ProgramFiles%\OpenSSL-Win64\bin" "%ProgramFiles%\OpenSSL-Win64-ARM\bin" "%ProgramFiles%\OpenSSL\bin" "%ProgramFiles%\FireDaemon OpenSSL 3\bin") do (
    if exist "%%~d\openssl.exe" (
        echo   openssl is not on PATH; using %%~d\openssl.exe for this run
        set "PATH=%%~d;%PATH%"
        exit /b 0
    )
)
call :missing openssl "the TLS regression tests create their certificate with it; put openssl.exe on PATH"
exit /b 0

REM ---- missing <name> <why>: note a missing prerequisite ----
:missing
echo   missing prerequisite: %~1 -- %~2
if defined MISSING (set "MISSING=%MISSING%, %~1") else set "MISSING=%~1"
exit /b 0
