@echo off

echo Building...
echo ---

del /q *.obe

obc -src ..\..\..\core\compiler\lib_src\llama.obs -lib net,json,encrypt,misc -tar lib -dest ..\..\..\core\release\deploy64\lib\llama.obl

if not "%1" == "" (
	obc -src %1 -lib llama,json,net,encrypt
	
	if not "%2" == "" (
		echo Testing...
		echo ---
		
		obr "%1" %2
	)
)