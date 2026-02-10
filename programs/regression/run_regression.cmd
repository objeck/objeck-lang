@echo off
REM Regression test runner for Windows
REM Usage: run_regression.cmd [x64|arm64]

setlocal enabledelayedexpansion

set PLATFORM=%1
if "%PLATFORM%"=="" set PLATFORM=x64

set COMPILER=..\..\core\release\deploy-%PLATFORM%\bin\obc.exe
set VM=..\..\core\release\deploy-%PLATFORM%\bin\obr.exe
set RESULTS_DIR=results

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

    %COMPILER% -src %%f -lib cipher,collect,xml,json -opt s3 -dest %%~nf.obe > "%RESULTS_DIR%\%%~nf_compile.log" 2>&1

    if errorlevel 1 (
        echo   FAIL ^(compilation error^)
        set /a FAIL_COUNT+=1
    ) else (
        %VM% %%~nf.obe > "%RESULTS_DIR%\%%~nf_output.txt" 2>&1

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
