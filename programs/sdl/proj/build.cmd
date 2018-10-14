@echo off
setlocal

if "%~1" == "" goto ERROR 
	del /q %1.obe

	set PATH=%PATH%;..\..\..\core\release\deploy\bin
	set OBJECK_LIB_PATH=..\..\..\core\release\deploy\lib

	obc -src %1\anim_sprite.obs,%1\bg_texture.obs,%1\background.obs,%1\component.obs,%1\actor.obs,%1\game.obs,%1\sprite.obs -lib collect.obl,sdl2.obl,sdl_game.obl -dest %1.obe

	if "%~2" neq "brun" goto END 
		obr %1.obe
		goto END

:ERROR
	echo No code directory provided!

:END