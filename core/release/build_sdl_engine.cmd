@echo off

cd deploy\bin
obc -src ..\..\..\compiler\lib_src\sdl.obs -lib collect.obl -tar lib -dest ..\lib\sdl.obl
obc -src ..\..\..\..\programs\sdl\engine\sdl_engine.obs -lib collect.obl,sdl.obl -tar lib -dest ..\lib\sdl_engine.obl
if NOT %1.==. (
obc -src ..\..\..\..\programs\sdl\engine\tests\%1.obs -lib collect.obl,sdl.obl,sdl_engine.obl -dest ..\..\%1.obe
REM devenv ..\..\..\lib\sdl\sdl\sdl.sln  /rebuild
copy /y ..\..\..\lib\sdl\sdl\Debug\libobjk_sdl.dll ..\lib\native
copy /y ..\..\..\lib\sdl\lib\x86\*.dll .

rmdir /s /q images
mkdir images
xcopy /e ..\..\..\..\programs\sdl\engine\tests\media\images\*.png images\
obr ..\..\%1.obe
)
cd ..\..