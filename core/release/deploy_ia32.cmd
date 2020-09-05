REM clean up
rmdir /s /q deploy
mkdir deploy

REM update version information
powershell.exe -executionpolicy remotesigned -file  update_version.ps1

REM build binaries
devenv objeck.sln /rebuild "Release|x86"
mkdir deploy\bin
copy ..\compiler\Release\*.exe deploy\bin
copy ..\vm\Release\*.exe deploy\bin
copy ..\debugger\Release\*.exe deploy\bin

REM libraries
mkdir deploy\lib
mkdir deploy\lib\sdl
mkdir deploy\lib\sdl\fonts
mkdir deploy\lib\sdl
copy ..\lib\*.obl deploy\lib
del deploy\lib\gtk2.obl
del /q deploy\bin\a.*
copy ..\vm\misc\*.pem deploy\lib

REM openssl support
mkdir deploy\lib\native
cd ..\lib\openssl
devenv openssl.sln /rebuild "Release|x86"
REM copy win\x86\bin\*.dll ..\..\Release\deploy\bin
copy Release\*.dll ..\..\Release\deploy\lib\native
copy win\x86\bin\*.dll ..\..\Release\deploy\bin
cd ..\..\Release

REM odbc support
cd ..\lib\odbc
devenv odbc.sln  /rebuild "Release|x86"
copy Release\*.dll ..\..\Release\deploy\lib\native
cd ..\..\Release

REM sdl
cd ..\lib\sdl
devenv sdl\sdl.sln /rebuild "Release|x86"
copy sdl\Release\Win32\*.dll ..\..\Release\deploy\lib\native
copy lib\fonts\*.ttf ..\..\Release\deploy\lib\sdl\fonts
copy lib\x86\*.dll ..\..\Release\deploy\lib\sdl
cd ..\..\Release

REM app
mkdir deploy\app
cd WindowsLauncher
devenv AppLauncher.sln /rebuild "Release|x86"
copy win32\Release\*.exe ..\deploy\app
copy AppLauncher\set_ob_env.cmd ..\deploy\app
cd ..

REM copy examples
mkdir deploy\examples\
mkdir deploy\examples\doc\
mkdir deploy\examples\tiny\
mkdir deploy\examples\media\
del  /s /q ..\..\programs\*.obe
xcopy /e ..\..\programs\deploy\*.obs deploy\examples\
xcopy /e ..\..\programs\deploy\media\*.png deploy\examples\media\
xcopy /e ..\..\programs\deploy\media\*.wav deploy\examples\media\
xcopy /e ..\..\programs\doc\* deploy\examples\doc\
xcopy /e ..\..\programs\tiny\* deploy\examples\tiny\
del  /s /q ..\..\programs\tiny\*.obe
del  /s /q ..\..\programs\tiny\*.e

REM build and update docs
mkdir deploy\doc 
mkdir deploy\doc\syntax
xcopy /e ..\..\docs\syntax\* deploy\doc\syntax
copy ..\..\docs\readme.html deploy
copy ..\..\LICENSE deploy
call code_doc.cmd

REM finished
if [%1] NEQ [deploy] goto end
	set ZIP_BIN="\Program Files\7-Zip"
	rmdir /q /s deploy\examples\doc
	rmdir /q /s "%USERPROFILE%\Desktop\objeck-lang-win32"
	mkdir "%USERPROFILE%\Desktop\objeck-lang-win32"
	xcopy /e deploy "%USERPROFILE%\Desktop\objeck-lang-win32"
	mkdir "%USERPROFILE%\Desktop\objeck-lang-win32\doc\icons"
	copy ..\..\images\setup_icons\*.ico "%USERPROFILE%\Desktop\objeck-lang-win32\doc\icons"
	copy ..\..\images\setup_icons\*.jpg "%USERPROFILE%\Desktop\objeck-lang-win32\doc\icons"
	copy ..\..\docs\eula.rtf "%USERPROFILE%\Desktop\objeck-lang-win32\doc"
	copy ..\..\docs\uninstall.vbs "%USERPROFILE%\Desktop\objeck-lang-win32\doc"
	copy ..\..\docs\getting_started.url "%USERPROFILE%\Desktop\objeck-lang-win32\doc"
	
	copy /y ..\setup
	devenv setup.sln /rebuild "Release"
	signtool sign /f "d:\Dropbox\Personal\signing keys\2018\randy_hollines.p12" /p %2 /d "Objeck Toolchain" /t http://timestamp.comodoca.com Release\setup.msi
	copy Release\setup.msi "%USERPROFILE%\Desktop\objeck-lang-win32.msi"
	
	rmdir /s /q "%USERPROFILE%\Desktop\Release"
	mkdir "%USERPROFILE%\Desktop\Release"
	move "%USERPROFILE%\Desktop\objeck-lang-win32" "%USERPROFILE%\Desktop\Release"
	%ZIP_BIN%\7z.exe a -r -tzip "%USERPROFILE%\Desktop\Release\objeck-lang-win32.zip" "%USERPROFILE%\Desktop\Release\objeck-lang-win32\*"
	move "%USERPROFILE%\Desktop\objeck-lang-win32.msi" "%USERPROFILE%\Desktop\Release"
:end
