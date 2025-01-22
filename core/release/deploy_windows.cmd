REM clean up

if not [%1]==[x64] if not [%1]==[arm64] (
	echo Windows targets are: 'x64' and 'arm64'
	goto end
)

IF "%VCINSTALLDIR%"=="" (
	echo Could not Visual Studio build environment
	goto end
)

set ZIP_BIN="\Program Files\7-Zip"

if [%1] == [arm64] (
	set TARGET=deploy-arm64
)

if [%1] == [x64] (
	set TARGET=deploy-x64
)

REM debug installer
REM goto installer

rmdir /s /q %TARGET%
mkdir %TARGET%
mkdir %TARGET%\app
mkdir %TARGET%\lib
mkdir %TARGET%\lib\sdl
mkdir %TARGET%\lib\sdl\fonts
mkdir %TARGET%\lib\native
mkdir %TARGET%\lib\native\misc
copy ..\lib\*.obl %TARGET%\lib

REM update version information
powershell.exe -executionpolicy remotesigned -file  update_version.ps1

REM compiler, runtime and debugger
if [%1] == [arm64] (
	devenv objeck.sln /rebuild "Release|ARM64"
)

if [%1] == [x64] (
	devenv objeck.sln /rebuild "Release|x64"
)

mkdir %TARGET%\bin
if [%1] == [arm64] (
	copy ARM64\Release\*.exe %TARGET%\bin
	mt.exe -manifest ..\vm\vs\manifest.xml -outputresource:%TARGET%\bin\obr.exe;1
	mt.exe -manifest ..\vm\vs\manifest.xml -outputresource:%TARGET%\bin\obi.exe;1
)

if [%1] == [x64] (
	copy ..\compiler\release\win64\*.exe %TARGET%\bin
	mt.exe -manifest ..\vm\vs\manifest.xml -outputresource:%TARGET%\bin\obr.exe;1	

	copy ..\repl\release\win64\*.exe %TARGET%\bin
	mt.exe -manifest ..\vm\vs\manifest.xml -outputresource:%TARGET%\bin\obi.exe;1

	copy ..\vm\release\win64\*.exe %TARGET%\bin
	copy ..\debugger\release\win64\*.exe %TARGET%\bin
)

REM native launcher
if [%1] == [arm64] (
	cd ..\utils\launcher
	devenv native_launcher.sln /rebuild "Release|ARM64"
	copy ARM64\Release\obn.exe ..\..\release\%TARGET%\lib\native\misc
	copy ARM64\Release\obb.exe ..\..\release\%TARGET%\bin
	copy ..\..\vm\misc\config.prop ..\..\release\%TARGET%\lib\native\misc
	cd ..\..\release
)

if [%1] == [x64] (
	cd ..\utils\launcher
	devenv native_launcher.sln /rebuild "Release|x64"
	copy x64\Release\obn.exe ..\..\release\%TARGET%\lib\native\misc
	copy x64\Release\obb.exe ..\..\release\%TARGET%\bin
	copy ..\..\vm\misc\config.prop ..\..\release\%TARGET%\lib\native\misc
	cd ..\..\release
)

REM libraries
del /q %TARGET%\bin\a.*
copy ..\vm\misc\*.pem %TARGET%\lib

REM openssl support
cd ..\lib\openssl

if [%1] == [arm64] (
	devenv openssl.sln /rebuild "Release|ARM64"
	copy ARM64\Release\*.dll ..\..\release\%TARGET%\lib\native
)

if [%1] == [x64] (
	devenv openssl.sln /rebuild "Release|x64"
	copy Release\win64\*.dll ..\..\release\%TARGET%\lib\native
)
cd ..\..\release

REM app
cd ..\utils\WindowsApp
if [%1] == [arm64] (
	devenv AppLauncher.sln /rebuild "Release|ARM64"
	copy ARM64\Release\*.exe ..\..\release\%TARGET%\app
)

if [%1] == [x64] (
	devenv AppLauncher.sln /rebuild "Release|x64"
	copy x64\Release\*.exe ..\..\release\%TARGET%\app
)
cd ..\..\release

REM diags
cd ..\lib\diags
if [%1] == [arm64] (
	devenv diag.sln /rebuild "Release|ARM64"
	copy vs\Release\ARM64\*.dll* ..\..\release\%TARGET%\lib\native
)

if [%1] == [x64] (
	devenv diag.sln /rebuild "Release|x64"
	copy vs\Release\x64\*.dll ..\..\release\%TARGET%\lib\native
)
cd ..\..\release


REM odbc support
cd ..\lib\odbc
if [%1] == [arm64] (
	devenv odbc.sln /rebuild "Release|ARM64"
	copy ARM64\Release\*.dll ..\..\release\%TARGET%\lib\native
)

if [%1] == [x64] (
	devenv odbc.sln /rebuild "Release|x64"
	copy Release\win64\*.dll ..\..\release\%TARGET%\lib\native
)
cd ..\..\release

REM matrix support
cd ..\lib\matrix	
if [%1] == [arm64] (
	devenv matrix.sln /rebuild "Release|ARM64"
	copy Release\ARM64\*.dll ..\..\release\%TARGET%\lib\native
)

if [%1] == [x64] (
	devenv matrix.sln /rebuild "Release|x64"
	copy Release\x64\*.dll ..\..\release\%TARGET%\lib\native
)
cd ..\..\release

REM TODO: migrate to arm64
if [%1] == [arm64] (
	REM sdl
	cd ..\lib\sdl
	devenv sdl\sdl.sln /rebuild "Release|ARM64"
	copy sdl\Release\arm64\*.dll ..\..\release\%TARGET%\lib\native
	copy lib\fonts\*.ttf ..\..\release\%TARGET%\lib\sdl\fonts
	copy lib\arm64\*.dll ..\..\release\%TARGET%\lib\sdl
	cd ..\..\release
)

if [%1] == [x64] (
	REM sdl
	cd ..\lib\sdl
	devenv sdl\sdl.sln /rebuild "Release|x64"
	copy sdl\Release\x64\*.dll ..\..\release\%TARGET%\lib\native
	copy lib\fonts\*.ttf ..\..\release\%TARGET%\lib\sdl\fonts
	copy lib\x64\*.dll ..\..\release\%TARGET%\lib\sdl
	cd ..\..\release
)

REM copy examples
mkdir %TARGET%\examples\

mkdir %TARGET%\examples\media\
del  /s /q ..\..\programs\*.obe
xcopy /e ..\..\programs\deploy\*.obs %TARGET%\examples\
xcopy /e ..\..\programs\deploy\media\*.png %TARGET%\examples\media\
xcopy /e ..\..\programs\deploy\media\*.wav %TARGET%\examples\media\
xcopy /e ..\..\programs\deploy\data\* %TARGET%\examples\data\

REM build and update docs
mkdir %TARGET%\doc 
mkdir %TARGET%\doc\syntax
xcopy /e ..\..\docs\syntax\* %TARGET%\doc\syntax

REM update and process readme
pushd ..\..\programs\deploy\util\readme
REM call build.cmd readme_builder readme.json
popd && copy ..\..\docs\readme.html %TARGET%

copy ..\..\docs\doc\readme.css %TARGET%\doc
copy ..\..\LICENSE %TARGET%

REM copy docs
if [%1] == [arm64] (
	%ZIP_BIN%\7z.exe x ..\..\docs\api.zip -o%TARGET%\doc
	rmdir /s /q ARM64
)

if [%1] == [x64] (
	call code_doc64.cmd x64
	rmdir /s /q x64
)

:installer

REM finished
if [%2] NEQ [deploy] goto end
	if [%1] == [arm64] (
		set INSTALL_TARGET=objeck-lang-arm64
	)

	if [%1] == [x64] (
		set INSTALL_TARGET=objeck-lang-x64
	)

	rmdir /q /s %TARGET%\examples\doc
	rmdir /q /s "%USERPROFILE%\Documents\Objeck-Build\%INSTALL_TARGET%"
	mkdir "%USERPROFILE%\Documents\Objeck-Build\%INSTALL_TARGET%"
	xcopy /e %TARGET% "%USERPROFILE%\Documents\Objeck-Build\%INSTALL_TARGET%"
	mkdir "%USERPROFILE%\Documents\Objeck-Build\%INSTALL_TARGET%\doc\icons"
	copy ..\..\docs\images\setup_icons\*.ico "%USERPROFILE%\Documents\Objeck-Build\%INSTALL_TARGET%\doc\icons"
	copy ..\..\docs\images\setup_icons\*.jpg "%USERPROFILE%\Documents\Objeck-Build\%INSTALL_TARGET%\doc\icons"
	copy ..\..\docs\eula.rtf "%USERPROFILE%\Documents\Objeck-Build\%INSTALL_TARGET%\doc"
	
	if [%1] == [arm64] (
		copy /y ..\utils\setup\arm64 .
		devenv setup-arm64.sln /rebuild "Release"
		signtool sign /fd sha256 /f "%USERPROFILE%\Dropbox\Personal\signing keys\2022\code\randy_hollines.p12" /p %3 /d "Objeck: Windows Toolchain" /t http://timestamp.sectigo.com release-arm64\setup.msi
		copy release-arm64\setup.msi "%USERPROFILE%\Documents\Objeck-Build\objeck-windows-arm64_0.0.0.msi"

		rmdir /s /q "%USERPROFILE%\Documents\Objeck-Build\release-arm64"
		mkdir "%USERPROFILE%\Documents\Objeck-Build\release-arm64"
		move "%USERPROFILE%\Documents\Objeck-Build\%INSTALL_TARGET%" "%USERPROFILE%\Documents\Objeck-Build\release-arm64\%INSTALL_TARGET%"
		%ZIP_BIN%\7z.exe a -r -tzip "%USERPROFILE%\Documents\Objeck-Build\release-arm64\objeck-windows-arm64_0.0.0.zip" "%USERPROFILE%\Documents\Objeck-Build\release-arm64\%INSTALL_TARGET%"
		move "%USERPROFILE%\Documents\Objeck-Build\objeck-windows-arm64_0.0.0.msi" "%USERPROFILE%\Documents\Objeck-Build\release-arm64"
	)

	if [%1] == [x64] (
		copy /y ..\utils\setup\x64 .
		devenv setup-x64.sln /rebuild "Release"
		signtool sign /fd sha256 /f "%USERPROFILE%\Dropbox\Personal\signing keys\2022\code\randy_hollines.p12" /p %3 /d "Objeck: Windows Toolchain" /t http://timestamp.sectigo.com release-x64\setup.msi
		copy release-x64\setup.msi "%USERPROFILE%\Documents\Objeck-Build\objeck-windows-x64_0.0.0.msi"

		rmdir /s /q "%USERPROFILE%\Documents\Objeck-Build\release-x64"
		mkdir "%USERPROFILE%\Documents\Objeck-Build\release-x64"
		move "%USERPROFILE%\Documents\Objeck-Build\%INSTALL_TARGET%" "%USERPROFILE%\Documents\Objeck-Build\release-x64\%INSTALL_TARGET%"
		%ZIP_BIN%\7z.exe a -r -tzip "%USERPROFILE%\Documents\Objeck-Build\release-x64\objeck-windows-x64_0.0.0.zip" "%USERPROFILE%\Documents\Objeck-Build\release-x64\%INSTALL_TARGET%"
		move "%USERPROFILE%\Documents\Objeck-Build\objeck-windows-x64_0.0.0.msi" "%USERPROFILE%\Documents\Objeck-Build\release-x64"
	)
:end