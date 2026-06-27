@echo off
SETLOCAL

cd /d %~dp0

SET VERSION=%1
IF "%VERSION%"=="" SET VERSION=0.0.0

SET OBJECK_ROOT=..\..\..\..
SET LIB_SRC=%OBJECK_ROOT%\core\compiler\lib_src

SET PATH=%PATH%;%OBJECK_ROOT%\core\release\deploy-x64\bin
SET OBJECK_LIB_PATH=%OBJECK_ROOT%\core\release\deploy-x64\lib

del /q *.obe 2>nul

obc -src doc_json.obs,doc_parser.obs -lib gen_collect,xml,json,cipher -dest doc_json.obe
if %ERRORLEVEL% NEQ 0 (
	echo Failed: doc_json.obe
	exit /b 1
)

obr doc_json.obe templates "%VERSION%" ^
	%LIB_SRC%\lang.obs ^
	%LIB_SRC%\regex.obs ^
	%LIB_SRC%\json_stream.obs ^
	%LIB_SRC%\json.obs ^
	%LIB_SRC%\xml.obs ^
	%LIB_SRC%\cipher.obs ^
	%LIB_SRC%\odbc.obs ^
	%LIB_SRC%\csv.obs ^
	%LIB_SRC%\query.obs ^
	%LIB_SRC%\sdl2.obs ^
	%LIB_SRC%\sdl_game.obs ^
	%LIB_SRC%\gen_collect.obs ^
	%LIB_SRC%\net_common.obs ^
	%LIB_SRC%\net.obs ^
	%LIB_SRC%\net_secure.obs ^
	%LIB_SRC%\net_server.obs ^
	%LIB_SRC%\net_h2.obs ^
	%LIB_SRC%\net_quic.obs ^
	%LIB_SRC%\rss.obs ^
	%LIB_SRC%\misc.obs ^
	%LIB_SRC%\diags.obs ^
	%LIB_SRC%\ml_core.obs ^
	%LIB_SRC%\ml_linear.obs ^
	%LIB_SRC%\ml_tree.obs ^
	%LIB_SRC%\ml_bayes.obs ^
	%LIB_SRC%\ml_neighbors.obs ^
	%LIB_SRC%\ml_cluster.obs ^
	%LIB_SRC%\ml_data.obs ^
	%LIB_SRC%\ai_search.obs ^
	%LIB_SRC%\ai_game.obs ^
	%LIB_SRC%\ai_optimize.obs ^
	%LIB_SRC%\ai_rl.obs ^
	%LIB_SRC%\nlp.obs ^
	%LIB_SRC%\openai.obs ^
	%LIB_SRC%\gemini.obs ^
	%LIB_SRC%\ollama.obs ^
	%LIB_SRC%\opencv.obs ^
	%LIB_SRC%\onnx.obs ^
	%LIB_SRC%\json_rpc.obs ^
	%LIB_SRC%\lame.obs ^
	%LIB_SRC%\math.obs ^
	%LIB_SRC%\web_server.obs
if %ERRORLEVEL% NEQ 0 (
	echo Failed: doc_json generation
	exit /b 1
)

move /y out.json ..\objk_apis.json >nul
