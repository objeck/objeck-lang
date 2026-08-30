@echo off
call "C:\Program Files\Microsoft Visual Studio\2022\Professional\VC\Auxiliary\Build\vcvarsall.bat" amd64
echo === Running regression tests ===
cd /d "C:\Users\objec\Documents\Code\objeck-lang\programs\regression"
echo Current dir: %CD%
call "C:\Users\objec\Documents\Code\objeck-lang\programs\regression\run_regression.cmd" x64
echo === Regression exit code: %ERRORLEVEL% ===
echo.
echo === Running code_doc64 ===
cd /d "C:\Users\objec\Documents\Code\objeck-lang\core\release"
echo Current dir: %CD%
call "C:\Users\objec\Documents\Code\objeck-lang\core\release\code_doc64.cmd" x64 deploy
echo === code_doc64 exit code: %ERRORLEVEL% ===
