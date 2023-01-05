@echo off

obc -src ..\..\..\compiler\lib_src\web_server.obs -lib gen_collect,net -tar lib -dest ..\..\..\release\deploy64\lib\web_server.obl
obc -src ..\..\..\..\programs\frameworks\web\get_post.obs -lib gen_collect,net,web_server -tar web -dest get_post.obw
copy /y ..\..\..\..\programs\frameworks\web\in.html .