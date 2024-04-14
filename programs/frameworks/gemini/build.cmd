@echo off

set OBJECK_ROOT=C:\Users\objec\Documents\Code\objeck-lang
obc -src %OBJECK_ROOT%\core\compiler\lib_src\net_common.obs,%OBJECK_ROOT%\core\compiler\lib_src\net.obs,%OBJECK_ROOT%\core\compiler\lib_src\net_secure.obs -lib json -tar lib -dest %OBJECK_ROOT%\core\release\deploy64\lib\net.obl
obc -src %OBJECK_ROOT%\core\compiler\lib_src\gemini.obs -lib json,net,encrypt -tar lib -dest %OBJECK_ROOT%\core\release\deploy64\lib\gemini.obl

del /q *.obe

if [%1] == [] goto end
	obc -src %1 -lib misc,net,json,encrypt,gemini -dest %1
	obr %1 %2 %3 %4 %5
:end