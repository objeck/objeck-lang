if [%1]==[] (
	echo Windows targets are: 'x64' and 'arm64'
	exit /b
)

set ZIP_BIN="\Program Files\7-Zip"

if [%1] == [arm64] (
	set TARGET="deploy-arm64"
)

if [%1] == [x64] (
	set TARGET="deploy-x64"
)

cd %TARGET%\bin
rmdir /s /q ..\html 
mkdir ..\html
obc -src ..\..\..\lib\code_doc\doc_html.obs,..\..\..\lib\code_doc\doc_parser.obs -lib cipher,json,xml,misc,net -dest ..\..\code_doc.obe

if [%2] NEQ [deploy] goto end
obr ..\..\code_doc.obe ..\..\..\lib\code_doc\templates 2025.6.0 ..\..\..\compiler\lib_src\lang.obs ..\..\..\compiler\lib_src\regex.obs ..\..\..\compiler\lib_src\json_stream.obs ..\..\..\compiler\lib_src\json.obs ..\..\..\compiler\lib_src\xml.obs ..\..\..\compiler\lib_src\cipher.obs ..\..\..\compiler\lib_src\odbc.obs ..\..\..\compiler\lib_src\odbc.obs ..\..\..\compiler\lib_src\csv.obs ..\..\..\compiler\lib_src\query.obs ..\..\..\compiler\lib_src\sdl2.obs ..\..\..\compiler\lib_src\sdl_game.obs ..\..\..\compiler\lib_src\gen_collect.obs ..\..\..\compiler\lib_src\net_common.obs ..\..\..\compiler\lib_src\net.obs ..\..\..\compiler\lib_src\net_secure.obs ..\..\..\compiler\lib_src\rss.obs ..\..\..\compiler\lib_src\misc.obs ..\..\..\compiler\lib_src\diags.obs ..\..\..\compiler\lib_src\ml.obs ..\..\..\compiler\lib_src\openai.obs ..\..\..\compiler\lib_src\gemini.obs ..\..\..\compiler\lib_src\ollama.obs ..\..\..\compiler\lib_src\json_rpc.obs
	rmdir /s /q ..\doc\api
	mkdir ..\doc\api
	xcopy /e ..\html\* ..\doc\api
	mkdir ..\doc\api\style
	copy ..\..\..\lib\code_doc\templates\resources\* ..\doc\api\style
	rmdir /s /q ..\html
	cd ..\doc
	%ZIP_BIN%\7z.exe a -r -tzip api.zip api\*
	move api.zip ..\..\..\..\docs
:end

cd ..\..
