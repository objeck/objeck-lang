@echo off
setlocal

cd deploy\bin
set path=..\lib\sdl;%path%
obc -src ..\..\..\compiler\lib_src\sdl2.obs -lib collect.obl -tar lib -dest ..\lib\sdl2.obl
obc -src ..\..\..\compiler\lib_src\sdl_game.obs -lib collect.obl,sdl2.obl -tar lib -dest ..\lib\sdl_game.obl

copy ..\..\..\lib\sdl\sdl\Debug\Win32\libobjk_sdl.dll ..\..\deploy\lib\native