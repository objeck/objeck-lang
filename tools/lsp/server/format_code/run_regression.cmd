@echo off
SETLOCAL

cd /d %~dp0

REM format_code -> server -> lsp -> tools -> repo root
IF "%OBJECK_ROOT%"=="" SET OBJECK_ROOT=..\..\..\..

REM The release tree is named deploy-x64/deploy-arm64 on some platforms and
REM plain deploy on others, so probe instead of hardcoding one of them.
SET DEPLOY_NAME=
FOR %%C IN (deploy-x64 deploy-arm64 deploy) DO (
	IF NOT DEFINED DEPLOY_NAME (
		IF EXIST "%OBJECK_ROOT%\core\release\%%C\bin" SET DEPLOY_NAME=%%C
	)
)

IF NOT DEFINED DEPLOY_NAME (
	echo Build failed: no deploy tree under %OBJECK_ROOT%\core\release
	echo   ^(looked for deploy-x64, deploy-arm64, deploy^)
	EXIT /B 1
)

REM Resolve to an absolute path outside the loop, where %CD% expands correctly.
pushd "%OBJECK_ROOT%\core\release\%DEPLOY_NAME%"
SET DEPLOY_DIR=%CD%
popd

REM Call the toolchain by absolute path. Relying on PATH picks up a system-wide
REM Objeck install ahead of the build tree, which then fails against these
REM libraries with a tool chain version mismatch.
SET OBC=%DEPLOY_DIR%\bin\obc.exe
SET OBR=%DEPLOY_DIR%\bin\obr.exe
SET OBJECK_LIB_PATH=%DEPLOY_DIR%\lib
IF EXIST "%DEPLOY_DIR%\lib\native" SET PATH=%DEPLOY_DIR%\lib\native;%PATH%

del /q formatter_regression.obe 2>nul

echo Building formatter regression test...
"%OBC%" -src regression_test.obs,formatter.obs,scanner.obs -lib gen_collect -dest formatter_regression
if %ERRORLEVEL% NEQ 0 (
	echo Build failed
	EXIT /B 1
)

echo Running formatter regression test...
echo ---
"%OBR%" formatter_regression
EXIT /B %ERRORLEVEL%
