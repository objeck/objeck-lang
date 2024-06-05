@echo off

echo Building...
echo ---

del /q *.obe

obc -src ..\..\..\core\compiler\lib_src\ollama.obs -lib net,json,encrypt,misc -tar lib -dest ..\..\..\core\release\deploy64\lib\ollama.obl

if not [%1] == [] (
	obc -src %1 -lib ollama,json,net,encrypt
	
	if not [%2] == [] (
		echo Testing...
		echo ---
		
		obr "%1" %2
	)
)