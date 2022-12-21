@echo off
setlocal

if "%~1" == "" goto ERROR 
	del /q %1.obe

REM	set PATH=%PATH%;..\..\..\core\release\deploy\bin;..\..\..\core\release\deploy\lib\sdl
REM	set OBJECK_LIB_PATH=..\..\..\core\release\deploy\lib

	copy /y "C:\Users\objec\Documents\Code\objeck-lang\core\lib\sdl\sdl\Debug\Win32\libobjk_sdl.dll" ..\..\..\core\release\deploy\lib\native

	obc -src ..\..\..\core\compiler\lib_src\sdl2.obs -lib collect.obl -tar lib -dest ..\..\..\core\release\deploy\lib\sdl2.obl

	obc -src %1\ship.obs,%1\anim_sprite.obs,%1\bg_texture.obs,%1\background.obs,%1\component.obs,%1\actor.obs,%1\game.obs,%1\sprite.obs -lib collect.obl,sdl2.obl,sdl_game.obl -dest %1.obe

	if "%~2" neq "brun" goto END 
		obr %1.obe
		goto END

:ERROR
	echo No code directory provided!

:END