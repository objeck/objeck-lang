@echo off
setlocal enabledelayedexpansion

set DEPLOY=C:\Users\objec\Documents\Code\objeck-lang\core\release\deploy
set REG=C:\Users\objec\Documents\Code\objeck-lang\programs\regression

REM Delete the .obe to force fresh compilation
del /f "%REG%\core_arithmetic.obe" 2>nul

REM Try the regression runner's exact approach
set DEPLOY_DIR=%DEPLOY%
set BIN_DIR=%DEPLOY_DIR%\bin
pushd "%BIN_DIR%"
set ABS_BIN_DIR=%CD%
set ABS_COMPILER=%CD%\obc.exe
popd

echo ABS_COMPILER=%ABS_COMPILER%
echo ABS_BIN_DIR=%ABS_BIN_DIR%

pushd "%ABS_BIN_DIR%"
"%ABS_COMPILER%" -src "%REG%\core_arithmetic.obs" -lib cipher,collect,xml,json -opt s3 -dest "%REG%\core_arithmetic.obe" > "%REG%\results\diag_compile.log" 2>&1
set COMPILE_RESULT=!errorlevel!
popd

echo COMPILE_RESULT=!COMPILE_RESULT!
if exist "%REG%\core_arithmetic.obe" (echo OBE=EXISTS) else (echo OBE=MISSING)
type "%REG%\results\diag_compile.log"

REM Also check deploy-x64
echo.
echo === Checking deploy-x64 ===
if exist "C:\Users\objec\Documents\Code\objeck-lang\core\release\deploy-x64\bin\obc.exe" (
    echo deploy-x64 compiler EXISTS
    dir "C:\Users\objec\Documents\Code\objeck-lang\core\release\deploy-x64\bin\obc.exe"
) else (
    echo deploy-x64 compiler MISSING
)
