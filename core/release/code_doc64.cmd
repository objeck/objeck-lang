set ZIP_BIN="\Program Files\7-Zip"

cd deploy64\bin
rmdir /s /q ..\html 
mkdir ..\html
obc -src ..\..\..\lib\code_doc\doc_html.obs,..\..\..\lib\code_doc\doc_parser.obs -lib xml -dest ..\..\code_doc.obe
obr ..\..\code_doc.obe ..\..\..\lib\code_doc\templates 2025.1.1 ..\..\..\compiler\lib_src\lang.obs ..\..\..\compiler\lib_src\regex.obs ..\..\..\compiler\lib_src\json_stream.obs ..\..\..\compiler\lib_src\json.obs ..\..\..\compiler\lib_src\xml.obs ..\..\..\compiler\lib_src\cipher.obs ..\..\..\compiler\lib_src\odbc.obs ..\..\..\compiler\lib_src\odbc.obs ..\..\..\compiler\lib_src\csv.obs ..\..\..\compiler\lib_src\query.obs ..\..\..\compiler\lib_src\sdl2.obs ..\..\..\compiler\lib_src\sdl_game.obs ..\..\..\compiler\lib_src\gen_collect.obs ..\..\..\compiler\lib_src\net_common.obs ..\..\..\compiler\lib_src\net.obs ..\..\..\compiler\lib_src\net_secure.obs ..\..\..\compiler\lib_src\rss.obs ..\..\..\compiler\lib_src\misc.obs ..\..\..\compiler\lib_src\diags.obs ..\..\..\compiler\lib_src\ml.obs ..\..\..\compiler\lib_src\openai.obs ..\..\..\compiler\lib_src\gemini.obs ..\..\..\compiler\lib_src\ollama.obs ..\..\..\compiler\lib_src\json_rpc.obs
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
