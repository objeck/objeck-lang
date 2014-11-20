cd deploy\bin
rmdir /s /q ..\html
mkdir ..\html
copy ..\examples\doc\templates\index.html ..\html
obc -src ..\examples\doc\doc_html.obs,..\examples\doc\doc_parser.obs -lib collect.obl -dest ..\code_doc.obe
obr ..\code_doc.obe ..\examples\doc\templates ..\..\..\compiler\lib_src\lang.obs ..\..\..\compiler\lib_src\collect.obs ..\..\..\compiler\lib_src\regex.obs ..\..\..\compiler\lib_src\json.obs
explorer ..\html\index.html
cd ..\..
