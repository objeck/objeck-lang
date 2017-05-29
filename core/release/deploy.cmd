REM clean up
rmdir /s /q deploy
mkdir deploy

REM update version information
powershell.exe -executionpolicy remotesigned -file  update_version.ps1

REM build binaries
devenv /rebuild Release objeck.sln
mkdir deploy\bin
copy ..\compiler\Release\*.exe deploy\bin
copy ..\vm\Release\*.exe deploy\bin
copy ..\debugger\Release\*.exe deploy\bin

REM libraries
mkdir deploy\lib
copy ..\lib\*.obl deploy\lib
del deploy\lib\gtk2.obl
del deploy\lib\sdl.obl
del deploy\lib\db.obl
del /q deploy\bin\a.*
copy ..\vm\misc\*.pem deploy\lib

REM openssl support
mkdir deploy\lib\native
cd ..\lib\openssl
devenv /rebuild Release openssl.sln
copy Release\*.dll ..\..\release\deploy\lib\native
copy ..\win32\bin\*.dll ..\..\release\deploy\bin
cd ..\..\release

REM odbc support
cd ..\lib\odbc
devenv /rebuild Release odbc.sln
copy Release\*.dll ..\..\release\deploy\lib\native
cd ..\..\release

REM copy examples
mkdir deploy\examples\
mkdir deploy\examples\doc\
mkdir deploy\examples\tiny\
xcopy /e ..\..\programs\deploy\*.obs deploy\examples\
xcopy /e ..\..\programs\doc\* deploy\examples\doc\
xcopy /e ..\..\programs\tiny\* deploy\examples\tiny\
del  /s /q ..\..\programs\tiny\*.obe
del  /s /q ..\..\programs\tiny\*.e

REM build and update docs
mkdir deploy\doc 
copy ..\..\docs\guide\objeck_lang.pdf deploy\doc 
mkdir deploy\doc\syntax
copy ..\..\docs\syntax\* deploy\doc\syntax
copy ..\..\docs\readme.htm deploy
call code_doc.cmd

REM finished
if [%1] NEQ [deploy] goto end
	rmdir /s /q "%HOMEDRIVE%%HOMEPATH%\Desktop\objeck-lang"
	mkdir "%HOMEDRIVE%%HOMEPATH%\Desktop\objeck-lang"
	xcopy /e deploy "%HOMEDRIVE%%HOMEPATH%\Desktop\objeck-lang"
	mkdir "%HOMEDRIVE%%HOMEPATH%\Desktop\objeck-lang\doc\icons"
	copy ..\..\images\setup_icons\*.ico "%HOMEDRIVE%%HOMEPATH%\Desktop\objeck-lang\doc\icons"
	copy ..\..\docs\eula.rtf "%HOMEDRIVE%%HOMEPATH%\Desktop\objeck-lang\doc"
:end
