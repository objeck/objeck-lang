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

REM Set library paths for native module loading (use absolute paths)
pushd "%DEPLOY_DIR%\lib"
set OBJECK_LIB_PATH=%CD%
popd
set NATIVE_LIB_DIR=%DEPLOY_DIR%\lib\native
if exist "%NATIVE_LIB_DIR%" set PATH=%NATIVE_LIB_DIR%;%PATH%

if not exist "%RESULTS_DIR%" mkdir "%RESULTS_DIR%"

set PASS_COUNT=0
set FAIL_COUNT=0

REM Accumulate failed test names+reasons in a file (robust string handling in
REM batch); consumed for the end-of-run failed list and the CI step summary.
set FAILED_FILE=%RESULTS_DIR%\_failed_list.txt
if exist "%FAILED_FILE%" del "%FAILED_FILE%"

echo ========================================
echo   Objeck Regression Test Suite
echo   Platform: %PLATFORM%
echo ========================================
echo.

for %%f in (*.obs) do (
    echo Running: %%~nf...

    REM Build library list: base libs + any EXTRA_LIBS from test file
    set LIBS=cipher,collect,xml,json
    for /f "tokens=3*" %%a in ('findstr /c:"# EXTRA_LIBS:" "%REGRESSION_DIR%\%%f" 2^>nul') do set LIBS=!LIBS!,%%a

    REM Change to compiler directory so it can find ../lib
    pushd "%ABS_BIN_DIR%"
    "%ABS_COMPILER%" -src "%REGRESSION_DIR%\%%f" -lib !LIBS! -opt s3 -dest "%REGRESSION_DIR%\%%~nf.obe" > "%REGRESSION_DIR%\%RESULTS_DIR%\%%~nf_compile.log" 2>&1
    set COMPILE_RESULT=!errorlevel!
    popd

    REM Check for expected-compile-error tests
    set EXPECT_ERR=
    findstr /c:"# EXPECT_COMPILE_ERROR" "%REGRESSION_DIR%\%%f" >nul 2>&1
    if !errorlevel! equ 0 set EXPECT_ERR=1

    REM Check for expected-runtime-error tests
    set EXPECT_RT_ERR=
    findstr /c:"# EXPECT_RUNTIME_ERROR" "%REGRESSION_DIR%\%%f" >nul 2>&1
    if !errorlevel! equ 0 set EXPECT_RT_ERR=1

    if !COMPILE_RESULT! neq 0 (
        if defined EXPECT_ERR (
            REM Compiler rejected bad code as expected — verify it produced output
            for %%s in ("%REGRESSION_DIR%\%RESULTS_DIR%\%%~nf_compile.log") do set LOG_SIZE=%%~zs
            if !LOG_SIZE! gtr 0 (
                echo   PASS ^(compile error as expected^)
                set /a PASS_COUNT+=1
            ) else (
                echo   FAIL ^(compiler produced no output — possible crash^)
                >>"%FAILED_FILE%" echo %%~nf - compiler produced no output ^(possible crash^)
                set /a FAIL_COUNT+=1
            )
        ) else (
            echo   FAIL ^(compilation error^)
            >>"%FAILED_FILE%" echo %%~nf - compilation error
            set /a FAIL_COUNT+=1
        )
    ) else (
        if defined EXPECT_ERR (
            echo   FAIL ^(should have failed to compile^)
            >>"%FAILED_FILE%" echo %%~nf - should have failed to compile
            set /a FAIL_COUNT+=1
        ) else (
            REM Per-test opt-out for auto-JIT (mirrors the bash runner)
            set OBJECK_JIT_DISABLE=
            findstr /c:"# JIT_DISABLE" "%REGRESSION_DIR%\%%f" >nul 2>&1
            if !errorlevel! equ 0 set OBJECK_JIT_DISABLE=1

            REM Run from regression directory
            "%ABS_VM%" "%%~nf.obe" > "%RESULTS_DIR%\%%~nf_output.txt" 2>&1
            set RUN_RESULT=!errorlevel!

            if defined EXPECT_RT_ERR (
                REM Runtime error expected — PASS if VM exited non-zero with output
                for %%s in ("%REGRESSION_DIR%\%RESULTS_DIR%\%%~nf_output.txt") do set OUT_SIZE=%%~zs
                if !RUN_RESULT! neq 0 (
                    if !OUT_SIZE! gtr 0 (
                        echo   PASS ^(runtime error as expected^)
                        set /a PASS_COUNT+=1
                    ) else (
                        echo   FAIL ^(VM produced no output — possible crash^)
                        >>"%FAILED_FILE%" echo %%~nf - VM produced no output ^(possible crash^)
                        set /a FAIL_COUNT+=1
                    )
                ) else (
                    echo   FAIL ^(should have failed at runtime^)
                    >>"%FAILED_FILE%" echo %%~nf - should have failed at runtime
                    set /a FAIL_COUNT+=1
                )
            ) else (
                if !RUN_RESULT! neq 0 (
                    echo   FAIL ^(runtime error^)
                    >>"%FAILED_FILE%" echo %%~nf - runtime error ^(exit !RUN_RESULT!^)
                    set /a FAIL_COUNT+=1
                ) else (
                    echo   PASS
                    set /a PASS_COUNT+=1
                )
            )
        )
    )
)

echo.
echo ========================================
echo   Results: !PASS_COUNT! passed, !FAIL_COUNT! failed
echo ========================================

REM List exactly what failed so the reader never has to scroll the full log.
if !FAIL_COUNT! gtr 0 (
    echo.
    echo Failed tests:
    if exist "%FAILED_FILE%" for /f "usebackq delims=" %%l in ("%FAILED_FILE%") do echo   x %%l
)

REM Emit a GitHub Actions step summary when running in CI.
if defined GITHUB_STEP_SUMMARY (
    >>"%GITHUB_STEP_SUMMARY%" echo ### Regression - %PLATFORM%: !PASS_COUNT! passed, !FAIL_COUNT! failed
    >>"%GITHUB_STEP_SUMMARY%" echo.
    if !FAIL_COUNT! gtr 0 (
        >>"%GITHUB_STEP_SUMMARY%" echo ^| Result ^| Test - reason ^|
        >>"%GITHUB_STEP_SUMMARY%" echo ^| :----: ^| :------------ ^|
        if exist "%FAILED_FILE%" for /f "usebackq delims=" %%l in ("%FAILED_FILE%") do >>"%GITHUB_STEP_SUMMARY%" echo ^| FAIL ^| %%l ^|
    ) else (
        >>"%GITHUB_STEP_SUMMARY%" echo All regression tests passed
    )
    >>"%GITHUB_STEP_SUMMARY%" echo.
)

if !FAIL_COUNT! gtr 0 exit /b 1
exit /b 0
