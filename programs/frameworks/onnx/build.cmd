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

set PATH=%PATH%;%OBJK_BIN_PATH%

set OBJK_BASE=..\..\..
set OBJK_BIN_PATH=%OBJK_BASE%\core\release\%TARGET%\bin
set OBJK_LIB_PATH=%OBJK_BASE%\core\release\%TARGET%\lib
set OBJK_DEBUG_PATH=%OBJK_BASE%\core\vm\%1\%TYPE%

REM
REM Clean
REM 

del /q %OBJK_LIB_PATH%\native\libobjk_onnx.dll
del /q %OBJK_DEBUG_PATH%\*.dll
del /q %OBJK_BIN_PATH%\*.dll

REM
REM Compile libraries
REM 
obc -src %OBJK_BASE%\core\compiler\lib_src\onnx.obs -tar lib -opt s3 -dest %OBJK_LIB_PATH%\onnx.obl
rem obc -src %OBJK_BASE%\core\compiler\lib_src\lame.obs -tar lib -opt s3 -dest %OBJK_LIB_PATH%\lame.obl

REM
REM ONNX libraries
REM 

copy /y %OBJK_BASE%\core\lib\onnx\eq\dml\%1\%TYPE%\libobjk_onnx.dll %OBJK_LIB_PATH%\native

copy /y %OBJK_BASE%\core\lib\onnx\eq\dml\packages\Microsoft.AI.DirectML.1.15.4\bin\%1-win\*.dll %OBJK_DEBUG_PATH%
copy /y %OBJK_BASE%\core\lib\onnx\eq\dml\packages\Microsoft.AI.DirectML.1.15.4\bin\%1-win\*.dll %OBJK_BIN_PATH%

REM
REM OpenCV libraries
REM 

copy /y %OBJK_BASE%\core\lib\onnx\win\%1\bin\*.dll %OBJK_DEBUG_PATH%
copy /y %OBJK_BASE%\core\lib\onnx\win\%1\bin\*.dll %OBJK_BIN_PATH%

REM
REM Test
REM 

if [%3] == [] goto end
	del /q *.obe
	obc -src %3 -lib onnx
	rem obr %3 %4
:end
