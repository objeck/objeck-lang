@echo off

cd deploy\bin
rem obc -src ..\..\..\..\programs\sdl\code_gen\sdl_emitter.obs,..\..\..\..\programs\sdl\code_gen\sdl_parser.obs,..\..\..\..\programs\sdl\code_gen\sdl_scanner.obs -lib collect.obl -dest ..\..\sdl_code_gen.obe
rem obr ..\..\sdl_code_gen.obe %1 %2
obc -src ..\..\..\compiler\lib_src\sdl.obs -lib collect.obl -tar lib -dest ..\lib\sdl.obl
REM obc -src ..\..\..\..\programs\sdl\sdl_06.obs -lib collect.obl,sdl.obl -dest ..\..\sdl_06.obe
REM copy /y "D:\Code\objeck-lang\core\lib\sdl\sdl\Debug\libobjk_sdl.dll" ..\lib\native
REM copy /y ..\..\..\lib\sdl\lib\x86\*.dll .
REM obr ..\..\sdl_06.obe
cd ..\..