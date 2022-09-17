@echo off

setlocal 
pushd ..\..\release\deploy64
set OBJECK_PATH=%cd%
set PATH=%PATH%;%OBJECK_PATH%\bin
set OBJECK_LIB_PATH=%OBJECK_PATH%\lib
popd

pushd ..\..\compiler
obc -src lib_src\sdl2.obs -tar lib -dest ..\release\deploy64\lib\sdl2.obl
popd

devenv sdl\sdl.sln /rebuild "Release|x64"
copy /y sdl\Release\x64\libobjk_sdl.dll ..\..\release\deploy64\lib\native\libobjk_sdl.dll