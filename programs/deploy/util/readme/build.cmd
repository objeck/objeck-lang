@echo off

REM Builds the README that ships INSIDE the release archive, from readme.json
REM (hand-curated; see bump-version) through the readme.in.html template.
REM
REM Usage:  build.cmd readme_builder.obs        compile only
REM         build.cmd readme_builder.obs run    compile and generate
REM
REM Outputs readme.html / readme.md in this directory. It does NOT touch
REM docs/readme.html: that is the website changelog and is maintained by hand
REM (see .claude/skills/update-docs) with far more detail than readme.json
REM carries, so copying over it silently discarded the release notes.

setlocal

REM paths below are relative to this script, not to the caller's cwd
cd /d %~dp0

set OBJECK_ROOT=..\..\..\..

REM The release tree is deploy-x64/deploy-arm64 on some platforms and plain
REM deploy on others, so probe instead of hardcoding one (the old value,
REM deploy64, has not existed for some time and made this script a no-op).
set DEPLOY_NAME=
for %%C in (deploy-x64 deploy-arm64 deploy) do (
	if not defined DEPLOY_NAME (
		if exist "%OBJECK_ROOT%\core\release\%%C\bin" set DEPLOY_NAME=%%C
	)
)

if not defined DEPLOY_NAME (
	echo Failed: no deploy tree under %OBJECK_ROOT%\core\release
	echo   ^(looked for deploy-x64, deploy-arm64, deploy^)
	exit /b 1
)

set OBJECK_BIN_DST=%OBJECK_ROOT%\core\release\%DEPLOY_NAME%\bin
set OBJECK_LIB_DST=%OBJECK_ROOT%\core\release\%DEPLOY_NAME%\lib

set PATH=%OBJECK_BIN_DST%;%PATH%
set OBJECK_LIB_PATH=%OBJECK_LIB_DST%

del /q /f *.obe 2>nul

if [%1] == [] (
	echo Usage: build.cmd ^<builder.obs^> [run]
	exit /b 1
)

REM '@ml' is the dependency-closed alias for the LLM clients (core/lib/
REM configobjk.ini). The old explicit list -- openai,misc,json,net -- omitted
REM cipher, which openai.obl is built against, so this failed to resolve
REM 'Cipher.Encrypt'.
echo Compiling %1
obc -src %1 -lib @ml
if errorlevel 1 exit /b 1

if [%2] == [] goto end
	echo Generating readme.html / readme.md from readme.json
	obr %1 readme.json
	if errorlevel 1 exit /b 1
	echo Done. docs\readme.html is hand-maintained and was left untouched.
:end
