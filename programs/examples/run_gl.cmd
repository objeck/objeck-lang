@echo off
REM Build and run an Objeck OpenGL example on Windows.
REM
REM   .\run_gl.cmd                    the 3D walkthrough
REM   .\run_gl.cmd cube_gl            any example in this directory
REM   .\run_gl.cmd --verify           run the GL self-test instead, and report
REM   .\run_gl.cmd --tree <path>      use a specific deploy tree
REM
REM The Windows half of run_gl.sh, which is POSIX-only throughout -- uname
REM switches, /tmp paths, LD_LIBRARY_PATH -- and does not degrade onto cmd.
REM
REM Exists for the same reason: running a GL demo by hand means knowing which
REM deploy tree to use, whether its toolchain matches the source, and which -lib
REM flags the example needs. Each has cost real debugging time.
REM
REM Note the leading .\ above. This machine class sets
REM NoDefaultCurrentDirectoryInExePath, so a bare run_gl.cmd is not found.

setlocal enabledelayedexpansion

set HERE=%~dp0
set HERE=%HERE:~0,-1%
pushd "%HERE%\..\.."
set REPO=%CD%
popd

set DEMO=gl_walkthrough
set VERIFY=0
set TREE=

:parse
if "%~1"=="" goto parsed
if /i "%~1"=="--verify" ( set VERIFY=1& shift& goto parse )
if /i "%~1"=="--tree" ( set TREE=%~2& shift& shift& goto parse )
if /i "%~1"=="-h" goto usage
if /i "%~1"=="--help" goto usage
echo %~1 | findstr /b /c:"-" >nul && ( echo unknown option: %~1& exit /b 2 )
set DEMO=%~1
shift
goto parse

:usage
REM Print the header rather than a second copy of it, so the two cannot drift.
REM Stop at the first line that is not a REM, the way run_gl.sh does. Without
REM that, this also printed every comment further down the file -- the ones
REM that explain the code rather than the usage.
set SHOWN=
for /f "tokens=1,* delims=:" %%a in ('findstr /n "^" "%~f0"') do (
	if not defined SHOWN (
		if %%a GTR 1 (
			set LINE=%%b
			if /i "!LINE:~0,3!"=="REM" ( echo(!LINE:~4! ) else ( set SHOWN=1 )
		)
	)
)
exit /b 0

:parsed

REM ---------------------------------------------------------- deploy tree
if not defined TREE (
	REM Native arch first. Hardcoding x64 ahead of arm64 picks the emulated
	REM tree on an ARM64 machine that happens to have both.
	set CANDIDATES=deploy-x64 deploy-arm64 deploy
	if /i "%PROCESSOR_ARCHITECTURE%"=="ARM64" set CANDIDATES=deploy-arm64 deploy-x64 deploy
	for %%C in (!CANDIDATES!) do (
		if not defined TREE (
			if exist "%REPO%\core\release\%%C\bin\obc.exe" set TREE=%REPO%\core\release\%%C
		)
	)
)

if not defined TREE goto no_tree
if not exist "%TREE%\bin\obc.exe" goto no_tree
goto have_tree

:no_tree
echo No Objeck deploy tree with a Windows obc found under core\release.
echo.
echo Build one first, from a Visual Studio Developer Command Prompt:
echo   cd core\release
echo   deploy_windows.cmd x64
exit /b 1

:have_tree
set OBC=%TREE%\bin\obc.exe
set OBR=%TREE%\bin\obr.exe

REM A stale tree is the most confusing failure here: obc and the .obl each carry
REM a version stamp, and a mismatch surfaces as "compiled with an incompatible
REM version of the tool chain" -- which reads like a corrupt program rather than
REM an out-of-date build. Catch it up front.
set SRC_VER=
for /f "tokens=3" %%v in ('findstr /c:"#define VERSION_STRING" "%REPO%\core\shared\version.h"') do set SRC_VER=%%v
if defined SRC_VER (
	set SRC_VER=!SRC_VER:L"=!
	set SRC_VER=!SRC_VER:"=!
)
set TREE_VER=
for /f "tokens=1" %%v in ('"%OBC%" -version 2^>nul') do (
	if not defined TREE_VER set TREE_VER=%%v
)
if defined SRC_VER if defined TREE_VER if not "!SRC_VER!"=="!TREE_VER!" (
	echo Deploy tree is stale: %TREE%
	echo   its obc says !TREE_VER!, but core\shared\version.h says !SRC_VER!
	echo.
	echo Rebuild the tree, or pass --tree ^<path^> to point at a current one.
	exit /b 1
)

REM ---------------------------------------------------------- prerequisites
if not exist "%TREE%\lib\native\libobjk_sdl.dll" (
	echo Missing %TREE%\lib\native\libobjk_sdl.dll
	echo.
	REM Match the tree in use, and note the build output sits under the
	REM SOLUTION directory (core\lib\sdl\sdl), not one level further down.
	set SDL_ARCH=x64
	echo %TREE% | findstr /i "arm64" >nul && set SDL_ARCH=ARM64
	echo Build the SDL native library:
	echo   msbuild core\lib\sdl\sdl\sdl.sln -p:Configuration=Release -p:Platform=!SDL_ARCH!
	echo   copy core\lib\sdl\sdl\Release\!SDL_ARCH!\libobjk_sdl.dll "%TREE%\lib\native"
	exit /b 1
)

REM SDL2's own DLLs live in bin beside obr.exe -- the executable's directory is
REM the first place Windows looks when resolving a loaded DLL's imports, and
REM lib\native is never searched. Nothing to add to PATH; if these are missing
REM the tree predates that layout and wants rebuilding.
if not exist "%TREE%\bin\SDL2.dll" (
	echo Missing %TREE%\bin\SDL2.dll -- this deploy tree predates SDL2 moving to bin.
	echo Rebuild it with deploy_windows.cmd, or copy lib\sdl\*.dll into bin.
	exit /b 1
)

for %%L in (sdl2.obl sdl_gl.obl gen_collect.obl) do (
	if not exist "%TREE%\lib\%%L" (
		echo Missing %TREE%\lib\%%L -- rebuild the Objeck libraries.
		exit /b 1
	)
)

set OBJECK_LIB_PATH=%TREE%\lib

REM ---------------------------------------------------------- run
pushd "%TREE%\bin"

if "%VERIFY%"=="1" (
	echo Building the OpenGL self-test...
	"%OBC%" -src "%REPO%\programs\regression\gl_context_test.obs" -lib cipher,collect,xml,json,sdl2,sdl_gl -dest "%TEMP%\objeck_gl_test.obe" >nul
	if errorlevel 1 ( popd& echo Build failed.& exit /b 1 )
	echo.
	REM OBJECK_GL_REQUIRED turns "skip when there is no GL" into "fail when there
	REM is no GL", which is what you want when testing deliberately.
	set OBJECK_GL_REQUIRED=1
	"%OBR%" "%TEMP%\objeck_gl_test.obe"
	set RC=!errorlevel!
	popd
	exit /b !RC!
)

set SRC=%HERE%\%DEMO%.obs
if not exist "%SRC%" (
	popd
	echo No such example: %SRC%
	echo Available:
	for %%F in ("%HERE%\gl_*.obs" "%HERE%\cube_gl.obs") do echo   %%~nF
	exit /b 1
)

REM Assets a demo needs. We run from the deploy tree's bin directory, because
REM the demos reach for fonts at ..\lib\sdl\fonts -- so a demo that also loads a
REM file from its own source directory cannot find it by a relative path and has
REM to be told where it is. gl_model was silently failing this way.
set DEMO_ARGS=
if /i "%DEMO%"=="gl_model" set DEMO_ARGS="%HERE%\gl_crystal.obj"

echo Building %DEMO%...
"%OBC%" -src "%SRC%" -lib sdl2,sdl_gl -dest "%TEMP%\objeck_%DEMO%.obe" >nul
if errorlevel 1 ( popd& echo Build failed.& exit /b 1 )

echo Running %DEMO% -- escape to quit.
"%OBR%" "%TEMP%\objeck_%DEMO%.obe" %DEMO_ARGS%
set RC=!errorlevel!
popd
exit /b !RC!
