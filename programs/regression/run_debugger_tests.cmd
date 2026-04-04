@echo off
REM Debugger regression tests for Windows
REM Usage: run_debugger_tests.cmd [x64|arm64]

setlocal enabledelayedexpansion

set PLATFORM=%1
if "%PLATFORM%"=="" set PLATFORM=x64

REM Detect platform-specific deployment directory
set DEPLOY_DIR=..\..\core\release\deploy-%PLATFORM%
if not exist "%DEPLOY_DIR%" (
    set DEPLOY_DIR=..\..\core\release\deploy
    if not exist "!DEPLOY_DIR!" (
        echo ERROR: Could not find deployment directory
        exit /b 1
    )
)

set BIN_DIR=%DEPLOY_DIR%\bin
set RESULTS_DIR=results
set REGRESSION_DIR=%CD%

REM Get absolute paths
pushd "%BIN_DIR%"
set ABS_BIN_DIR=%CD%
set ABS_COMPILER=%CD%\obc.exe
set ABS_DEBUGGER=%CD%\obd.exe
popd

REM Set library path
pushd "%DEPLOY_DIR%\lib"
set OBJECK_LIB_PATH=%CD%
popd

set NATIVE_LIB_DIR=%DEPLOY_DIR%\lib\native
if exist "%NATIVE_LIB_DIR%" set PATH=%NATIVE_LIB_DIR%;%PATH%

if not exist "%RESULTS_DIR%" mkdir "%RESULTS_DIR%"

REM Check for debugger binary
if not exist "%ABS_DEBUGGER%" (
    echo WARNING: debugger binary not found at %ABS_DEBUGGER%, skipping
    exit /b 0
)

set PASS_COUNT=0
set FAIL_COUNT=0

echo ========================================
echo   Objeck Debugger Test Suite
echo   Platform: %PLATFORM%
echo ========================================
echo.

REM Compile test program with debug symbols
echo Compiling debugger test program...
pushd "%ABS_BIN_DIR%"
"%ABS_COMPILER%" -src "%REGRESSION_DIR%\debugger_test.obs" -dest "%REGRESSION_DIR%\debugger_test.obe" -debug > "%REGRESSION_DIR%\%RESULTS_DIR%\debugger_compile.log" 2>&1
set COMPILE_RESULT=!errorlevel!
popd

if !COMPILE_RESULT! neq 0 (
    echo   FAIL ^(compilation error^)
    exit /b 1
)
echo   Compiled successfully.
echo.

REM Run debugger tests via PowerShell
powershell.exe -ExecutionPolicy Bypass -File "%REGRESSION_DIR%\run_debugger_tests_win.ps1" -Debugger "%ABS_DEBUGGER%" -TestBin "%REGRESSION_DIR%\debugger_test.obe" -SrcDir "%REGRESSION_DIR%" -ResultsDir "%REGRESSION_DIR%\%RESULTS_DIR%"
set TEST_RESULT=!errorlevel!

REM Clean up
del /q "%REGRESSION_DIR%\debugger_test.obe" 2>nul

exit /b !TEST_RESULT!
