@echo off

cd deploy\bin
set path=..\lib\sdl;%path%
obc -src ..\..\..\compiler\lib_src\sdl2.obs -lib collect.obl -tar lib -dest ..\lib\sdl2.obl
obc -src ..\..\..\compiler\lib_src\sdl_game.obs -lib collect.obl,sdl2.obl -tar lib -dest ..\lib\sdl_game.obl
obc -src ..\..\..\..\programs\sdl\engine\tests\test6\game.obs,..\..\..\..\programs\sdl\engine\tests\test6\level.obs,..\..\..\..\programs\sdl\engine\tests\test6\player.obs,..\..\..\..\programs\sdl\engine\tests\test6\platform.obs -lib collect.obl,sdl2.obl,sdl_game.obl -dest ..\..\..\game.obe

if [%1] EQU [brun] (
	rmdir /s /q images
	mkdir images
	xcopy /e ..\..\..\..\programs\sdl\engine\tests\media\images\*.png images\
	obr ..\..\..\game.obe
)
cd ..\..