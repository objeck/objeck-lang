@echo off

cd deploy\bin
obc -src ..\..\..\compiler\lib_src\sdl.obs -lib collect.obl -tar lib -dest ..\lib\sdl.obl
obc -src ..\..\..\..\programs\sdl\engine\sdl_engine.obs -lib collect.obl,sdl.obl -tar lib -dest ..\lib\sdl_engine.obl
obc -src ..\..\..\..\programs\sdl\engine\tests\test%1\platform.obs,..\..\..\..\programs\sdl\engine\tests\test%1\player.obs,..\..\..\..\programs\sdl\engine\tests\test%1\enemy.obs,..\..\..\..\programs\sdl\engine\tests\test%1\bullet.obs -lib collect.obl,sdl.obl,sdl_engine.obl -dest ..\..\sdl_platform.obe

if NOT %2.==. (
	copy /y ..\..\..\lib\sdl\sdl\Debug\libobjk_sdl.dll ..\lib\native
	copy /y ..\..\..\lib\sdl\lib\x86\*.dll .
	rmdir /s /q images
	mkdir images
	xcopy /e ..\..\..\..\programs\sdl\engine\tests\media\images\*.png images\
	obr ..\..\sdl_platform.obe
)

cd ..\..