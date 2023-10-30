@echo off

REM if [%2] NEQ [data] goto data
REM 	obc data\gen
REM 	obr data\gen > data\test.csv
REM :data

obc -src ..\..\..\core\compiler\lib_src\ml.obs -tar lib -lib csv -dest ..\..\..\core\lib\ml.obl
copy /y ..\..\..\core\lib\ml.obl ..\..\..\core\release\deploy64\lib\ml.obl

obc -src dt3 -lib csv,ml

if [%1] NEQ [test] goto test
	obr dt3
:test