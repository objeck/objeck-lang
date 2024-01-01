@echo off

set BASE_DIR=..\..\..

obc -src %BASE_DIR%\core\compiler\lib_src\ml.obs -tar lib -lib csv -dest %BASE_DIR%\core\lib\ml.obl
copy /y %BASE_DIR%\core\lib\ml.obl %BASE_DIR%\core\release\deploy64\lib\ml.obl

obc -src %BASE_DIR%\programs\tests\prgm258.obs -lib csv,ml

if [%1] NEQ [test] goto test
	obr %BASE_DIR%\programs\tests\prgm258.obe
:test