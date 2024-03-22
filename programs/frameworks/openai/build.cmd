@echo off

set OBJECK_ROOT=C:\Users\objec\Documents\Code\objeck-lang
obc -src %OBJECK_ROOT%\core\compiler\lib_src\openai.obs -lib json,net -tar lib -dest %OBJECK_ROOT%\core\release\deploy64\lib\openai.obl

if [%1] == [] goto end
	obc -src %1 -lib net,json,openai -dest %1
	if [%2] == [] goto end
		obr %1 %2 %3 %4 %5
:end