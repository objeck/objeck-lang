@echo off

cd deploy\bin
obc -src ..\..\..\compiler\lib_src\sdl.obs -lib collect.obl -tar lib -dest ..\lib\sdl.obl
obc -src ..\..\..\..\programs\sdl\%1.obs -lib collect.obl,sdl.obl -dest ..\..\%1.obe
devenv ..\..\..\lib\sdl\sdl\sdl.sln  /rebuild
copy /y ..\..\..\lib\sdl\sdl\Debug\libobjk_sdl.dll ..\lib\native
copy /y ..\..\..\lib\sdl\lib\x86\*.dll .
if %1.==. (
	echo Compile only.
) else (
	obr ..\..\%1.obe
)
cd ..\..