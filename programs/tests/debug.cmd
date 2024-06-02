@echo off

echo Building...
echo ---
obc -src ..\..\core\compiler\lib_src\json_stream.obs -tar lib -dest ..\..\core\release\deploy64\lib\json_stream.obl
obc -src %1 -lib json_stream,json,net

IF "%2" == "brun" (
	echo Testing...
	echo ---
	obr %1
)