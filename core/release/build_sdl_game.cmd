@echo off

cd deploy\bin
set path=%cd%\..\bin;%cd%\..\lib\sdl;%cd%\..\lib\sdl;;%path%
set OBJECK_LIB_PATH=%cd%\..\lib

obc -src ..\..\..\compiler\lib_src\sdl2.obs -lib collect.obl -tar lib -dest ..\lib\sdl2.obl
obc -src ..\..\..\compiler\lib_src\sdl_game.obs -lib collect.obl,sdl2.obl -tar lib -dest ..\lib\sdl_game.obl

set prev_dir=%cd%
cd ..\..\..\..\programs\sdl\engine\tests\test6
obc -src game.obs,blocks.obs,enemies.obs,player.obs,level.obs -lib csv.obl,collect.obl,sdl2.obl,sdl_game.obl -dest %prev_dir%\..\..\..\game.obe
if not errorlevel 0 goto on_fail

:on_fail
cd %prev_dir%

if [%1] EQU [brun] (
	rmdir /s /q images
	mkdir images
	xcopy /e ..\..\..\..\programs\sdl\engine\tests\media\images\*.png images\
	obr ..\..\..\game.obe
)
cd ..\..