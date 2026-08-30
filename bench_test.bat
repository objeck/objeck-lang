@echo off
set DEPLOY=C:\Users\objec\Documents\Code\objeck-lang\core\release\deploy\bin
set SRC=C:\Users\objec\Documents\Code\objeck-lang\programs\tests\clbg
set OUT=C:\tmp

pushd "%DEPLOY%"

"%DEPLOY%\obc.exe" -src "%SRC%\binarytrees.obs" -opt s3 -dest "%OUT%\bt.obe" > "%OUT%\bt_compile.log" 2>&1
echo COMPILE_BT=%ERRORLEVEL%
if exist "%OUT%\bt.obe" (echo BT_OBE=YES) else (echo BT_OBE=NO)
type "%OUT%\bt_compile.log"

"%DEPLOY%\obc.exe" -src "%SRC%\nbody.obs" -opt s3 -dest "%OUT%\nb.obe" > "%OUT%\nb_compile.log" 2>&1
echo COMPILE_NB=%ERRORLEVEL%
if exist "%OUT%\nb.obe" (echo NB_OBE=YES) else (echo NB_OBE=NO)
type "%OUT%\nb_compile.log"

"%DEPLOY%\obc.exe" -src "%SRC%\spectralnorm.obs" -opt s3 -dest "%OUT%\sn.obe" > "%OUT%\sn_compile.log" 2>&1
echo COMPILE_SN=%ERRORLEVEL%
if exist "%OUT%\sn.obe" (echo SN_OBE=YES) else (echo SN_OBE=NO)

"%DEPLOY%\obc.exe" -src "%SRC%\fannkuchredux.obs" -opt s3 -dest "%OUT%\fk.obe" > "%OUT%\fk_compile.log" 2>&1
echo COMPILE_FK=%ERRORLEVEL%
if exist "%OUT%\fk.obe" (echo FK_OBE=YES) else (echo FK_OBE=NO)

popd

echo.
echo === Running benchmarks ===
echo.

if exist "%OUT%\bt.obe" (
  echo --- binarytrees 17 ---
  powershell -Command "Measure-Command { & '%DEPLOY%\obr.exe' '%OUT%\bt.obe' 17 | Out-Default } | ForEach-Object { Write-Host ('Time: ' + $_.TotalSeconds + 's') }"
  echo.
  echo --- binarytrees 21 ---
  powershell -Command "Measure-Command { & '%DEPLOY%\obr.exe' '%OUT%\bt.obe' 21 | Out-Default } | ForEach-Object { Write-Host ('Time: ' + $_.TotalSeconds + 's') }"
)

if exist "%OUT%\nb.obe" (
  echo.
  echo --- nbody 5000000 ---
  powershell -Command "Measure-Command { & '%DEPLOY%\obr.exe' '%OUT%\nb.obe' 5000000 | Out-Default } | ForEach-Object { Write-Host ('Time: ' + $_.TotalSeconds + 's') }"
)

if exist "%OUT%\sn.obe" (
  echo.
  echo --- spectralnorm 2000 ---
  powershell -Command "Measure-Command { & '%DEPLOY%\obr.exe' '%OUT%\sn.obe' 2000 | Out-Default } | ForEach-Object { Write-Host ('Time: ' + $_.TotalSeconds + 's') }"
)

if exist "%OUT%\fk.obe" (
  echo.
  echo --- fannkuchredux 11 ---
  powershell -Command "Measure-Command { & '%DEPLOY%\obr.exe' '%OUT%\fk.obe' 11 | Out-Default } | ForEach-Object { Write-Host ('Time: ' + $_.TotalSeconds + 's') }"
)
