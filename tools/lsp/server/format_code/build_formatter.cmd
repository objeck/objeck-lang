@echo off

del *.obe
obc -src test.obs,formatter.obs,scanner.obs -lib gen_collect -dest code_formatter

if "%~1" == "" goto end
	echo ---
	obr code_formatter %1
:end