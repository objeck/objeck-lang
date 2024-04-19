rem @echo off

set OBJECK_ROOT=C:\Users\objec\Documents\Code\objeck-lang
obc -src %OBJECK_ROOT%\core\compiler\lib_src\net_common.obs,%OBJECK_ROOT%\core\compiler\lib_src\net.obs,%OBJECK_ROOT%\core\compiler\lib_src\net_secure.obs -lib json -tar lib -dest %OBJECK_ROOT%\core\release\deploy64\lib\net.obl
obc -src %OBJECK_ROOT%\core\compiler\lib_src\json.obs -tar lib -dest %OBJECK_ROOT%\core\release\deploy64\lib\json.obl
obc -src %OBJECK_ROOT%\core\compiler\lib_src\ml.obs -lib json,csv -tar lib -dest %OBJECK_ROOT%\core\release\deploy64\lib\ml.obl
obc -src %OBJECK_ROOT%\core\compiler\lib_src\gemini.obs -lib misc,json,net,encrypt -tar lib -dest %OBJECK_ROOT%\core\release\deploy64\lib\gemini.obl

del /q *.obe

if [%1] == [] goto end
	obc -src %1 -lib csv,misc,ml,net,json,encrypt,gemini -dest %1
	obr %1 %2 %3 %4 %5
:end