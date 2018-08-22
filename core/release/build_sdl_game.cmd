@echo off

cd deploy\bin

obc -src ..\..\..\compiler\lib_src\sdl2.obs -lib collect.obl -tar lib -dest ..\lib\sdl2.obl
obc -src ..\..\..\compiler\lib_src\sdl_game.obs -lib collect.obl,sdl2.obl -tar lib -dest ..\lib\sdl_game.obl

set prgm_path=..\..\..\..\programs\sdl\engine\tests\test6
obc -src %prgm_path%\game.obs,%prgm_path%\blocks.obs,%prgm_path%\enemies.obs,%prgm_path%\player.obs,%prgm_path%\level.obs -lib csv.obl,collect.obl,sdl2.obl,sdl_game.obl -dest ..\..\..\game.obe

if [%1] EQU [brun] (
	rmdir /s /q images
	mkdir images
	xcopy /e ..\..\..\..\programs\sdl\engine\tests\media\images\*.png images\
	obr ..\..\..\game.obe
)
cd ..\..

