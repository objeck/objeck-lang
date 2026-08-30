@echo off
setlocal enabledelayedexpansion
set DEPLOY=C:\Users\objec\Documents\Code\objeck-lang\core\release\deploy\bin
set REG=C:\Users\objec\Documents\Code\objeck-lang\programs\regression

pushd "%DEPLOY%"
"%DEPLOY%\obc.exe" -src "%REG%\core_arithmetic.obs" -lib cipher,collect,xml,json -opt s3 -dest "%REG%\test_output.obe"
set CR=!errorlevel!
popd

echo COMPILE_RESULT=!CR!
if exist "%REG%\test_output.obe" (
    echo OBE_FILE=EXISTS
    "%DEPLOY%\obr.exe" "%REG%\test_output.obe"
    echo RUN_RESULT=!errorlevel!
) else (
    echo OBE_FILE=MISSING
)
