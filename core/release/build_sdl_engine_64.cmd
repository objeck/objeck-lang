@echo off

cd deploy64\bin
set path=..\lib\sdl;%path%
obc -src ..\..\..\compiler\lib_src\sdl.obs -lib collect.obl -tar lib -dest ..\lib\sdl.obl
obc -src ..\..\..\compiler\lib_src\game.obs -lib collect.obl,sdl.obl -tar lib -dest ..\lib\game.obl
obc -src ..\..\..\..\programs\sdl\engine\tests\%1.obs -lib collect.obl,sdl.obl,game.obl -dest ..\..\%1.obe

if NOT %2.==. (
	rmdir /s /q images
	mkdir images
	xcopy /e ..\..\..\..\programs\sdl\engine\tests\media\images\*.png images\
	obr ..\..\%1.obe
)
cd ..\..