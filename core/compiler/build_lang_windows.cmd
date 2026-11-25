@echo off
REM build_lang_windows.cmd [x64|arm64] [dest]
REM
REM Builds lang.obl on Windows, the way core/compiler/update_version.sh does on POSIX:
REM compile the _SYSTEM bootstrap compiler (sys_obc.exe, the Release-Sys configuration of
REM vs\compiler.vcxproj) and run it over lib_src\lang.obs. obc cannot compile anything
REM without lang.obl, so this one library cannot be built by the normal compiler.
REM
REM The result is byte-identical to the one the Linux build produces (CI compares them).
REM
REM   arch   x64 (default) or arm64
REM   dest   output file (default ..\lib\lang.obl, relative to core\compiler)
REM
REM Needs msbuild on PATH (a VS developer prompt, or microsoft/setup-msbuild in CI).
setlocal
set ARCH=%~1
if "%ARCH%"=="" set ARCH=x64
set DEST=%~2
if "%DEST%"=="" set DEST=..\lib\lang.obl

if /i "%ARCH%"=="x64" (
	set PLAT=x64
	set OUTSUB=win64
) else if /i "%ARCH%"=="arm64" (
	set PLAT=ARM64
	set OUTSUB=arm64
) else (
	echo usage: build_lang_windows.cmd [x64^|arm64] [dest]
	exit /b 2
)

where msbuild >nul 2>&1
if errorlevel 1 (
	echo ERROR: msbuild is not on PATH - run this from a Visual Studio developer prompt
	exit /b 1
)

pushd "%~dp0"
msbuild vs\compiler.vcxproj /p:Configuration=Release-Sys /p:Platform=%PLAT% /m /nologo /v:minimal
if errorlevel 1 (
	echo ERROR: building sys_obc.exe failed
	popd
	exit /b 1
)

set SYS_OBC=Release-Sys\%OUTSUB%\sys_obc.exe
if not exist "%SYS_OBC%" (
	echo ERROR: %SYS_OBC% was not produced
	popd
	exit /b 1
)

"%SYS_OBC%" -src lib_src/lang.obs -tar lib -opt s2 -dest %DEST% -strict
if errorlevel 1 (
	echo ERROR: sys_obc failed to compile lang.obs
	popd
	exit /b 1
)
popd
echo lang.obl written to %DEST%
exit /b 0
