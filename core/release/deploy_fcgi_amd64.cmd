REM clean up
rmdir /s /q deploy_fcgi64
mkdir deploy_fcgi64

REM update version information
powershell.exe -executionpolicy remotesigned -file  update_version.ps1

REM libraries
mkdir deploy_fcgi64\lib
mkdir deploy_fcgi64\lib\native
mkdir deploy_fcgi64\apps
copy ..\lib\*.obl deploy_fcgi64\lib
del /q deploy_fcgi64\bin\a.*
copy ..\vm\misc\*.pem deploy_fcgi64\lib

REM build binaries
devenv ..\vm\fcgi\fcgi.sln /rebuild "Release|x64"
mkdir deploy_fcgi64\bin
copy ..\vm\fcgi\x64\Release\*.exe deploy_fcgi64\bin
copy ..\vm\fcgi\x64\Release\*.dll deploy_fcgi64\bin

REM openssl support
cd ..\lib\openssl
devenv openssl.sln /rebuild "Release|x64"
copy Release\win64\*.dll ..\..\Release\deploy_fcgi64\lib\native
cd ..\..\release

REM odbc support
cd ..\lib\odbc
devenv odbc.sln /rebuild "Release|x64"
copy Release\win64\*.dll ..\..\Release\deploy_fcgi64\lib\native
cd ..\..\release

REM copy readme
copy ..\..\docs\fcgi_readme.html deploy_fcgi64\readme.html

REM finished
if [%1] NEQ [deploy] goto end
	set ZIP_BIN="\Program Files\7-Zip"
	rmdir /q /s deploy_fcgi64\examples\doc
	rmdir /q /s "%USERPROFILE%\Desktop\objeck-lang-win64"
	mkdir "%USERPROFILE%\Desktop\objeck-lang-win64"
	xcopy /e deploy_fcgi64 "%USERPROFILE%\Desktop\objeck-lang-win64"
	mkdir "%USERPROFILE%\Desktop\objeck-lang-win64\doc\icons"
	copy ..\..\images\setup_icons\*.ico "%USERPROFILE%\Desktop\objeck-lang-win64\doc\icons"
	copy ..\..\images\setup_icons\*.jpg "%USERPROFILE%\Desktop\objeck-lang-win64\doc\icons"
	copy ..\..\docs\eula.rtf "%USERPROFILE%\Desktop\objeck-lang-win64\doc"
	copy ..\..\docs\uninstall.vbs "%USERPROFILE%\Desktop\objeck-lang-win64\doc"
	copy ..\..\docs\getting_started.url "%USERPROFILE%\Desktop\objeck-lang-win64\doc"
	copy /y ..\setup64
	devenv setup.sln /rebuild "Release"
	signtool sign /f "d:\Dropbox\Personal\signing keys\2018\randy_hollines.p12" /p %2 /d "Objeck Toolchain" /t http://timestamp.comodoca.com Release64\setup.msi
	copy Release64\setup.msi "%USERPROFILE%\Desktop\objeck-lang-win64.msi"
	
	rmdir /s /q "%USERPROFILE%\Desktop\Release64"
	mkdir "%USERPROFILE%\Desktop\Release64"
	move "%USERPROFILE%\Desktop\objeck-lang-win64" "%USERPROFILE%\Desktop\Release64"
	%ZIP_BIN%\7z.exe a -r -tzip "%USERPROFILE%\Desktop\Release64\objeck-lang-win64.zip" "%USERPROFILE%\Desktop\Release64\objeck-lang-win64\*"
	move "%USERPROFILE%\Desktop\objeck-lang-win64.msi" "%USERPROFILE%\Desktop\Release64"
:end
