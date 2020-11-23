@echo off
obc -src ..\..\..\core\compiler\lib_src\sdl_game.obs -tar lib -lib sdl2,gen_collect,json -dest ..\..\..\core\release\deploy64\lib\sdl_game.obl
obc -src multiplication.obs -lib sdl_game,sdl2,gen_collect,json -dest multiplication.obe