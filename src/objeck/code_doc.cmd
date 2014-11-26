cd deploy\bin
mkdir ..\html
copy ..\examples\doc\templates\index.html ..\html
obc -src ..\examples\doc\doc_html.obs,..\examples\doc\doc_parser.obs -lib collect.obl -dest ..\..\code_doc.obe
obr ..\..\code_doc.obe ..\examples\doc\templates ..\..\..\compiler\lib_src\lang.obs ..\..\..\compiler\lib_src\collect.obs ..\..\..\compiler\lib_src\regex.obs ..\..\..\compiler\lib_src\json.obs
rmdir /s /q ..\doc\api
mkdir ..\doc\api
xcopy /e ..\html\* ..\doc\api
rmdir /s /q ..\html
explorer ..\doc\api\index.html
cd ..\doc\
"C:\Program Files\7-Zip\7z.exe" a -tzip api "api\*.html"
copy api.zip ..\..\..\..\docs
cd ..\..
