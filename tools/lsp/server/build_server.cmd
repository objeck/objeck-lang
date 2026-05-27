@echo off
SETLOCAL

SET PORT=%1
IF "%OBJECK_ROOT%"=="" SET OBJECK_ROOT=..\..\objeck-lang

cd /d %~dp0

SET PATH=%PATH%;%OBJECK_ROOT%\core\release\deploy-x64\bin
SET OBJECK_LIB_PATH=%OBJECK_ROOT%\core\release\deploy-x64\lib

del /q *.obe 2>nul
del /q %TEMP%\objk-* 2>nul

echo ---

obc -src %OBJECK_ROOT%\core\compiler\lib_src\diags.obs -lib gen_collect -tar lib -opt s3 -dest %OBJECK_ROOT%\core\lib\diags.obl
if %ERRORLEVEL% NEQ 0 (
	echo Build failed: diags.obl
	goto end
)
copy /y %OBJECK_ROOT%\core\lib\diags.obl %OBJECK_ROOT%\core\release\deploy-x64\lib\diags.obl

echo ---

obc -src frameworks.obs,proxy.obs,server.obs,format_code/scanner.obs,format_code/formatter.obs -lib diags,net,json,regex,cipher -dest objeck_lsp.obe
if %ERRORLEVEL% NEQ 0 (
	echo Build failed: objeck_lsp.obe
	goto end
)
copy /y objeck_lsp.obe ..\clients\vscode\server

echo ---
echo Build successful

if "%PORT%" == "" goto end
	echo Running on: %PORT%...
	obr objeck_lsp.obe objk_apis.json pipe debug
REM	obr objeck_lsp.obe objk_apis.json %PORT% debug
REM	obr objeck_lsp.obe objk_apis.json stdio debug
:end
