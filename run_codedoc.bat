@echo off
call "C:\Program Files\Microsoft Visual Studio\2022\Professional\VC\Auxiliary\Build\vcvarsall.bat" amd64

set BINDIR=C:\Users\objec\Documents\Code\objeck-lang\core\release\deploy-x64\bin
set RELDIR=C:\Users\objec\Documents\Code\objeck-lang\core\release

echo === Compiling code_doc with -opt s3 ===
cd /d "%BINDIR%"
"%BINDIR%\obc.exe" -src "%RELDIR%\..\lib\code_doc\doc_html.obs,%RELDIR%\..\lib\code_doc\doc_parser.obs" -lib cipher,json,xml,misc,net -opt s3 -dest "%RELDIR%\code_doc.obe"
echo Compile exit code: %ERRORLEVEL%

echo === Running code_doc ===
rmdir /s /q "%RELDIR%\deploy-x64\html" 2>nul
mkdir "%RELDIR%\deploy-x64\html"
cd /d "%BINDIR%"
"%BINDIR%\obr.exe" "%RELDIR%\code_doc.obe" "%RELDIR%\..\lib\code_doc\templates" 2026.6.0 "%RELDIR%\..\compiler\lib_src\lang.obs" "%RELDIR%\..\compiler\lib_src\regex.obs" "%RELDIR%\..\compiler\lib_src\json_stream.obs" "%RELDIR%\..\compiler\lib_src\json.obs" "%RELDIR%\..\compiler\lib_src\xml.obs" "%RELDIR%\..\compiler\lib_src\cipher.obs" "%RELDIR%\..\compiler\lib_src\odbc.obs" "%RELDIR%\..\compiler\lib_src\csv.obs" "%RELDIR%\..\compiler\lib_src\query.obs" "%RELDIR%\..\compiler\lib_src\sdl2.obs" "%RELDIR%\..\compiler\lib_src\sdl_game.obs" "%RELDIR%\..\compiler\lib_src\lame.obs" "%RELDIR%\..\compiler\lib_src\gen_collect.obs" "%RELDIR%\..\compiler\lib_src\net_common.obs" "%RELDIR%\..\compiler\lib_src\net.obs" "%RELDIR%\..\compiler\lib_src\net_secure.obs" "%RELDIR%\..\compiler\lib_src\net_server.obs" "%RELDIR%\..\compiler\lib_src\net_h2.obs" "%RELDIR%\..\compiler\lib_src\net_quic.obs" "%RELDIR%\..\compiler\lib_src\web_server.obs" "%RELDIR%\..\compiler\lib_src\rss.obs" "%RELDIR%\..\compiler\lib_src\misc.obs" "%RELDIR%\..\compiler\lib_src\diags.obs" "%RELDIR%\..\compiler\lib_src\ml_core.obs" "%RELDIR%\..\compiler\lib_src\ml_linear.obs" "%RELDIR%\..\compiler\lib_src\ml_tree.obs" "%RELDIR%\..\compiler\lib_src\ml_bayes.obs" "%RELDIR%\..\compiler\lib_src\ml_neighbors.obs" "%RELDIR%\..\compiler\lib_src\ml_cluster.obs" "%RELDIR%\..\compiler\lib_src\ml_data.obs" "%RELDIR%\..\compiler\lib_src\ai_search.obs" "%RELDIR%\..\compiler\lib_src\ai_game.obs" "%RELDIR%\..\compiler\lib_src\ai_optimize.obs" "%RELDIR%\..\compiler\lib_src\ai_rl.obs" "%RELDIR%\..\compiler\lib_src\openai.obs" "%RELDIR%\..\compiler\lib_src\gemini.obs" "%RELDIR%\..\compiler\lib_src\ollama.obs" "%RELDIR%\..\compiler\lib_src\json_rpc.obs" "%RELDIR%\..\compiler\lib_src\opencv.obs" "%RELDIR%\..\compiler\lib_src\onnx.obs" "%RELDIR%\..\compiler\lib_src\nlp.obs" "%RELDIR%\..\compiler\lib_src\math.obs"
echo Run exit code: %ERRORLEVEL%
