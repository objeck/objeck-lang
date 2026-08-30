@echo off
call "C:\Program Files\Microsoft Visual Studio\2022\Professional\VC\Auxiliary\Build\vcvarsall.bat" amd64
if "%VCINSTALLDIR%"=="" (
    echo ERROR: Failed to set up Visual Studio environment
    exit /b 1
)
echo VCINSTALLDIR=%VCINSTALLDIR%
echo === Starting deploy_windows.cmd x64 ===
cd /d "C:\Users\objec\Documents\Code\objeck-lang\core\release"
echo Current directory: %CD%
call "C:\Users\objec\Documents\Code\objeck-lang\core\release\deploy_windows.cmd" x64
echo === Deploy complete with exit code %ERRORLEVEL% ===
