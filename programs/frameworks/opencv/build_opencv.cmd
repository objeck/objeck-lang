@echo off
setlocal

if not [%1]==[x64] if not [%1]==[arm64] (
	echo Windows targets are: 'x64' and 'arm64'
	goto end
)

if [%1] == [x64] (
	set TARGET=deploy-x64
)

if [%1] == [arm64] (
	set TARGET=deploy-arm64
)

if not [%2]==[Debug] if not [%2]==[Release] (
	echo Builds are: 'Debug' and 'Release'
	goto end
)

set TYPE=%2

set ZIP_BIN="\Program Files\7-Zip"

set OBJK_BASE=\Users\objec\Documents\Code\objeck-lang
set PATH=%PATH%;%OBJK_BASE%\core\release\%TARGET%\bin
set OBJECK_LIB_PATH=%OBJK_BASE%\core\release\%TARGET%\lib

obc -src %OBJK_BASE%\core\compiler\lib_src\opencv.obs -tar lib -opt s3 -dest %OBJK_BASE%\core\release\%TARGET%\lib\opencv.obl
rem obc -src %OBJK_BASE%\core\compiler\lib_src\lame.obs -tar lib -opt s3 -dest %OBJK_BASE%\core\release\%TARGET%\lib\lame.obl

del /q ..\..\..\core\vm\%1\%TYPE%\*.dll
copy /y %OBJK_BASE%\core\lib\opencv\win\%1\bin\*.dll ..\..\..\core\vm\%1\%TYPE%

del /q %OBJK_BASE%\core\release\%TARGET%\bin\*.dll
copy /y %OBJK_BASE%\core\lib\opencv\win\%1\bin\*.dll %OBJK_BASE%\core\release\%TARGET%\bin

if [%3] == [] goto end
	del /q *.obe
	obc -src %3 -lib opencv
	rem obr %3 %4
:end
