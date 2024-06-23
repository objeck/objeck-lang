@echo off

set OBJECK_ROOT=C:\Users\objec\Documents\Code\objeck-lang
SET OBJECK_LIB_SRC=%OBJECK_ROOT%\core\compiler\lib_src
SET OBJECK_LIB_DST=%OBJECK_ROOT%\core\release\deploy64\lib

obc -src %OBJECK_LIB_SRC%\net_common.obs,%OBJECK_LIB_SRC%\net.obs,%OBJECK_LIB_SRC%\net_secure.obs -lib json -tar lib -dest %OBJECK_LIB_DST%\net.obl
obc -src %OBJECK_LIB_SRC%\openai.obs -lib json,net,misc -tar lib -dest %OBJECK_LIB_DST%\openai.obl

del /q *.obe

if [%1] == [] goto end
	echo %1
	obc -src %1 -lib openai,misc,json,net
	obr %1 %2 %3 %4 %5
:end