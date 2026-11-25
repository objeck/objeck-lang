@echo off
REM Build and run the MCP canvas demo (mcp_canvas.obs) on Windows.
REM
REM   .\run_mcp_canvas.cmd --demo                  two scripted painters, no keys, no network
REM   .\run_mcp_canvas.cmd                         live: Claude and an OpenAI model paint together
REM   .\run_mcp_canvas.cmd --selftest              check the tools, no window
REM   .\run_mcp_canvas.cmd --demo --shot out.png   no window: write out_mid.png and out.png
REM   .\run_mcp_canvas.cmd --tree <path> ...       use a specific deploy tree
REM
REM Live mode reads the keys from ANTHROPIC_API_KEY / OPENAI_API_KEY, or from
REM anthropic_api_key.dat / openai_api_key.dat in the CURRENT directory; a painter
REM without a key falls back to the scripted one. Other options: see mcp_canvas.obs.
REM
REM Note the leading .\ above: this machine class sets NoDefaultCurrentDirectoryInExePath.

setlocal enabledelayedexpansion

set HERE=%~dp0
set HERE=%HERE:~0,-1%
pushd "%HERE%\..\.."
set REPO=%CD%
popd

set TREE=
set ARGS=
:parse
if "%~1"=="" goto parsed
if /i "%~1"=="--tree" ( set TREE=%~2& shift& shift& goto parse )
set ARGS=!ARGS! %1
shift
goto parse
:parsed

if not defined TREE (
	for %%C in (deploy-x64 deploy-arm64 deploy) do (
		if not defined TREE (
			if exist "%REPO%\core\release\%%C\bin\obc.exe" set TREE=%REPO%\core\release\%%C
		)
	)
)
if not defined TREE (
	echo No Objeck deploy tree found under core\release; build one with deploy_windows.cmd x64, or pass --tree.
	exit /b 1
)

set OBE=%TEMP%\mcp_canvas.obe
"%TREE%\bin\obc.exe" -src "%HERE%\mcp_canvas.obs" -lib gen_collect,json,net,net_server,cipher,misc,mcp,anthropic,openai,sdl2 -dest "%OBE%"
if errorlevel 1 exit /b 1

set OBJECK_LIB_PATH=%TREE%\lib
"%TREE%\bin\obr.exe" "%OBE%" !ARGS!
