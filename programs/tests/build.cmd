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

del /q *.obe

set OBJK_BASE=\Users\objec\Documents\Code\objeck-lang
set PATH=%PATH%;%OBJK_BASE%\core\release\%TARGET%\bin
set OBJECK_LIB_PATH=%OBJK_BASE%\core\release\%TARGET%\lib

obc -src %OBJK_BASE%\core\compiler\lib_src\gen_collect.obs -lib lang -tar lib -opt s3 -dest %OBJK_BASE%\core\release\%TARGET%\lib\gen_collect.obl -strict
rem obc -src %OBJK_BASE%\core\compiler\lib_src\cipher.obs -tar lib -dest %OBJK_BASE%\core\release\%TARGET%\lib\cipher.obl
rem obc -src %OBJK_BASE%\core\compiler\lib_src\net.obs,%OBJK_BASE%\core\compiler\lib_src\net_common.obs,%OBJK_BASE%\core\compiler\lib_src\net_secure.obs -tar lib -lib json,cipher -dest %OBJK_BASE%\core\release\%TARGET%\lib\net.obl
rem obc -src %OBJK_BASE%\core\compiler\lib_src\json_stream.obs -tar lib -dest %OBJK_BASE%\core\release\%TARGET%\lib\json_stream.obl

if [%2] == [] goto end
	obc -src %2 -lib json_stream
	obr %2 %3
:end

