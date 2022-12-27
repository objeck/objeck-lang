@echo off

obc -src ..\..\..\compiler\lib_src\web_server.obs -tar lib -dest ..\..\..\release\deploy64\lib\web_server.obl
obc -src ..\..\..\..\programs\web\hello.obs -lib web_server -tar web -dest hello.obw