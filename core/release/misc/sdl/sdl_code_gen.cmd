@echo off

cd deploy\bin
obc -src ..\..\..\compiler\lib_src\sdl.obs -lib collect.obl -tar lib -dest sdl.obl
obc -src ..\..\..\compiler\test_src\sdl\code_gen\sdl_scanner.obs,..\..\..\compiler\test_src\sdl\code_gen\sdl_parser.obs,..\..\..\compiler\test_src\sdl\code_gen\sdl_emitter.obs -opt s2 -lib collect.obl -dest ..\code_gen.obe
obr ..\code_gen.obe Event ..\..\..\lib\sdl\include\SDL_events.h
cd ..\..
