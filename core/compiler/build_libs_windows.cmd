@echo off
REM build_libs_windows.cmd [x64|arm64]
REM
REM Builds every standard library (core\lib\*.obl) on Windows, from source, in one go:
REM
REM   1. lang.obl      with the _SYSTEM bootstrap compiler (build_lang_windows.cmd)
REM   2. obc.exe       the Release configuration of vs\compiler.vcxproj
REM   3. everything else, the list in build_libs.sh, compiled by that obc
REM
REM The .obl are build products, not stored files; CI jobs get them from the `libs` job.
REM Run this on a fresh clone before deploy_windows.cmd. It needs msbuild on PATH (a Visual
REM Studio developer prompt) and `sh` (Git for Windows). arm64 must be run on an ARM64
REM machine, since the obc it builds is executed here.
setlocal
set ARCH=%~1
if "%ARCH%"=="" set ARCH=x64

if /i "%ARCH%"=="x64" (
	set PLAT=x64
) else if /i "%ARCH%"=="arm64" (
	set PLAT=ARM64
) else (
	echo usage: build_libs_windows.cmd [x64^|arm64]
	exit /b 2
)

where sh >nul 2>&1
if errorlevel 1 (
	echo ERROR: sh is not on PATH - install Git for Windows, or run core/compiler/update_version.sh in WSL
	exit /b 1
)

pushd "%~dp0"

call "%~dp0build_lang_windows.cmd" %ARCH%
if errorlevel 1 (
	popd
	exit /b 1
)

msbuild vs\compiler.vcxproj /p:Configuration=Release /p:Platform=%PLAT% /m /nologo /v:minimal
if errorlevel 1 (
	echo ERROR: building obc.exe failed
	popd
	exit /b 1
)

REM where MSBuild put the compiler, whatever the platform's output directory is
set OBC=
for /f "usebackq delims=" %%i in (`msbuild vs\compiler.vcxproj /p:Configuration^=Release /p:Platform^=%PLAT% -getProperty:TargetPath -nologo`) do set OBC=%%i
if "%OBC%"=="" (
	echo ERROR: could not find obc.exe
	popd
	exit /b 1
)

set OBJECK_LIB_PATH=%~dp0..\lib
sh ./build_libs.sh "%OBC%"
if errorlevel 1 (
	echo ERROR: a library failed to compile
	popd
	exit /b 1
)
popd
echo all libraries built into core\lib
exit /b 0
