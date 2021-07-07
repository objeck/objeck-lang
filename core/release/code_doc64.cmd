set ZIP_BIN="\Program Files\7-Zip"

cd deploy64\bin
rmdir /s /q ..\html 
mkdir ..\html
obc -src ..\examples\doc\doc_html.obs,..\examples\doc\doc_parser.obs -lib gen_collect.obl -dest ..\..\code_doc.obe
obr ..\..\code_doc.obe ..\examples\doc\templates 5.9.7 ..\..\..\compiler\lib_src\lang.obs ..\..\..\compiler\lib_src\regex.obs ..\..\..\compiler\lib_src\json.obs ..\..\..\compiler\lib_src\xml.obs ..\..\..\compiler\lib_src\encrypt.obs ..\..\..\compiler\lib_src\odbc.obs ..\..\..\compiler\lib_src\fcgi.obs ..\..\..\compiler\lib_src\odbc.obs ..\..\..\compiler\lib_src\csv.obs ..\..\..\compiler\lib_src\query.obs ..\..\..\compiler\lib_src\sdl2.obs ..\..\..\compiler\lib_src\sdl_game.obs ..\..\..\compiler\lib_src\gen_collect.obs ..\..\..\compiler\lib_src\net.obs ..\..\..\compiler\lib_src\rss.obs ..\..\..\compiler\lib_src\misc.obs
rmdir /s /q ..\doc\api
mkdir ..\doc\api
copy ..\..\..\..\programs\doc\templates\index.html ..\doc\api
xcopy /e ..\html\* ..\doc\api
rmdir /s /q ..\html
cd ..\doc
%ZIP_BIN%\7z.exe a -r -tzip api.zip api\*
move api.zip ..\..\..\..\docs
cd ..\..
