@echo off

echo Building...
echo ---
obc -src ..\..\core\compiler\lib_src\json.obs -tar lib -dest ..\..\core\release\deploy64\lib\json.obl
obc -src prgm261 -lib json

IF not "%1" == "" (
	echo Testing...
	echo ---
	obr prgm261 "%1"
)
:end