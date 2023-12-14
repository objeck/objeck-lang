@echo off

set BASE_DIR=C:\Users\objec\Documents\Code\objeck-lang
set PATH=%PATH%;%BASE_DIR%\core\release\deploy64\bin
set OBJECK_LIB_PATH=%BASE_DIR%\core\release\deploy64\lib

obc -src %BASE_DIR%\core\compiler\lib_src\ml.obs -tar lib -lib csv -dest %BASE_DIR%\core\lib\ml.obl
copy /y %BASE_DIR%\core\lib\ml.obl %OBJECK_LIB_PATH%\ml.obl

pushd %BASE_DIR%\programs\tests
obc -src prgm263.obs -lib csv,ml,net,json & obr prgm263
popd

if [%1] NEQ [dll] goto end
	devenv matrix.sln /rebuild "Debug|x64"
	copy /y Debug\x64\libobjk_ml.dll %OBJECK_LIB_PATH%\native
:end