@echo off

echo Building...
echo ---

del /q *.obe

obc -src ..\..\..\core\compiler\lib_src\ollama.obs -lib net,json,cipher,misc -tar lib -dest ..\..\..\core\release\deploy64\lib\ollama.obl
obc -src ..\..\..\core\compiler\lib_src\net_common.obs,..\..\..\core\compiler\lib_src\net.obs,..\..\..\core\compiler\lib_src\net_secure.obs -tar lib -lib json,gen_collect -opt s3 -dest ..\..\..\core\release\deploy64\lib\net.obl

if not [%1] == [] (
	obc -src %1 -lib ollama,json,net,cipher,misc
	
	if not [%2] == [] (
		echo Testing...
		echo ---
		
		obr "%1" %2 %3 %4
	)
)