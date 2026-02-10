@echo off
REM Regression test runner for Windows
REM Usage: run_regression.cmd [x64|arm64]

setlocal enabledelayedexpansion

set PLATFORM=%1
if "%PLATFORM%"=="" set PLATFORM=x64

REM Detect platform-specific deployment directory
set DEPLOY_DIR=..\..\core\release\deploy-%PLATFORM%
if not exist "%DEPLOY_DIR%" (
    set DEPLOY_DIR=..\..\core\release\deploy
    if not exist "!DEPLOY_DIR!" (
        echo ERROR: Could not find deployment directory
        echo Expected: ..\..\core\release\deploy-%PLATFORM% or ..\..\core\release\deploy
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
set ABS_VM=%CD%\obr.exe
popd

if not exist "%RESULTS_DIR%" mkdir "%RESULTS_DIR%"

set PASS_COUNT=0
set FAIL_COUNT=0

echo ========================================
echo   Objeck Regression Test Suite
echo   Platform: %PLATFORM%
echo ========================================
echo.

for %%f in (*.obs) do (
    echo Running: %%~nf...

    REM Change to compiler directory so it can find ../lib
    pushd "%ABS_BIN_DIR%"
    "%ABS_COMPILER%" -src "%REGRESSION_DIR%\%%f" -lib cipher,collect,xml,json -opt s3 -dest "%REGRESSION_DIR%\%%~nf.obe" > "%REGRESSION_DIR%\%RESULTS_DIR%\%%~nf_compile.log" 2>&1
    set COMPILE_RESULT=!errorlevel!
    popd

    if !COMPILE_RESULT! neq 0 (
        echo   FAIL ^(compilation error^)
        set /a FAIL_COUNT+=1
    ) else (
        REM Run from regression directory
        "%ABS_VM%" "%%~nf.obe" > "%RESULTS_DIR%\%%~nf_output.txt" 2>&1

        if errorlevel 1 (
            echo   FAIL ^(runtime error^)
            set /a FAIL_COUNT+=1
        ) else (
            echo   PASS
            set /a PASS_COUNT+=1
        )
    )
)

echo.
echo ========================================
echo   Results: !PASS_COUNT! passed, !FAIL_COUNT! failed
echo ========================================

if !FAIL_COUNT! gtr 0 exit /b 1
exit /b 0
