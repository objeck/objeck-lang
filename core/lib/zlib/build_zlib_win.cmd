@echo off
REM ============================================================
REM  Rebuild libz-static.lib for Windows from the checked-in
REM  zlib sources using the CURRENT MSVC toolset.
REM
REM  Must be run inside a Visual Studio Developer environment:
REM     x64   : vcvarsall.bat amd64        then  build_zlib_win.cmd x64
REM     arm64 : vcvarsall.bat amd64_arm64  then  build_zlib_win.cmd arm64
REM
REM  Built WITHOUT /GL (no LTCG) so the archive contains plain
REM  COFF objects that link cleanly into any consumer regardless
REM  of the linker's LTCG version. Release CRT is /MT to match the
REM  Objeck library/VM projects (RuntimeLibrary=MultiThreaded).
REM ============================================================
setlocal EnableDelayedExpansion

set ARCH=%1
if "%ARCH%"=="" set ARCH=x64
if not "%ARCH%"=="x64" if not "%ARCH%"=="arm64" (
	echo Usage: build_zlib_win.cmd [x64^|arm64]
	exit /b 1
)

where cl >nul 2>&1
if errorlevel 1 (
	echo ERROR: cl.exe not on PATH - run inside a VS Developer Command Prompt
	echo   x64:   vcvarsall.bat amd64
	echo   arm64: vcvarsall.bat amd64_arm64
	exit /b 1
)

set SRC=%~dp0win\
set OUT=%~dp0win\%ARCH%
set OBJDIR=%TEMP%\zlib_obj_%ARCH%

if not exist "%OUT%" mkdir "%OUT%"
if exist "%OBJDIR%" rmdir /s /q "%OBJDIR%"
mkdir "%OBJDIR%"

set CFLAGS=/nologo /c /O2 /MT /DNDEBUG /D_CRT_SECURE_NO_WARNINGS /D_CRT_NONSTDC_NO_DEPRECATE

pushd "%OBJDIR%"
for %%f in (adler32 compress crc32 deflate gzclose gzlib gzread gzwrite infback inffast inflate inftrees trees uncompr zutil) do (
	cl %CFLAGS% "%SRC%%%f.c"
	if errorlevel 1 (
		echo ERROR: failed compiling %%f.c
		popd
		exit /b 1
	)
)

lib /nologo /out:"%OUT%\libz-static.lib" *.obj
if errorlevel 1 (
	echo ERROR: lib.exe archive step failed
	popd
	exit /b 1
)
popd

echo ZLIB_BUILD_OK %ARCH% -^> %OUT%\libz-static.lib
endlocal
