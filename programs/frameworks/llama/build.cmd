@echo off

echo Building...
echo ---

obc -src ..\..\..\core\compiler\lib_src\json_stream.obs -tar lib -dest ..\..\..\core\release\deploy64\lib\json_stream.obl
obc -src ..\..\..\core\compiler\lib_src\net_common.obs,..\..\..\core\compiler\lib_src\net.obs,..\..\..\core\compiler\lib_src\net_secure.obs -lib json -tar lib -dest ..\..\..\core\release\deploy64\lib\net.obl

if not "%1" == "" (
	obc -src %1 -lib json_stream,json,net
	
	if "%2" == "brun" (
		echo Testing...
		echo ---
		
		obr "%1"
	)
)