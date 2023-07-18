@echo off

set ZIP_BIN="\Program Files\7-Zip"

cd deploy64\bin
rmdir /s /q ..\html 
mkdir ..\html
obc -src ..\..\..\lib\code_doc\doc_html.obs,..\..\..\lib\code_doc\doc_parser.obs -lib xml -dest ..\..\code_doc.obe
obr ..\..\code_doc.obe ..\..\..\lib\code_doc\templates 2023.7.3 ..\..\..\compiler\lib_src\json.obs ..\..\..\compiler\lib_src\xml.obs
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
cd ..\..
