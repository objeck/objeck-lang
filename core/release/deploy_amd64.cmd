REM clean up

if [%1]==[] (
	echo Windows targets are: 'x64' and 'arm64'
	goto end
)

set ZIP_BIN="\Program Files\7-Zip"

rmdir /s /q deploy64
mkdir deploy64
mkdir deploy64\app
mkdir deploy64\lib
mkdir deploy64\lib\sdl
mkdir deploy64\lib\sdl\fonts
mkdir deploy64\lib\native
mkdir deploy64\lib\native\misc
copy ..\lib\*.obl deploy64\lib

REM update version information
powershell.exe -executionpolicy remotesigned -file  update_version.ps1

REM compiler, runtime and debugger
if [%1] == [arm64] (
	devenv objeck.sln /rebuild "Release|ARM64"
)

if [%1] == [x64] (
	devenv objeck.sln /rebuild "Release|x64"
)

mkdir deploy64\bin
if [%1] == [arm64] (
	copy ARM64\Release\*.exe deploy64\bin
	mt.exe -manifest ..\vm\vs\manifest.xml -outputresource:deploy64\bin\obr.exe;1
	mt.exe -manifest ..\vm\vs\manifest.xml -outputresource:deploy64\bin\obi.exe;1
)

if [%1] == [x64] (
	copy ..\compiler\release\ARM64\Release\*.exe deploy64\bin
	mt.exe -manifest ..\vm\vs\manifest.xml -outputresource:..\vm\release\ARM64\Release\obr.exe;1
	copy ..\vm\release\ARM64\Release\*.exe deploy64\bin
	copy ..\debugger\release\ARM64\Release\*.exe deploy64\bin
	copy ..\repl\release\ARM64\Release\*.exe deploy64\bin
	mt.exe -manifest ..\vm\vs\manifest.xml -outputresource:..\repl\release\ARM64\Release\obi.exe;1
)

REM native launcher
if [%1] == [arm64] (
	cd ..\utils\launcher
	devenv native_launcher.sln /rebuild "Release|ARM64"
	copy ARM64\Release\obn.exe ..\..\release\deploy64\lib\native\misc
	copy ARM64\Release\obb.exe ..\..\release\deploy64\bin
	copy ..\..\vm\misc\config.prop ..\..\release\deploy64\lib\native\misc
	cd ..\..\release
)

if [%1] == [x64] (
	cd ..\utils\launcher
	devenv native_launcher.sln /rebuild "Release|x64"
	copy x64\Release\obn.exe ..\..\release\deploy64\lib\native\misc
	copy x64\Release\obb.exe ..\..\release\deploy64\bin
	copy ..\..\vm\misc\config.prop ..\..\release\deploy64\lib\native\misc
	cd ..\..\release
)

REM libraries
del /q deploy64\bin\a.*
copy ..\vm\misc\*.pem deploy64\lib

REM openssl support
cd ..\lib\openssl

if [%1] == [arm64] (
	devenv openssl.sln /rebuild "Release|ARM64"
	copy ARM64\Release\*.dll ..\..\release\deploy64\lib\native
)

if [%1] == [x64] (
	devenv openssl.sln /rebuild "Release|x64"
	copy Release\win64\*.dll ..\..\release\deploy64\lib\native
)
cd ..\..\release

REM app
cd ..\utils\WindowsApp
if [%1] == [arm64] (
	devenv AppLauncher.sln /rebuild "Release|ARM64"
	copy ARM64\Release\*.exe ..\..\release\deploy64\app
)

if [%1] == [x64] (
	devenv AppLauncher.sln /rebuild "Release|x64"
	copy x64\Release\*.exe ..\..\release\deploy64\app
)
cd ..\..\release

if [%1] == [x64] (
	REM matrix support
	cd ..\lib\matrix
	devenv matrix.sln /rebuild "Release|x64"
	copy Release\x64\*.dll ..\..\release\deploy64\lib\native
	cd ..\..\release

	REM odbc support
	cd ..\lib\odbc
	devenv odbc.sln /rebuild "Release|x64"
	copy Release\win64\*.dll ..\..\release\deploy64\lib\native
	cd ..\..\release

	REM sdl
	cd ..\lib\sdl
	devenv sdl\sdl.sln /rebuild "Release|x64"
	copy sdl\Release\x64\*.dll ..\..\release\deploy64\lib\native
	copy lib\fonts\*.ttf ..\..\release\deploy64\lib\sdl\fonts
	copy lib\x64\*.dll ..\..\release\deploy64\lib\sdl
	cd ..\..\release

	REM diags
	cd ..\lib\diags
	devenv diag.sln /rebuild "Release|x64"
	copy vs\Release\x64\*.dll ..\..\release\deploy64\lib\native
	cd ..\..\release
)

REM copy examples
mkdir deploy64\examples\

mkdir deploy64\examples\media\
del  /s /q ..\..\programs\*.obe
xcopy /e ..\..\programs\deploy\*.obs deploy64\examples\
xcopy /e ..\..\programs\deploy\media\*.png deploy64\examples\media\
xcopy /e ..\..\programs\deploy\media\*.wav deploy64\examples\media\
xcopy /e ..\..\programs\deploy\data\* deploy64\examples\data\

REM build and update docs
mkdir deploy64\doc 
mkdir deploy64\doc\syntax
xcopy /e ..\..\docs\syntax\* deploy64\doc\syntax

REM update and process readme
pushd ..\..\programs\deploy\util\readme
REM call build.cmd readme_builder readme.json
popd && copy ..\..\docs\readme.html deploy64

copy ..\..\docs\doc\readme.css deploy64\doc
copy ..\..\LICENSE deploy64

REM copy docs
if [%1] == [x64] (
	call code_doc64.cmd
)

if [%1] == [arm64] (
	%ZIP_BIN%\7z.exe x ..\..\docs\api.zip -odeploy64\doc
)

REM finished
if [%2] NEQ [deploy] goto end
	rmdir /q /s deploy64\examples\doc
	rmdir /q /s "%USERPROFILE%\Desktop\objeck-lang-win64"
	mkdir "%USERPROFILE%\Desktop\objeck-lang-win64"
	xcopy /e deploy64 "%USERPROFILE%\Desktop\objeck-lang-win64"
	mkdir "%USERPROFILE%\Desktop\objeck-lang-win64\doc\icons"
	copy ..\..\docs\images\setup_icons\*.ico "%USERPROFILE%\Desktop\objeck-lang-win64\doc\icons"
	copy ..\..\docs\images\setup_icons\*.jpg "%USERPROFILE%\Desktop\objeck-lang-win64\doc\icons"
	copy ..\..\docs\eula.rtf "%USERPROFILE%\Desktop\objeck-lang-win64\doc"
	copy /y ..\utils\setup64 .
	devenv setup.sln /rebuild "Release"
	signtool sign /fd sha256 /f "%USERPROFILE%\Dropbox\Personal\signing keys\2022\code\randy_hollines.p12" /p %3 /d "Objeck: Windows Toolchain" /t http://timestamp.sectigo.com Release64\setup.msi
	copy Release64\setup.msi "%USERPROFILE%\Desktop\objeck-windows-x64_0.0.0.msi"
	
	rmdir /s /q "%USERPROFILE%\Desktop\Release64"
	mkdir "%USERPROFILE%\Desktop\Release64"
	move "%USERPROFILE%\Desktop\objeck-lang-win64" "%USERPROFILE%\Desktop\Release64\objeck-lang"
	%ZIP_BIN%\7z.exe a -r -tzip "%USERPROFILE%\Desktop\Release64\objeck-windows-x64_0.0.0.zip" "%USERPROFILE%\Desktop\Release64\objeck-lang"
	move "%USERPROFILE%\Desktop\objeck-windows-x64_0.0.0.msi" "%USERPROFILE%\Desktop\Release64"
:end