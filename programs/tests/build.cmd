@echo off
setlocal

del /q *.obe

set OBJK_BASE=\Users\objec\Documents\Code\objeck-lang
set PATH=%PATH%:%OBJK_BASE%\core\release\deploy-x64\bin
set OBJECK_LIB_PATH=%OBJK_BASE%\core\release\deploy-x64\lib

rem obc -src %OBJK_BASE%\core\compiler\lib_src\cipher.obs -tar lib -dest %OBJK_BASE%\core\release\deploy-x64\lib\cipher.obl
obc -src %OBJK_BASE%\core\compiler\lib_src\net.obs,%OBJK_BASE%\core\compiler\lib_src\net_common.obs,%OBJK_BASE%\core\compiler\lib_src\net_secure.obs -tar lib -lib json,cipher -dest %OBJK_BASE%\core\release\deploy-x64\lib\net.obl

if [%1] == [] goto end
	obc -src %1 -lib net,json,cipher
	obr %1 %2
:end

