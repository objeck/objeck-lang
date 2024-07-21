@echo off

set OBJECK_ROOT=..\..\..\..\..
set OBJECK_LIB_SRC=%OBJECK_ROOT%\core\compiler\lib_src
set OBJECK_LIB_DST=%OBJECK_ROOT%\core\release\deploy64\lib

REM obc -src %OBJECK_LIB_SRC%\net_common.obs,%OBJECK_LIB_SRC%\net.obs,%OBJECK_LIB_SRC%\net_secure.obs -lib json -tar lib -dest %OBJECK_LIB_DST%\net.obl
REM obc -src %OBJECK_LIB_SRC%\openai.obs -lib json,net,misc -tar lib -dest %OBJECK_LIB_DST%\openai.obl

del /q /f *.obe

if [%1] == [] goto end
	echo %1
	obc -src %1 -lib openai,misc,json,net
	
	if [%2] == [] goto end
		obr %1 readme.json
		copy readme.html ..\..\..\..\docs\readme.html
:end