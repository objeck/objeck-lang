if [%1]==[] (
	echo Windows targets are: 'x64' and 'arm64'
	goto end
)

set ZIP_BIN="\Program Files\7-Zip"

if [%1] == [arm64] (
	set TARGET="deploy-arm64"
)

if [%1] == [x64] (
	set TARGET="deploy-x64"
)

cd %TARGET%\\bin
rmdir /s /q ..\html 
mkdir ..\html
obc -src ..\..\..\lib\code_doc\doc_html.obs,..\..\..\lib\code_doc\doc_parser.obs -lib xml -dest ..\..\code_doc.obe
REM goto end

obr ..\..\code_doc.obe ..\..\..\lib\code_doc\templates 2025.5.0 ..\..\..\compiler\lib_src\regex.obs

rmdir /s /q ..\doc\api
mkdir ..\doc\api
copy ..\..\..\lib\code_doc\templates\index.html ..\doc\api
xcopy /e ..\html\* ..\doc\api
mkdir ..\doc\api\resources
copy ..\..\..\lib\code_doc\templates\resources\* ..\doc\api\resources
rmdir /s /q ..\html
cd ..\doc
%ZIP_BIN%\7z.exe a -r -tzip api.zip api\*
move api.zip ..\..\..\..\docs

:end
cd ..\..
