@echo off
setlocal

if not [%1]==[x64] if not [%1]==[arm64] (
	echo Windows targets are: 'x64' and 'arm64'
	goto end
)

if not [%2]==[dml] if not [%2]==[qnn] (
	echo Windows providers are: 'dml' and 'qnn'
	goto end
)

if [%1] == [x64] (
	set TARGET=deploy-x64
)

set ZIP_BIN="\Program Files\7-Zip"

if [%1] == [arm64] (
	set TARGET=deploy-arm64
)

set TYPE=Release

set OBJK_BASE=\Users\objec\Documents\Code\objeck-lang
set PATH=%PATH%;%OBJK_BASE%\core\release\%TARGET%\bin
set OBJECK_LIB_PATH=%OBJK_BASE%\core\release\%TARGET%\lib

obc -src %OBJK_BASE%\core\compiler\lib_src\onnx.obs -tar lib -opt s3 -dest %OBJK_BASE%\core\release\%TARGET%\lib\onnx.obl
rem obc -src %OBJK_BASE%\core\compiler\lib_src\lame.obs -tar lib -opt s3 -dest %OBJK_BASE%\core\release\%TARGET%\lib\lame.obl
rem obc -src %OBJK_BASE%\core\compiler\lib_src\gen_collect.obs -lib lang -tar lib -opt s3 -dest %OBJK_BASE%\core\release\%TARGET%\lib\gen_collect.obl -strict
rem obc -src %OBJK_BASE%\core\compiler\lib_src\cipher.obs -tar lib -dest %OBJK_BASE%\core\release\%TARGET%\lib\cipher.obl
rem obc -src %OBJK_BASE%\core\compiler\lib_src\net.obs,%OBJK_BASE%\core\compiler\lib_src\net_common.obs,%OBJK_BASE%\core\compiler\lib_src\net_secure.obs -tar lib -lib json,cipher -dest %OBJK_BASE%\core\release\%TARGET%\lib\net.obl
rem obc -src %OBJK_BASE%\core\compiler\lib_src\json_stream.obs -tar lib -dest %OBJK_BASE%\core\release\%TARGET%\lib\json_stream.obl

del /q %OBJECK_LIB_PATH%\native\libobjk_onnx.dll
copy /y %OBJK_BASE%\core\lib\onnx\qnn\%1\%TYPE%\libobjk_onnx.dll %OBJECK_LIB_PATH%\native

del /q ..\..\core\vm\%1\%TYPE%\*.dll
copy /y %OBJK_BASE%\core\lib\onnx\win\opencv\%1\bin\*.dll ..\..\core\vm\%1\%TYPE%
copy /y %OBJK_BASE%\core\lib\onnx\win\opencv\%1\bin\*.dll %OBJK_BASE%\core\release\%TARGET%\bin

if [%2] == [dml] (
	copy /y %OBJK_BASE%\core\lib\onnx\dml\packages\Microsoft.ML.OnnxRuntime.DirectML.1.22.1\runtimes\win-%1\native\*.dll ..\..\core\vm\%1\%TYPE%

	del /q %OBJK_BASE%\core\release\%TARGET%\bin\*.dll
	copy /y %OBJK_BASE%\core\lib\onnx\dml\packages\Microsoft.ML.OnnxRuntime.DirectML.1.22.1\runtimes\win-%1\native\*.dll %OBJK_BASE%\core\release\%TARGET%\bin
)

if [%2] == [qnn] (
	copy /y %OBJK_BASE%\core\lib\onnx\qnn\win\onnx\%1\bin\*.dll ..\..\core\vm\%1\%TYPE%

	del /q %OBJK_BASE%\core\release\%TARGET%\bin\*.dll
	copy /y %OBJK_BASE%\core\lib\onnx\qnn\win\onnx\%1\bin\*.dll %OBJK_BASE%\core\release\%TARGET%\bin
)

if [%3] == [] goto end
	del /q *.obe
	obc -src %3 -lib onnx
	rem	obr %3 %4
:end
