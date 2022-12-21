REM clean up
rmdir /s /q objeck_fcgi64
mkdir objeck_fcgi64

REM update version information
powershell.exe -executionpolicy remotesigned -file  update_version.ps1

REM libraries
mkdir objeck_fcgi64\lib
mkdir objeck_fcgi64\lib\native
mkdir objeck_fcgi64\apps
copy ..\lib\*.obl objeck_fcgi64\lib
del /q objeck_fcgi64\bin\a.*
copy ..\vm\misc\*.pem objeck_fcgi64\lib

REM build binaries
devenv ..\vm\fcgi\fcgi.sln /rebuild "Release|x64"
mkdir objeck_fcgi64\bin
copy ..\vm\fcgi\x64\Release\*.exe objeck_fcgi64\bin
copy ..\lib\fcgi\windows\lib\x64\*.dll objeck_fcgi64\bin
copy ..\vm\fcgi\x64\Release\*.dll objeck_fcgi64\lib\native

REM openssl support
cd ..\lib\openssl
devenv openssl.sln /rebuild "Release|x64"
copy Release\win64\*.dll ..\..\Release\objeck_fcgi64\lib\native
cd ..\..\release

REM odbc support
cd ..\lib\odbc
devenv odbc.sln /rebuild "Release|x64"
copy Release\win64\*.dll ..\..\Release\objeck_fcgi64\lib\native
cd ..\..\release

REM copy readme
copy ..\..\docs\fcgi_readme.html objeck_fcgi64\readme.html

REM finished
if [%1] NEQ [deploy] goto end
	set ZIP_BIN="\Program Files\7-Zip"
	mkdir "%USERPROFILE%\Desktop\Release64\"
	del "%USERPROFILE%\Desktop\Release64\objeck-lang-fcgi-win64.zip" 
	rmdir /s /q objeck_fcgi
	rename objeck_fcgi64 objeck_fcgi
	%ZIP_BIN%\7z.exe a -r -tzip "%USERPROFILE%\Desktop\Release64\objeck-lang-fcgi-win64.zip" "objeck_fcgi\*"
:end
