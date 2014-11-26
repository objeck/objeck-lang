set ZIP_BIN="C:\Program Files\7-Zip"

cd deploy\bin
rmdir /s /q ..\html
mkdir ..\html
obc -src ..\examples\doc\doc_html.obs,..\examples\doc\doc_parser.obs -lib collect.obl -dest ..\..\code_doc.obe
obr ..\..\code_doc.obe ..\examples\doc\templates ..\..\..\compiler\lib_src\lang.obs ..\..\..\compiler\lib_src\collect.obs ..\..\..\compiler\lib_src\regex.obs ..\..\..\compiler\lib_src\json.obs ..\..\..\compiler\lib_src\xml.obs ..\..\..\compiler\lib_src\encrypt.obs
rmdir /s /q ..\doc\api
mkdir ..\doc\api
copy ..\..\..\compiler\rc\doc\templates\index.html ..\doc\api
xcopy /e ..\html\* ..\doc\api
rmdir /s /q ..\html
%ZIP_BIN%\7z.exe a -tzip api "..\doc\api\*.html"
copy api.zip ..\..\..\..\docs
cd ..\..
