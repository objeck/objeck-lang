@echo off

if not [%1]==[deploy-x64] if not [%1]==[deploy-arm64] (
	echo Windows targets are: 'deploy-x64' and 'deploy-arm64'
	goto end
)

set TARGET=%1
set OBJECK_ROOT=C:\Users\objec\Documents\Code\objeck-lang
set OBJECK_LIB_SRC=%OBJECK_ROOT%\core\compiler\lib_src
set OBJECK_LIB_DST=%OBJECK_ROOT%\core\release\%TARGET%\lib

rem obc -src %OBJECK_LIB_SRC%\json.obs -lib gen_collect -tar lib -opt s3 -dest %OBJECK_LIB_DST%\json.obl
rem obc -src %OBJECK_LIB_SRC%\net_common.obs,%OBJECK_LIB_SRC%\net.obs,%OBJECK_LIB_SRC%\net_secure.obs -lib json -tar lib -dest %OBJECK_LIB_DST%\net.obl
rem obc -src %OBJECK_LIB_SRC%\json.obs -tar lib -dest %OBJECK_LIB_DST%\json.obl
rem obc -src %OBJECK_LIB_SRC%\ml.obs -lib json,csv -tar lib -dest %OBJECK_LIB_DST%\ml.obl
obc -src %OBJECK_ROOT%\core\compiler\lib_src\openai.obs -lib json,cipher,net,misc -tar lib -dest %OBJECK_ROOT%\core\release\%TARGET%\lib\openai.obl

if [%2] == [] goto end
	obc -src %2 -lib net,json,cipher,misc,openai
	obr %2 %3 %4 %5 %6
:end