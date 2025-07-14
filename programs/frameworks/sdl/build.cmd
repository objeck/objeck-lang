@echo off
setlocal

if not [%1]==[x64] if not [%1]==[arm64] (
	echo Windows targets are: 'x64' and 'arm64'
	goto end
)

if [%1] == [x64] (
	set TARGET=deploy-x64
)

set ZIP_BIN="\Program Files\7-Zip"

if [%1] == [arm64] (
	set TARGET=deploy-arm64
)

del *.obe

set OBJECK_ROOT=C:\Users\objec\Documents\Code\objeck-lang
set OBJECK_LIB_SRC=%OBJECK_ROOT%\core\compiler\lib_src
set OBJECK_LIB_DST=%OBJECK_ROOT%\core\release\%TARGET%\lib
set OBJECK_SDL2_DEBUG_ROOT=%OBJECK_ROOT%\core\lib\sdl\sdl\Debug\x64

obc -src %OBJECK_LIB_SRC%\\sdl2.obs -lib collect.obl -tar lib -dest %OBJECK_LIB_DST%\sdl2.obl
obc -src %OBJECK_ROOT%\core\compiler\lib_src\openai.obs -lib json,cipher,net,net_server,misc -tar lib -dest %OBJECK_ROOT%\core\release\%TARGET%\lib\openai.obl
rem obc -src %OBJECK_LIB_SRC%\json.obs -tar lib -dest %OBJECK_LIB_DST%\json.obl
rem obc -src %OBJECK_LIB_SRC%\ml.obs -lib json,csv -tar lib -dest %OBJECK_LIB_DST%\ml.obl
rem obc -src %OBJECK_LIB_SRC%\net_common.obs,%OBJECK_LIB_SRC%\net.obs,%OBJECK_LIB_SRC%\net_secure.obs -lib json,cipher -tar lib -dest %OBJECK_LIB_DST%\net.obl
rem obc -src %OBJECK_LIB_SRC%\net_server.obs -lib net,json,cipher -tar lib -dest %OBJECK_LIB_DST%\net_server.obl

copy /y %OBJECK_SDL2_DEBUG_ROOT%\libobjk_sdl.dll %OBJECK_LIB_DST%\native\libobjk_sdl.dll

if [%2] == [] goto end
	obc -src %2 -lib sdl2,std_game
	obr %2 %3 %4 %5 %6
:end
