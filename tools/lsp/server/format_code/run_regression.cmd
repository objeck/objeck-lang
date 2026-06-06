@echo off
SETLOCAL

SET OBJECK_ROOT=..\..\..\objeck-lang

cd /d %~dp0

SET PATH=%PATH%;%OBJECK_ROOT%\core\release\deploy-x64\bin
SET OBJECK_LIB_PATH=%OBJECK_ROOT%\core\release\deploy-x64\lib

del /q formatter_regression.obe 2>nul

echo Building formatter regression test...
obc -src regression_test.obs,formatter.obs,scanner.obs -lib gen_collect -dest formatter_regression
if %ERRORLEVEL% NEQ 0 (
	echo Build failed
	goto end
)

echo Running formatter regression test...
echo ---
obr formatter_regression

:end
