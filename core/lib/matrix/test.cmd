@echo off

set BASE_DIR=..\..\..
set PATH=%PATH%;%BASE_DIR%\core\release\deploy64\bin
set OBJECK_LIB_PATH=%BASE_DIR%\core\release\deploy64\lib

devenv matrix.sln /rebuild "Debug|x64"
copy /y Debug\x64\libobjk_ml.dll %OBJECK_LIB_PATH%\native

obc -src %BASE_DIR%\core\compiler\lib_src\ml.obs -tar lib -lib csv -dest %BASE_DIR%\core\lib\ml.obl
copy /y %BASE_DIR%\core\lib\ml.obl %OBJECK_LIB_PATH%\ml.obl

obc -src test.obs -lib csv,ml -asm

if [%1] NEQ [test] goto test
	obr lr3.obe
:test