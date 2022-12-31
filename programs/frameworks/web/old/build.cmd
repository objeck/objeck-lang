@echo off
set WSCP="\Program Files (x86)\WinSCP\WinSCP.exe"
obc -src ..\..\core\compiler\lib_src\fcgi.obs -lib gen_collect,net,misc -tar lib -opt s3 -dest ..\..\core\lib\fcgi.obl
obc -src ..\..\core\compiler\lib_src\fcgi_web.obs -lib fcgi,gen_collect,net,misc -tar lib -opt s3 -dest ..\..\core\lib\fcgi_web.obl
copy /y ..\..\core\lib\fcgi.obl ..\..\core\release\deploy64\lib\
copy /y ..\..\core\lib\fcgi_web.obl ..\..\core\release\deploy64\lib\
obc -src %1.obs -lib fcgi,gen_collect,net -tar web -dest %1.obw
if "%~2"=="" goto end
%WSCP% /client objec:%2@IIS-VM /command "put ..\..\core\lib\fcgi.obl /inetpub/wwwroot/objeck_fcgi/lib/fcgi.obl"
%WSCP% /client objec:%2@IIS-VM /command "put ..\..\core\lib\fcgi_web.obl /inetpub/wwwroot/objeck_fcgi/lib/fcgi_web.obl"
%WSCP% /client objec:%2@IIS-VM /command "put %1.obw /inetpub/wwwroot/objeck_fcgi/apps/%1.obw"
:end