@echo off

cd deploy\bin
rem obc -src ..\..\..\..\programs\sdl\code_gen\sdl_emitter.obs,..\..\..\..\programs\sdl\code_gen\sdl_parser.obs,..\..\..\..\programs\sdl\code_gen\sdl_scanner.obs -lib collect.obl -dest ..\..\sdl_code_gen.obe
rem obr ..\..\sdl_code_gen.obe %1 %2
obc -src ..\..\..\compiler\lib_src\sdl.obs -lib collect.obl -tar lib -dest ..\lib\sdl.obl
obc -src ..\..\..\..\programs\sdl\%1.obs -lib collect.obl,sdl.obl -dest ..\..\%1.obe
copy /y "D:\Code\objeck-lang\core\lib\sdl\sdl\Debug\libobjk_sdl.dll" ..\lib\native
copy /y ..\..\..\lib\sdl\lib\x86\*.dll .
REM obr ..\..\%1.obe
cd ..\..