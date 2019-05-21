@echo off

if "%~1"=="" goto blank
for %%i in (%1) do (
	set fn=%%~ni
	
	del /q %fn%.obe
	obc -src ..\..\..\core\compiler\lib_src\gen_collect.obs -tar lib -dest ..\..\..\core\release\deploy64\lib\gen_collect.obl

	echo ---
	obc -src %fn%.obs -lib \gen_collect.obl -dest %fn%.obe
	if "%~2"=="" goto end

	echo ---
	obr %fn%.obe
	goto end
)

:blank
echo missing args

:end
