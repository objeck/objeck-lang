@echo off

cd deploy\bin
obc -src '..\..\..\compiler\lib_src\sdl.obs' -tar lib -dest sdl.obl
copy sdl.obl ..\..\..\compiler
copy ..\..\..\lib\sdl\lib\x86\sdl2.dll .
REM devenv /rebuild Debug ..\..\..\lib\sdl\sdl\sdl.sln
copy ..\..\..\lib\sdl\sdl\Debug\libobjk_sdl.dll ..\lib\objeck-lang
cd ..\..