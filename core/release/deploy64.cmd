REM clean up
rmdir /s /q deploy64
mkdir deploy64

REM update version information
powershell.exe -executionpolicy remotesigned -file  update_version.ps1

REM build binaries
devenv objeck.sln /rebuild "Release|x64"
mkdir deploy64\bin
copy ..\compiler\Release\win64\*.exe deploy64\bin
copy ..\vm\Release\win64\*.exe deploy64\bin
copy ..\debugger\Release\win64\*.exe deploy64\bin

REM libraries
mkdir deploy64\lib
copy ..\lib\*.obl deploy64\lib
del deploy64\lib\gtk2.obl
del deploy64\lib\sdl.obl
REM del deploy64\lib\query.obl
del /q deploy64\bin\a.*
copy ..\vm\misc\*.pem deploy64\lib

REM openssl support
mkdir deploy64\lib\native
cd ..\lib\openssl
devenv openssl.sln /rebuild "Release|x64"
copy Release\win64\*.dll ..\..\Release\deploy64\lib\native
copy ..\win64\bin\*.dll ..\..\Release\deploy64\bin
cd ..\..\release

REM odbc support
cd ..\lib\odbc
devenv odbc.sln /rebuild "Release|x64"
copy Release\win64\*.dll ..\..\Release\deploy64\lib\native
cd ..\..\release

REM copy examples
mkdir deploy64\examples\
mkdir deploy64\examples\doc\
mkdir deploy64\examples\tiny\
xcopy /e ..\..\programs\deploy64\*.obs deploy64\examples\
xcopy /e ..\..\programs\doc\* deploy64\examples\doc\
xcopy /e ..\..\programs\tiny\* deploy64\examples\tiny\
del  /s /q ..\..\programs\tiny\*.obe
del  /s /q ..\..\programs\tiny\*.e

REM build and update docs
mkdir deploy64\doc 
copy ..\..\docs\guide\objeck_lang.pdf deploy64\doc 
mkdir deploy64\doc\syntax
xcopy /e ..\..\docs\syntax\* deploy64\doc\syntax
copy ..\..\docs\readme.htm deploy64
call code_doc64.cmd

REM finished
if [%1] NEQ [deploy64] goto end
	mkdir "%USERPROFILE%\Desktop\objeck-lang"
	xcopy /e deploy64 "%USERPROFILE%\Desktop\objeck-lang"
	mkdir "%USERPROFILE%\Desktop\objeck-lang\doc\icons"
	copy ..\..\images\setup_icons\*.ico "%USERPROFILE%\Desktop\objeck-lang\doc\icons"
	copy ..\..\images\setup_icons\*.jpg "%USERPROFILE%\Desktop\objeck-lang\doc\icons"
	copy ..\..\docs\eula.rtf "%USERPROFILE%\Desktop\objeck-lang\doc"
	copy ..\..\docs\uninstall.vbs "%USERPROFILE%\Desktop\objeck-lang\doc"
	copy ..\setup
	devenv setup.sln /rebuild "Release|x64"
	signtool sign /f "D:\Dropbox\Personal\signing keys\2016\randy_hollines.pfx" /p %2 /d "Objeck Toolchain" /t http://timestamp.verisign.com/scripts/timstamp.dll Release\setup.msi
	copy Release\setup.msi "%USERPROFILE%\Desktop\objeck-lang.msi"
	
	rmdir /s /q "%USERPROFILE%\Desktop\Release"
	mkdir "%USERPROFILE%\Desktop\Release"
	move "%USERPROFILE%\Desktop\objeck-lang" "%USERPROFILE%\Desktop\Release"
	move "%USERPROFILE%\Desktop\objeck-lang.msi" "%USERPROFILE%\Desktop\Release"
:end
