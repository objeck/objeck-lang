@echo off

set WWW_ROOT=C:\inetpub\wwwroot

iisreset /stop

rmdir /s /q %WWW_ROOT%\native
mkdir %WWW_ROOT%\native
copy libobjk_iis.dll %WWW_ROOT%\native

del /q %WWW_ROOT%
copy config.ini %WWW_ROOT%
copy hello.obw %WWW_ROOT%
copy objeck_iis.dll %WWW_ROOT%

iisreset /start