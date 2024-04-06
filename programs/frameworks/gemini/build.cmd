@echo off

set OBJECK_ROOT=C:\Users\objec\Documents\Code\objeck-lang
obc -src %OBJECK_ROOT%\core\compiler\lib_src\gemini.obs -lib json,net,encrypt -tar lib -dest %OBJECK_ROOT%\core\release\deploy64\lib\gemini.obl

del /q *.obe

if [%1] == [] goto end
	obc -src %1 -lib net,json,encrypt,gemini -dest %1
	obr %1 %2 %3 %4 %5
:end