REM clean up

if not [%1]==[x64] if not [%1]==[arm64] (
	echo Windows targets are: 'x64' and 'arm64'
	goto end
)

IF "%VCINSTALLDIR%"=="" (
	echo Could not Visual Studio build environment
	goto end
)

REM Check for mbedTLS ARM64 libraries
if [%1] == [arm64] (
	if not exist "..\lib\openssl\win\arm64\mbedtls.lib" (
		echo.
		echo ============================================================
		echo  ERROR: mbedTLS ARM64 libraries not found
		echo ============================================================
		echo.
		echo The ARM64 build requires mbedTLS 3.6.3 libraries.
		echo.
		echo Building mbedTLS ARM64 libraries automatically...
		echo This is a one-time setup that will take 5-10 minutes.
		echo.

		pushd ..\lib\crypto
		call build_mbedtls_arm64.cmd
		popd

		if not exist "..\lib\openssl\win\arm64\mbedtls.lib" (
			echo.
			echo ERROR: Failed to build mbedTLS ARM64 libraries.
			echo Please see core\lib\crypto\BUILD_MBEDTLS_ARM64.md for manual instructions.
			echo.
			goto end
		)

		echo.
		echo mbedTLS ARM64 libraries installed successfully!
		echo Continuing with build...
		echo.
	)
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
copy ..\lib\*.ini %TARGET%\lib

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
	REM Cross-compilation: use x64 host mt.exe (ARM64 mt.exe can't run on x64 host)
	REM WindowsSdkVerBinPath has trailing backslash, e.g., "C:\Program Files (x86)\Windows Kits\10\bin\10.0.26100.0\"
	"%WindowsSdkVerBinPath%x64\mt.exe" -manifest ..\vm\vs\manifest.xml -outputresource:%TARGET%\bin\obr.exe;1
	"%WindowsSdkVerBinPath%x64\mt.exe" -manifest ..\vm\vs\manifest.xml -outputresource:%TARGET%\bin\obi.exe;1

	copy "%VCToolsRedistDir%\arm64\Microsoft.VC143.CRT\vcruntime140.dll" %TARGET%\bin
	copy "%VCToolsRedistDir%\arm64\Microsoft.VC143.CRT\vcruntime140_1.dll" %TARGET%\bin
)

if [%1] == [x64] (
	copy ..\compiler\release\win64\*.exe %TARGET%\bin
	copy ..\repl\release\win64\*.exe %TARGET%\bin
	copy ..\vm\release\win64\*.exe %TARGET%\bin
	copy ..\debugger\release\win64\*.exe %TARGET%\bin

	REM Embed manifests AFTER copying binaries
	mt.exe -manifest ..\vm\vs\manifest.xml -outputresource:%TARGET%\bin\obr.exe;1
	mt.exe -manifest ..\vm\vs\manifest.xml -outputresource:%TARGET%\bin\obi.exe;1

	copy "%VCToolsRedistDir%\x64\Microsoft.VC143.CRT\vcruntime140.dll" %TARGET%\bin
	copy "%VCToolsRedistDir%\x64\Microsoft.VC143.CRT\vcruntime140_1.dll" %TARGET%\bin
)

copy ..\lib\lame\win\%1\*.dll %TARGET%\bin

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

REM crypto support (optional - requires mbedtls)
cd ..\lib\crypto

if [%1] == [arm64] (
	devenv crypto.sln /rebuild "Release|ARM64"
	if exist ARM64\Release\*.dll (
		copy ARM64\Release\*.dll ..\..\release\%TARGET%\lib\native
	) else (
		echo Warning: Crypto library DLL not found - skipping (mbedtls may not be available)
	)
)

if [%1] == [x64] (
	devenv crypto.sln /rebuild "Release|x64"
	if exist Release\win64\*.dll (
		copy Release\win64\*.dll ..\..\release\%TARGET%\lib\native
	) else (
		echo Warning: Crypto library DLL not found - skipping (mbedtls may not be available)
	)
)
cd ..\..\release

REM lame support
cd ..\lib\lame

if [%1] == [arm64] (
	devenv lame.sln /rebuild "Release|ARM64"
	copy ARM64\Release\libobjk_lame.dll ..\..\release\%TARGET%\lib\native
)

if [%1] == [x64] (
	devenv lame.sln /rebuild "Release|x64"
	copy x64\Release\libobjk_lame.dll ..\..\release\%TARGET%\lib\native
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

REM opencv support
cd ..\lib\opencv	
if [%1] == [arm64] (
	devenv opencv.sln /rebuild "Release|ARM64"
	copy arm64\Release\libobjk_opencv.dll ..\..\release\%TARGET%\lib\native

	copy /y win\arm64\bin\opencv_world4120.dll ..\..\release\%TARGET%\bin
	copy /y win\arm64\bin\opencv_videoio_ffmpeg4120_64.dll ..\..\release\%TARGET%\bin
)

if [%1] == [x64] (
	devenv opencv.sln /rebuild "Release|x64"
	copy x64\Release\libobjk_opencv.dll ..\..\release\%TARGET%\lib\native

	copy /y win\x64\bin\opencv_world4120.dll ..\..\release\%TARGET%\bin
	copy /y win\x64\bin\opencv_videoio_ffmpeg4120_64.dll ..\..\release\%TARGET%\bin
)
cd ..\..\release

REM onnx support
cd ..\lib\onnx

REM Restore NuGet packages before building
nuget restore onnx.sln

if [%1] == [arm64] (
	devenv onnx.sln /rebuild "Release-QNN|ARM64"
	copy vs\ARM64\Release-QNN\libobjk_onnx.dll ..\..\release\%TARGET%\lib\native

	copy /y eq\qnn\win\onnx\arm64\bin\*.dll ..\..\release\%TARGET%\bin
)

if [%1] == [x64] (
	devenv onnx.sln /rebuild "Release-DML|x64"
	copy vs\x64\Release-DML\libobjk_onnx.dll ..\..\release\%TARGET%\lib\native

	copy /y packages\Microsoft.ML.OnnxRuntime.DirectML.1.22.1\runtimes\win-x64\native\*.dll ..\..\release\%TARGET%\bin
	copy /y packages\Microsoft.AI.DirectML.1.15.4\bin\x64-win\DirectML.dll ..\..\release\%TARGET%\bin
)
cd ..\..\release

REM sdl support
cd ..\lib\sdl	
if [%1] == [arm64] (
	REM sdl
	devenv sdl\sdl.sln /rebuild "Release|ARM64"
	copy sdl\Release\arm64\*.dll ..\..\release\%TARGET%\lib\native
	copy lib\fonts\*.ttf ..\..\release\%TARGET%\lib\sdl\fonts
	copy lib\arm64\*.dll ..\..\release\%TARGET%\lib\sdl
)

if [%1] == [x64] (
	REM sdl
	devenv sdl\sdl.sln /rebuild "Release|x64"
	copy sdl\Release\x64\*.dll ..\..\release\%TARGET%\lib\native
	copy lib\fonts\*.ttf ..\..\release\%TARGET%\lib\sdl\fonts
	copy lib\x64\*.dll ..\..\release\%TARGET%\lib\sdl
)
cd ..\..\release

REM copy examples
mkdir %TARGET%\examples\

mkdir %TARGET%\examples\media\
del  /s /q ..\..\programs\*.obe
xcopy /e ..\..\programs\deploy\*.obs %TARGET%\examples\
xcopy /e ..\..\programs\deploy\media\*.png %TARGET%\examples\media\
xcopy /e ..\..\programs\deploy\media\*.wav %TARGET%\examples\media\
xcopy /e ..\..\programs\deploy\data\* %TARGET%\examples\data\

REM copy ONNX demo programs
copy ..\..\programs\frameworks\opencv_onnx\demo_phi3*.obs %TARGET%\examples\

REM copy ONNX models (phi3 text and phi3v vision) if downloaded
set MODELS_SRC=..\..\programs\frameworks\opencv_onnx\data\models
if exist "%MODELS_SRC%\phi3\directml\directml-int4-awq-block-128\model.onnx" (
	mkdir %TARGET%\examples\data\models\phi3
	xcopy /y /q "%MODELS_SRC%\phi3\directml\directml-int4-awq-block-128\*.onnx" %TARGET%\examples\data\models\phi3\
	xcopy /y /q "%MODELS_SRC%\phi3\directml\directml-int4-awq-block-128\*.onnx.data" %TARGET%\examples\data\models\phi3\
	xcopy /y /q "%MODELS_SRC%\phi3\directml\directml-int4-awq-block-128\*.json" %TARGET%\examples\data\models\phi3\
)
if exist "%MODELS_SRC%\phi3v\directml-int4-rtn-block-32\model.onnx" (
	mkdir %TARGET%\examples\data\models\phi3v
	xcopy /y /q "%MODELS_SRC%\phi3v\directml-int4-rtn-block-32\*.onnx" %TARGET%\examples\data\models\phi3v\
	xcopy /y /q "%MODELS_SRC%\phi3v\directml-int4-rtn-block-32\*.onnx.data" %TARGET%\examples\data\models\phi3v\
	xcopy /y /q "%MODELS_SRC%\phi3v\directml-int4-rtn-block-32\*.json" %TARGET%\examples\data\models\phi3v\
)

REM build and update docs
mkdir %TARGET%\doc 
mkdir %TARGET%\doc\syntax
xcopy /e ..\..\docs\syntax\* %TARGET%\doc\syntax

REM update and process readme
mkdir %TARGET%\style 
copy ..\..\docs\style\*.css %TARGET%\style
copy ..\lib\code_doc\templates\resources\*.png %TARGET%\style
copy ..\..\docs\readme.html %TARGET%
copy ..\..\LICENSE %TARGET%

REM copy docs (skip for ARM64 cross-compilation - can't run ARM64 binaries on x64 host)
if [%1] == [x64] (
	call code_doc64.cmd %1 deploy
	rmdir /s /q %1
) else (
	echo Skipping code_doc for ARM64 cross-compilation - using pre-built API docs
	mkdir %TARGET%\doc\api
	powershell -Command "Expand-Archive -Path '..\..\docs\api.zip' -DestinationPath '%TARGET%\doc' -Force"
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

	REM Create directory structure for MSI build (files must be in release-x64 or release-arm64)
	REM Use repo-relative path (..\..\Objeck-Build) to match MSI project expectations
	if [%1] == [arm64] (
		rmdir /q /s ..\..\Objeck-Build\release-arm64
		mkdir ..\..\Objeck-Build\release-arm64\%INSTALL_TARGET%
		xcopy /e %TARGET% ..\..\Objeck-Build\release-arm64\%INSTALL_TARGET%
		mkdir ..\..\Objeck-Build\release-arm64\%INSTALL_TARGET%\doc\icons
		copy ..\..\docs\images\setup_icons\*.ico ..\..\Objeck-Build\release-arm64\%INSTALL_TARGET%\doc\icons
		copy ..\..\docs\images\setup_icons\*.jpg ..\..\Objeck-Build\release-arm64\%INSTALL_TARGET%\doc\icons
		copy ..\..\docs\eula.rtf ..\..\Objeck-Build\release-arm64\%INSTALL_TARGET%\doc
	)

	if [%1] == [x64] (
		rmdir /q /s ..\..\Objeck-Build\release-x64
		mkdir ..\..\Objeck-Build\release-x64\%INSTALL_TARGET%
		xcopy /e %TARGET% ..\..\Objeck-Build\release-x64\%INSTALL_TARGET%
		mkdir ..\..\Objeck-Build\release-x64\%INSTALL_TARGET%\doc\icons
		copy ..\..\docs\images\setup_icons\*.ico ..\..\Objeck-Build\release-x64\%INSTALL_TARGET%\doc\icons
		copy ..\..\docs\images\setup_icons\*.jpg ..\..\Objeck-Build\release-x64\%INSTALL_TARGET%\doc\icons
		copy ..\..\docs\eula.rtf ..\..\Objeck-Build\release-x64\%INSTALL_TARGET%\doc
	)

	REM Build MSI installer with WiX v5
	REM Use absolute paths because WiX resolves -d variables relative to the .wxs file
	for %%i in ("..\..\Objeck-Build") do set OBJECK_BUILD=%%~fi

	if [%1] == [arm64] (
		set WIX_ARCH=arm64
		set WIX_SOURCEDIR=%OBJECK_BUILD%\release-arm64\%INSTALL_TARGET%
		set WIX_OUTPUT=%OBJECK_BUILD%\release-arm64\objeck-windows-arm64_0.0.0.msi
	)

	if [%1] == [x64] (
		set WIX_ARCH=x64
		set WIX_SOURCEDIR=%OBJECK_BUILD%\release-x64\%INSTALL_TARGET%
		set WIX_OUTPUT=%OBJECK_BUILD%\release-x64\objeck-windows-x64_0.0.0.msi
	)

	wix build -arch %WIX_ARCH% -o %WIX_OUTPUT% ^
		-d Platform=%WIX_ARCH% -d Version=0.0.0 ^
		-d SourceDir=%WIX_SOURCEDIR% ^
		-ext WixToolset.UI.wixext -ext WixToolset.Util.wixext ^
		..\utils\setup\objeck.wxs

	REM Try to sign MSI if certificate is available
	signtool sign /tr http://timestamp.sectigo.com /td sha256 /fd sha256 /a %WIX_OUTPUT% 2>nul
	if errorlevel 1 (
		echo Warning: Code signing failed or no certificate available - continuing with unsigned MSI
	) else (
		echo MSI signed successfully
	)

	REM Create ZIP
	if [%1] == [arm64] (
		powershell -Command "Compress-Archive -Path '..\..\Objeck-Build\release-arm64\%INSTALL_TARGET%' -DestinationPath '..\..\Objeck-Build\release-arm64\objeck-windows-arm64_0.0.0.zip' -Force"
	)

	if [%1] == [x64] (
		powershell -Command "Compress-Archive -Path '..\..\Objeck-Build\release-x64\%INSTALL_TARGET%' -DestinationPath '..\..\Objeck-Build\release-x64\objeck-windows-x64_0.0.0.zip' -Force"
	)
:end
