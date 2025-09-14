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

if not [%3]==[dml] if not [%3]==[qnn] (
	echo EPs are: 'dml' and 'qnn'
	goto end
)

call build_opencv %1 %2 %3 %4

set TYPE=%2

set PATH=%PATH%;%OBJECK_BIN_PATH%

set OBJECK_BASE=..\..\..
set OBJECK_BIN_PATH=%OBJECK_BASE%\core\release\%TARGET%\bin
set OBJECK_LIB_PATH=%OBJECK_BASE%\core\release\%TARGET%\lib
set OBJECK_DEBUG_PATH=%OBJECK_BASE%\core\vm\%1\%TYPE%
set PATH=%PATH%;%OBJECK_BIN_PATH%

REM
REM Clean
REM 

del /q %OBJECK_LIB_PATH%\native\libobjk_onnx.dll
REM del /q %OBJECK_DEBUG_PATH%\*.dll
REM del /q %OBJECK_BIN_PATH%\*.dll

REM
REM Compile libraries
REM 
obc -src %OBJECK_BASE%\core\compiler\lib_src\onnx.obs -lib opencv -tar lib -opt s3 -dest %OBJECK_LIB_PATH%\onnx.obl
rem obc -src %OBJECK_BASE%\core\compiler\lib_src\lame.obs -tar lib -opt s3 -dest %OBJECK_LIB_PATH%\lame.obl

REM
REM ONNX libraries
REM 

if [%3]==[dml] (
	copy /y %OBJECK_BASE%\core\lib\onnx\eq\dml\%1\%TYPE%\libobjk_onnx.dll %OBJECK_LIB_PATH%\native

	copy /y %OBJECK_BASE%\core\lib\onnx\eq\dml\packages\Microsoft.AI.DirectML.1.15.4\bin\%1-win\*.dll %OBJECK_DEBUG_PATH%
	copy /y %OBJECK_BASE%\core\lib\onnx\eq\dml\packages\Microsoft.ML.OnnxRuntime.DirectML.1.22.1\runtimes\win-%1\native\*.dll %OBJECK_DEBUG_PATH%

	copy /y %OBJECK_BASE%\core\lib\onnx\eq\dml\packages\Microsoft.AI.DirectML.1.15.4\bin\%1-win\*.dll %OBJECK_BIN_PATH%
	copy /y %OBJECK_BASE%\core\lib\onnx\eq\dml\packages\Microsoft.ML.OnnxRuntime.DirectML.1.22.1\runtimes\win-%1\native\*.dll %OBJECK_BIN_PATH%
) else (
	copy /y %OBJECK_BASE%\core\lib\onnx\eq\qnn\%1\%TYPE%\libobjk_onnx.dll %OBJECK_LIB_PATH%\native

	copy /y %OBJECK_BASE%\core\lib\onnx\eq\qnn\win\onnx\%1\bin\*.dll %OBJECK_DEBUG_PATH%

	copy /y %OBJECK_BASE%\core\lib\onnx\eq\qnn\win\onnx\%1\bin\*.dll %OBJECK_BIN_PATH%
)

REM
REM Test
REM 

if [%4] == [] goto end
	del /q *.obe
	obc -src %4 -lib opencv,onnx -asm
	obr %4 %5
:end
