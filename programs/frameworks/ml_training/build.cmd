@echo off

set OBJECK_ROOT=..\..\..
SET OBJECK_LIB_SRC=%OBJECK_ROOT%\core\compiler\lib_src
SET OBJECK_LIB_DST=%OBJECK_ROOT%\core\release\deploy64\lib

REM obc -src %OBJECK_LIB_SRC%\net_common.obs,%OBJECK_LIB_SRC%\net.obs,%OBJECK_LIB_SRC%\net_secure.obs -lib json -tar lib -dest %OBJECK_LIB_DST%\net.obl
REM obc -src %OBJECK_LIB_SRC%\json.obs -tar lib -dest %OBJECK_LIB_DST%\json.obl
REM obc -src %OBJECK_ROOT%\core\compiler\lib_src\openai.obs -lib json,net,misc -tar lib -dest %OBJECK_ROOT%\core\release\deploy64\lib\openai.obl

if [%1] == [] goto end
	obc -src %1 -lib csv,net,json,misc
	obr %1 %2 %3 %4 %5
:end