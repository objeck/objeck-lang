REM clean up

if not [%1]==[x64] if not [%1]==[arm64] (
	echo Windows targets are: 'x64' and 'arm64'
	goto end
)

IF "%VCINSTALLDIR%"=="" (
	echo Could not Visual Studio build environment
	goto end
)

REM mbedTLS and nghttp2 come from vcpkg, for both x64 and ARM64.
REM
REM They used to be binaries committed under lib\openssl\win\<arch>, and this
REM script hand-built mbedTLS 3.6.3 for ARM64 when they were missing. The
REM project files now resolve both through core\build\vcpkg.props, so the
REM committed copies are gone and the hand-build step with them.
set VCPKG_TRIPLET=x64-windows
if [%1] == [arm64] set VCPKG_TRIPLET=arm64-windows

set VCPKG_DIR=
if not "%VCPKG_ROOT%" == "" (
	if exist "%VCPKG_ROOT%\installed\%VCPKG_TRIPLET%\include\mbedtls\build_info.h" set VCPKG_DIR=%VCPKG_ROOT%
)
if "%VCPKG_DIR%" == "" (
	if exist "C:\vcpkg\installed\%VCPKG_TRIPLET%\include\mbedtls\build_info.h" set VCPKG_DIR=C:\vcpkg
)
REM %USERPROFILE%\vcpkg is the default `git clone` location, and where this
REM repo's ARM64 box keeps it. Kept in step with core\build\vcpkg.props.
if "%VCPKG_DIR%" == "" (
	if exist "%USERPROFILE%\vcpkg\installed\%VCPKG_TRIPLET%\include\mbedtls\build_info.h" set VCPKG_DIR=%USERPROFILE%\vcpkg
)

if "%VCPKG_DIR%" == "" (
	echo.
	echo ============================================================
	echo  ERROR: mbedTLS not found in vcpkg for %VCPKG_TRIPLET%
	echo ============================================================
	echo.
	echo Install it with:
	echo     vcpkg install mbedtls:%VCPKG_TRIPLET% nghttp2:%VCPKG_TRIPLET%
	echo.
	echo Run that from a directory with no vcpkg.json in it. Otherwise vcpkg
	echo switches to manifest mode, rejects the package arguments outright,
	echo and installs to a vcpkg_installed\ tree that nothing here reads.
	echo.
	echo Probed: %%VCPKG_ROOT%%, C:\vcpkg, %%USERPROFILE%%\vcpkg. Set VCPKG_ROOT
	echo if yours is elsewhere. Note that Visual Studio ships its own vcpkg and
	echo points VCPKG_ROOT at it; that copy usually has nothing installed.
	echo.
	goto end
)

echo Using vcpkg at %VCPKG_DIR% ^(%VCPKG_TRIPLET%^)

REM 7-Zip, used below to unpack the runtime archives that ship compressed.
REM Probed rather than hard-coded: the ZIP_BIN this replaces pointed at a
REM drive-relative path and was never referenced by anything, so nothing ever
REM noticed it was unusable.
set ZIP_EXE=
if exist "%ProgramFiles%\7-Zip\7z.exe" set ZIP_EXE="%ProgramFiles%\7-Zip\7z.exe"
if "%ZIP_EXE%"=="" if exist "%ProgramW6432%\7-Zip\7z.exe" set ZIP_EXE="%ProgramW6432%\7-Zip\7z.exe"
if "%ZIP_EXE%"=="" where 7z >nul 2>&1 && set ZIP_EXE=7z

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
if errorlevel 1 (
	echo.
	echo ============================================================
	echo  ERROR: update_version.ps1 failed - aborting deploy
	echo ============================================================
	exit /b 1
)

REM compiler, runtime and debugger
if [%1] == [arm64] (
	devenv objeck.sln /rebuild "Release|ARM64"
)

if [%1] == [x64] (
	devenv objeck.sln /rebuild "Release|x64"
)

if errorlevel 1 (
	echo.
	echo ============================================================
	echo  ERROR: objeck.sln build failed - aborting deploy
	echo ============================================================
	exit /b 1
)
REM Verify build output exists (catches Ctrl+C kills where devenv returns errorlevel 0)
if [%1] == [arm64] (
	if not exist "ARM64\Release\obr.exe" (
		echo.
		echo ============================================================
		echo  ERROR: objeck.sln build incomplete - was the build interrupted?
		echo ============================================================
		exit /b 1
	)
)
if [%1] == [x64] (
	if not exist "..\vm\release\win64\obr.exe" (
		echo.
		echo ============================================================
		echo  ERROR: objeck.sln build incomplete - was the build interrupted?
		echo ============================================================
		exit /b 1
	)
)

mkdir %TARGET%\bin
if [%1] == [arm64] (
	copy ARM64\Release\*.exe %TARGET%\bin
	REM Cross-compilation: use x64 host mt.exe (ARM64 mt.exe can't run on x64 host)
	REM WindowsSdkVerBinPath has trailing backslash, e.g., "C:\Program Files (x86)\Windows Kits\10\bin\10.0.26100.0\"
	"%WindowsSdkVerBinPath%x64\mt.exe" -manifest ..\vm\vs\manifest.xml -outputresource:%TARGET%\bin\obr.exe;1
	"%WindowsSdkVerBinPath%x64\mt.exe" -manifest ..\vm\vs\manifest.xml -outputresource:%TARGET%\bin\obi.exe;1
	for /d %%d in ("%VCToolsRedistDir%\arm64\Microsoft.VC*.CRT") do (
		copy "%%d\vcruntime140.dll" %TARGET%\bin
		copy "%%d\vcruntime140_1.dll" %TARGET%\bin
		REM msvcp140.dll too: the OpenCV and ONNX DLLs are C++ and import it.
		REM Shipping only the C runtime worked on x64 because the host had the
		REM redistributable installed anyway; a clean ARM64 machine does not.
		copy "%%d\msvcp140.dll" %TARGET%\bin
	)
)
if errorlevel 1 (
	echo.
	echo ============================================================
	echo  ERROR: ARM64 binary copy/manifest step failed - aborting deploy
	echo ============================================================
	exit /b 1
)

if [%1] == [x64] (
	copy ..\compiler\release\win64\*.exe %TARGET%\bin
	copy ..\repl\release\win64\*.exe %TARGET%\bin
	copy ..\vm\release\win64\*.exe %TARGET%\bin
	copy ..\debugger\release\win64\*.exe %TARGET%\bin
	REM Embed manifests AFTER copying binaries
	mt.exe -manifest ..\vm\vs\manifest.xml -outputresource:%TARGET%\bin\obr.exe;1
	mt.exe -manifest ..\vm\vs\manifest.xml -outputresource:%TARGET%\bin\obi.exe;1
	for /d %%d in ("%VCToolsRedistDir%\x64\Microsoft.VC*.CRT") do (
		copy "%%d\vcruntime140.dll" %TARGET%\bin
		copy "%%d\vcruntime140_1.dll" %TARGET%\bin
		REM msvcp140.dll too: the OpenCV and ONNX DLLs are C++ and import it.
		REM Shipping only the C runtime worked on x64 because the host had the
		REM redistributable installed anyway; a clean ARM64 machine does not.
		copy "%%d\msvcp140.dll" %TARGET%\bin
	)
)
if errorlevel 1 (
	echo.
	echo ============================================================
	echo  ERROR: x64 binary copy/manifest step failed - aborting deploy
	echo ============================================================
	exit /b 1
)

copy ..\lib\lame\win\%1\*.dll %TARGET%\bin
if errorlevel 1 (
	echo.
	echo ============================================================
	echo  ERROR: lame runtime DLL copy failed - aborting deploy
	echo ============================================================
	exit /b 1
)

REM nghttp2 runtime DLL (required by obr for HTTP/2 support).
REM Taken from vcpkg so the shipped DLL matches the import library linked
REM against. The committed copy it replaces was byte-identical to vcpkg's,
REM having come from there in the first place.
copy "%VCPKG_DIR%\installed\%VCPKG_TRIPLET%\bin\nghttp2.dll" %TARGET%\bin
if errorlevel 1 (
	echo.
	echo ============================================================
	echo  ERROR: nghttp2 runtime DLL copy failed - aborting deploy
	echo ============================================================
	exit /b 1
)

REM native launcher
if [%1] == [arm64] (
	cd ..\utils\launcher
	devenv native_launcher.sln /rebuild "Release|ARM64"
	if errorlevel 1 (
		echo.
		echo ============================================================
		echo  ERROR: native_launcher.sln build failed - aborting deploy
		echo ============================================================
		exit /b 1
	)
	if not exist "ARM64\Release\obn.exe" (
		echo.
		echo ============================================================
		echo  ERROR: native_launcher.sln build incomplete - was the build interrupted?
		echo ============================================================
		exit /b 1
	)
	copy ARM64\Release\obn.exe ..\..\release\%TARGET%\lib\native\misc
	copy ARM64\Release\obb.exe ..\..\release\%TARGET%\bin
	copy ..\..\vm\misc\config.prop ..\..\release\%TARGET%\lib\native\misc
	REM build updater. There is no .sln for this single project, so drive msbuild
	REM directly -- it is on PATH in a VS Developer Command Prompt and in CI via
	REM setup-msbuild. obu.vcxproj selects $(DefaultPlatformToolset), so it needs
	REM no toolset override on either VS2022 or VS2026.
	msbuild ..\updater\vs\obu.vcxproj /p:Configuration=Release /p:Platform=ARM64 /t:Rebuild /v:minimal /nologo
	if errorlevel 1 (
		echo.
		echo ============================================================
		echo  ERROR: obu.vcxproj build failed - aborting deploy
		echo ============================================================
		exit /b 1
	)
	if not exist "..\updater\vs\ARM64\Release\obu.exe" (
		echo.
		echo ============================================================
		echo  ERROR: obu.vcxproj build incomplete - was the build interrupted?
		echo ============================================================
		exit /b 1
	)
	copy ..\updater\vs\ARM64\Release\obu.exe ..\..\release\%TARGET%\bin
	cd ..\..\release
)

if [%1] == [x64] (
	cd ..\utils\launcher
	devenv native_launcher.sln /rebuild "Release|x64"
	if errorlevel 1 (
		echo.
		echo ============================================================
		echo  ERROR: native_launcher.sln build failed - aborting deploy
		echo ============================================================
		exit /b 1
	)
	if not exist "x64\Release\obn.exe" (
		echo.
		echo ============================================================
		echo  ERROR: native_launcher.sln build incomplete - was the build interrupted?
		echo ============================================================
		exit /b 1
	)
	copy x64\Release\obn.exe ..\..\release\%TARGET%\lib\native\misc
	copy x64\Release\obb.exe ..\..\release\%TARGET%\bin
	copy ..\..\vm\misc\config.prop ..\..\release\%TARGET%\lib\native\misc
	REM build updater -- see the ARM64 branch above for why this uses msbuild.
	msbuild ..\updater\vs\obu.vcxproj /p:Configuration=Release /p:Platform=x64 /t:Rebuild /v:minimal /nologo
	if errorlevel 1 (
		echo.
		echo ============================================================
		echo  ERROR: obu.vcxproj build failed - aborting deploy
		echo ============================================================
		exit /b 1
	)
	if not exist "..\updater\vs\x64\Release\obu.exe" (
		echo.
		echo ============================================================
		echo  ERROR: obu.vcxproj build incomplete - was the build interrupted?
		echo ============================================================
		exit /b 1
	)
	copy ..\updater\vs\x64\Release\obu.exe ..\..\release\%TARGET%\bin
	cd ..\..\release
)

REM libraries
del /q %TARGET%\bin\a.*
copy ..\vm\misc\*.pem %TARGET%\lib

REM crypto support (optional - requires mbedtls)
cd ..\lib\crypto

if [%1] == [arm64] (
	devenv crypto.sln /rebuild "Release|ARM64"
	if errorlevel 1 (
		echo.
		echo ============================================================
		echo  ERROR: crypto.sln build failed - aborting deploy
		echo ============================================================
		exit /b 1
	)
	if exist ARM64\Release\*.dll (
		copy ARM64\Release\*.dll ..\..\release\%TARGET%\lib\native
	) else (
		echo Warning: Crypto library DLL not found - skipping (mbedtls may not be available)
	)
)

if [%1] == [x64] (
	devenv crypto.sln /rebuild "Release|x64"
	if errorlevel 1 (
		echo.
		echo ============================================================
		echo  ERROR: crypto.sln build failed - aborting deploy
		echo ============================================================
		exit /b 1
	)
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
	if errorlevel 1 (
		echo.
		echo ============================================================
		echo  ERROR: lame.sln build failed - aborting deploy
		echo ============================================================
		exit /b 1
	)
	if not exist "ARM64\Release\libobjk_lame.dll" (
		echo.
		echo ============================================================
		echo  ERROR: lame.sln build incomplete - was the build interrupted?
		echo ============================================================
		exit /b 1
	)
	copy ARM64\Release\libobjk_lame.dll ..\..\release\%TARGET%\lib\native
)

if [%1] == [x64] (
	devenv lame.sln /rebuild "Release|x64"
	if errorlevel 1 (
		echo.
		echo ============================================================
		echo  ERROR: lame.sln build failed - aborting deploy
		echo ============================================================
		exit /b 1
	)
	if not exist "x64\Release\libobjk_lame.dll" (
		echo.
		echo ============================================================
		echo  ERROR: lame.sln build incomplete - was the build interrupted?
		echo ============================================================
		exit /b 1
	)
	copy x64\Release\libobjk_lame.dll ..\..\release\%TARGET%\lib\native
)
cd ..\..\release

REM app
cd ..\utils\WindowsApp
if [%1] == [arm64] (
	devenv AppLauncher.sln /rebuild "Release|ARM64"
	if errorlevel 1 (
		echo.
		echo ============================================================
		echo  ERROR: AppLauncher.sln build failed - aborting deploy
		echo ============================================================
		exit /b 1
	)
	if not exist "ARM64\Release\ObLauncher.exe" (
		echo.
		echo ============================================================
		echo  ERROR: AppLauncher.sln build incomplete - was the build interrupted?
		echo ============================================================
		exit /b 1
	)
	copy ARM64\Release\*.exe ..\..\release\%TARGET%\app
)

if [%1] == [x64] (
	devenv AppLauncher.sln /rebuild "Release|x64"
	if errorlevel 1 (
		echo.
		echo ============================================================
		echo  ERROR: AppLauncher.sln build failed - aborting deploy
		echo ============================================================
		exit /b 1
	)
	if not exist "x64\Release\ObLauncher.exe" (
		echo.
		echo ============================================================
		echo  ERROR: AppLauncher.sln build incomplete - was the build interrupted?
		echo ============================================================
		exit /b 1
	)
	copy x64\Release\*.exe ..\..\release\%TARGET%\app
)
cd ..\..\release

REM diags
cd ..\lib\diags
if [%1] == [arm64] (
	devenv diag.sln /rebuild "Release|ARM64"
	if errorlevel 1 (
		echo.
		echo ============================================================
		echo  ERROR: diag.sln build failed - aborting deploy
		echo ============================================================
		exit /b 1
	)
	if not exist "vs\Release\ARM64\libobjk_diags.dll" (
		echo.
		echo ============================================================
		echo  ERROR: diag.sln build incomplete - was the build interrupted?
		echo ============================================================
		exit /b 1
	)
	copy vs\Release\ARM64\*.dll* ..\..\release\%TARGET%\lib\native
)

if [%1] == [x64] (
	devenv diag.sln /rebuild "Release|x64"
	if errorlevel 1 (
		echo.
		echo ============================================================
		echo  ERROR: diag.sln build failed - aborting deploy
		echo ============================================================
		exit /b 1
	)
	if not exist "vs\Release\x64\libobjk_diags.dll" (
		echo.
		echo ============================================================
		echo  ERROR: diag.sln build incomplete - was the build interrupted?
		echo ============================================================
		exit /b 1
	)
	copy vs\Release\x64\*.dll ..\..\release\%TARGET%\lib\native
)
cd ..\..\release


REM odbc support
cd ..\lib\odbc
if [%1] == [arm64] (
	devenv odbc.sln /rebuild "Release|ARM64"
	if errorlevel 1 (
		echo.
		echo ============================================================
		echo  ERROR: odbc.sln build failed - aborting deploy
		echo ============================================================
		exit /b 1
	)
	if not exist "ARM64\Release\libobjk_odbc.dll" (
		echo.
		echo ============================================================
		echo  ERROR: odbc.sln build incomplete - was the build interrupted?
		echo ============================================================
		exit /b 1
	)
	copy ARM64\Release\*.dll ..\..\release\%TARGET%\lib\native
)

if [%1] == [x64] (
	devenv odbc.sln /rebuild "Release|x64"
	if errorlevel 1 (
		echo.
		echo ============================================================
		echo  ERROR: odbc.sln build failed - aborting deploy
		echo ============================================================
		exit /b 1
	)
	if not exist "Release\win64\libobjk_odbc.dll" (
		echo.
		echo ============================================================
		echo  ERROR: odbc.sln build incomplete - was the build interrupted?
		echo ============================================================
		exit /b 1
	)
	copy Release\win64\*.dll ..\..\release\%TARGET%\lib\native
)
cd ..\..\release

REM matrix support
cd ..\lib\matrix
if [%1] == [arm64] (
	devenv matrix.sln /rebuild "Release|ARM64"
	if errorlevel 1 (
		echo.
		echo ============================================================
		echo  ERROR: matrix.sln build failed - aborting deploy
		echo ============================================================
		exit /b 1
	)
	if not exist "Release\ARM64\libobjk_ml.dll" (
		echo.
		echo ============================================================
		echo  ERROR: matrix.sln build incomplete - was the build interrupted?
		echo ============================================================
		exit /b 1
	)
	copy Release\ARM64\*.dll ..\..\release\%TARGET%\lib\native
)

if [%1] == [x64] (
	devenv matrix.sln /rebuild "Release|x64"
	if errorlevel 1 (
		echo.
		echo ============================================================
		echo  ERROR: matrix.sln build failed - aborting deploy
		echo ============================================================
		exit /b 1
	)
	if not exist "Release\x64\libobjk_ml.dll" (
		echo.
		echo ============================================================
		echo  ERROR: matrix.sln build incomplete - was the build interrupted?
		echo ============================================================
		exit /b 1
	)
	copy Release\x64\*.dll ..\..\release\%TARGET%\lib\native
)
cd ..\..\release

REM opencv support
cd ..\lib\opencv
if [%1] == [arm64] (
	devenv opencv.sln /rebuild "Release|ARM64"
	if errorlevel 1 (
		echo.
		echo ============================================================
		echo  ERROR: opencv.sln build failed - aborting deploy
		echo ============================================================
		exit /b 1
	)
	if not exist "arm64\Release\libobjk_opencv.dll" (
		echo.
		echo ============================================================
		echo  ERROR: opencv.sln build incomplete - was the build interrupted?
		echo ============================================================
		exit /b 1
	)
	copy arm64\Release\libobjk_opencv.dll ..\..\release\%TARGET%\lib\native

	for %%f in (win\arm64\bin\opencv_*4.dll) do (
		copy /y %%f ..\..\release\%TARGET%\bin
	)
)

if [%1] == [x64] (
	devenv opencv.sln /rebuild "Release|x64"
	if errorlevel 1 (
		echo.
		echo ============================================================
		echo  ERROR: opencv.sln build failed - aborting deploy
		echo ============================================================
		exit /b 1
	)
	if not exist "x64\Release\libobjk_opencv.dll" (
		echo.
		echo ============================================================
		echo  ERROR: opencv.sln build incomplete - was the build interrupted?
		echo ============================================================
		exit /b 1
	)
	copy x64\Release\libobjk_opencv.dll ..\..\release\%TARGET%\lib\native

	if exist win\x64\bin\opencv_world4120.dll (
		copy /y win\x64\bin\opencv_world4120.dll ..\..\release\%TARGET%\bin
	) else (
		echo Warning: win\x64\bin\opencv_world4120.dll not found - OpenCV runtime unavailable
	)
	if exist win\x64\bin\opencv_videoio_ffmpeg4120_64.dll (
		copy /y win\x64\bin\opencv_videoio_ffmpeg4120_64.dll ..\..\release\%TARGET%\bin
	)
)
cd ..\..\release

REM onnx support
cd ..\lib\onnx

REM Restore NuGet packages before building
REM 1) Try VS-bundled nuget.exe  2) Try PATH (CI runners have it via Chocolatey)
REM 3) Download from nuget.org as last resort (needed when VS doesn't bundle it)
set NUGET_EXE=%VSINSTALLDIR%Common7\IDE\CommonExtensions\Microsoft\NuGet\nuget.exe
if not exist "%NUGET_EXE%" set NUGET_EXE=
if "%NUGET_EXE%"=="" (
	where nuget >nul 2>&1
	if not errorlevel 1 set NUGET_EXE=nuget
)
if "%NUGET_EXE%"=="" (
	echo nuget.exe not found in VS or PATH - downloading from nuget.org...
	powershell -Command "Invoke-WebRequest -Uri 'https://dist.nuget.org/win-x86-commandline/latest/nuget.exe' -OutFile '%TEMP%\nuget.exe' -UseBasicParsing" 2>nul
	if exist "%TEMP%\nuget.exe" set NUGET_EXE=%TEMP%\nuget.exe
)
if "%NUGET_EXE%"=="" (
	echo.
	echo ============================================================
	echo  ERROR: nuget.exe not found and download failed - aborting deploy
	echo  Install NuGet CLI: choco install nuget.commandline
	echo ============================================================
	exit /b 1
)
REM Retry the restore: nuget.org intermittently returns transient errors
REM (HTTP 502 Bad Gateway, dropped connections) on package downloads such as
REM Microsoft.AI.DirectML, which would otherwise abort an entire release build.
set NUGET_MAX_ATTEMPTS=5
set NUGET_ATTEMPT=0
:nuget_restore_retry
set /a NUGET_ATTEMPT+=1
"%NUGET_EXE%" restore onnx.sln
if not errorlevel 1 goto nuget_restore_ok
if %NUGET_ATTEMPT% geq %NUGET_MAX_ATTEMPTS% (
	echo.
	echo ============================================================
	echo  ERROR: NuGet restore failed after %NUGET_MAX_ATTEMPTS% attempts - aborting deploy
	echo ============================================================
	exit /b 1
)
echo NuGet restore attempt %NUGET_ATTEMPT% of %NUGET_MAX_ATTEMPTS% failed ^(transient nuget.org error?^) - retrying in 15s...
powershell -Command "Start-Sleep -Seconds 15" >nul 2>&1
goto nuget_restore_retry
:nuget_restore_ok

if [%1] == [arm64] (
	devenv onnx.sln /rebuild "Release-QNN|ARM64"
	if errorlevel 1 (
		echo.
		echo ============================================================
		echo  ERROR: onnx.sln build failed - aborting deploy
		echo ============================================================
		exit /b 1
	)
	if not exist "ARM64\Release-QNN\libobjk_onnx.dll" (
		echo.
		echo ============================================================
		echo  ERROR: onnx.sln build incomplete - was the build interrupted?
		echo ============================================================
		exit /b 1
	)
	copy ARM64\Release-QNN\libobjk_onnx.dll ..\..\release\%TARGET%\lib\native

	if exist eq\qnn\win\onnx\arm64\bin (
		copy /y eq\qnn\win\onnx\arm64\bin\*.dll ..\..\release\%TARGET%\bin
		REM onnxruntime.dll ships COMPRESSED and nothing ever unpacked it, so the
		REM copy above moved the Qnn and provider DLLs and left the core runtime
		REM behind. libobjk_onnx.dll imports it, so loading failed with error 126
		REM on a deploy tree that otherwise looked complete.
		if exist eq\qnn\win\onnx\arm64\bin\onnxruntime.7z (
			if "%ZIP_EXE%"=="" (
				echo.
				echo ============================================================
				echo  ERROR: 7-Zip not found, cannot unpack onnxruntime.7z - aborting deploy
				echo ============================================================
				exit /b 1
			)
			%ZIP_EXE% x -y -o..\..\release\%TARGET%\bin eq\qnn\win\onnx\arm64\bin\onnxruntime.7z
			if errorlevel 1 (
				echo.
				echo ============================================================
				echo  ERROR: onnxruntime.7z extraction failed - aborting deploy
				echo ============================================================
				exit /b 1
			)
		)
		REM Fail here rather than ship a tree whose ONNX library cannot load. This
		REM checks the RUNTIME is present, not that any accelerator is: the QNN
		REM build targets Qualcomm Hexagon, and a machine without one is a valid
		REM deploy target that simply falls back.
		if not exist ..\..\release\%TARGET%\bin\onnxruntime.dll (
			echo.
			echo ============================================================
			echo  ERROR: onnxruntime.dll missing from bin - aborting deploy
			echo ============================================================
			exit /b 1
		)
	) else (
		echo.
		echo ============================================================
		echo  ERROR: ONNX QNN runtime tree not found for arm64 - aborting deploy
		echo ============================================================
		exit /b 1
	)
)

if [%1] == [x64] (
	devenv onnx.sln /rebuild "Release-DML|x64"
	if errorlevel 1 (
		echo.
		echo ============================================================
		echo  ERROR: onnx.sln build failed - aborting deploy
		echo ============================================================
		exit /b 1
	)
	if not exist "x64\Release-DML\libobjk_onnx.dll" (
		echo.
		echo ============================================================
		echo  ERROR: onnx.sln build incomplete - was the build interrupted?
		echo ============================================================
		exit /b 1
	)
	copy x64\Release-DML\libobjk_onnx.dll ..\..\release\%TARGET%\lib\native

	if exist packages\Microsoft.ML.OnnxRuntime.DirectML.1.22.1\runtimes\win-x64\native (
		copy /y packages\Microsoft.ML.OnnxRuntime.DirectML.1.22.1\runtimes\win-x64\native\*.dll ..\..\release\%TARGET%\bin
	) else (
		echo Warning: OnnxRuntime.DirectML nuget packages not found - ONNX runtime unavailable
	)
	REM DirectML.dll is an OS component on Windows 10 1903+, and Windows ships a
	REM NEWER build than the vendored package (OS 1.15.5 vs package 1.15.4 as of
	REM 2026-08). Windows loads an app-local DLL ahead of System32, so copying the
	REM package unconditionally DOWNGRADES a working install -- the likely cause of
	REM GPU inference failing at a Gather node. Ship the redistributable only when
	REM the OS does not provide one.
	if exist "%SystemRoot%\System32\DirectML.dll" (
		echo Using the OS DirectML.dll ^(System32^) - not shipping the older redistributable
		if exist ..\..\release\%TARGET%\bin\DirectML.dll del /q ..\..\release\%TARGET%\bin\DirectML.dll
	) else (
		if exist packages\Microsoft.AI.DirectML.1.15.4\bin\x64-win\DirectML.dll (
			echo No OS DirectML.dll - shipping the vendored redistributable
			copy /y packages\Microsoft.AI.DirectML.1.15.4\bin\x64-win\DirectML.dll ..\..\release\%TARGET%\bin
		) else (
			echo Warning: no OS DirectML.dll and no vendored package - GPU inference unavailable
		)
	)
)
cd ..\..\release

REM sdl support
cd ..\lib\sdl
if [%1] == [arm64] (
	REM sdl
	devenv sdl\sdl.sln /rebuild "Release|ARM64"
	if errorlevel 1 (
		echo.
		echo ============================================================
		echo  ERROR: sdl.sln build failed - aborting deploy
		echo ============================================================
		exit /b 1
	)
	if not exist "sdl\Release\arm64\libobjk_sdl.dll" (
		echo.
		echo ============================================================
		echo  ERROR: sdl.sln build incomplete - was the build interrupted?
		echo ============================================================
		exit /b 1
	)
	copy sdl\Release\arm64\*.dll ..\..\release\%TARGET%\lib\native
	copy lib\fonts\*.ttf ..\..\release\%TARGET%\lib\sdl\fonts
	REM SDL2's own runtime DLLs go to bin, NOT next to libobjk_sdl.dll.
	REM Windows resolves a dynamically-loaded DLL's imports against the
	REM EXECUTABLE's directory; the directory holding the DLL itself is never
	REM searched. That is why every other native library's dependencies --
	REM libmp3lame, nghttp2, opencv_world, onnxruntime -- already live in bin.
	REM Left in lib\sdl they resolve only when something has put lib\sdl on
	REM PATH, and nothing shipped to a user does: the MSI puts only bin on
	REM PATH. lib\sdl\fonts stays where it is -- Overlay loads it by path.
	copy lib\arm64\*.dll ..\..\release\%TARGET%\bin
)

if [%1] == [x64] (
	REM sdl
	devenv sdl\sdl.sln /rebuild "Release|x64"
	if errorlevel 1 (
		echo.
		echo ============================================================
		echo  ERROR: sdl.sln build failed - aborting deploy
		echo ============================================================
		exit /b 1
	)
	if not exist "sdl\Release\x64\libobjk_sdl.dll" (
		echo.
		echo ============================================================
		echo  ERROR: sdl.sln build incomplete - was the build interrupted?
		echo ============================================================
		exit /b 1
	)
	copy sdl\Release\x64\*.dll ..\..\release\%TARGET%\lib\native
	copy lib\fonts\*.ttf ..\..\release\%TARGET%\lib\sdl\fonts
	REM bin, not lib\sdl -- see the note on the arm64 copy above.
	copy lib\x64\*.dll ..\..\release\%TARGET%\bin
)
cd ..\..\release

REM copy examples
mkdir %TARGET%\examples\

mkdir %TARGET%\examples\media\
del  /s /q ..\..\programs\*.obe
xcopy /e ..\..\programs\deploy\*.obs %TARGET%\examples\
REM The OpenGL examples live in programs\examples rather than programs\deploy,
REM so no distribution ever carried them. They find the bundled font relative to
REM either bin or here, so they run from where they land.
mkdir %TARGET%\examples\opengl
copy ..\..\programs\examples\gl_*.obs %TARGET%\examples\opengl
copy ..\..\programs\examples\cube_gl.obs %TARGET%\examples\opengl
copy ..\..\programs\examples\gl_crystal.obj %TARGET%\examples\opengl
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
	call "%~dp0code_doc64.cmd" %1 deploy
	rmdir /s /q %1
) else (
	echo Skipping code_doc for ARM64 cross-compilation - using pre-built API docs
	mkdir %TARGET%\doc\api
	powershell -Command "Expand-Archive -Path '..\..\docs\api.zip' -DestinationPath '%TARGET%\doc' -Force"
	if errorlevel 1 (
		echo.
		echo ============================================================
		echo  ERROR: API docs extraction failed - aborting deploy
		echo ============================================================
		exit /b 1
	)
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
	if errorlevel 1 (
		echo.
		echo ============================================================
		echo  ERROR: WiX MSI build failed - aborting deploy
		echo ============================================================
		exit /b 1
	)

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
	if errorlevel 1 (
		echo.
		echo ============================================================
		echo  ERROR: ZIP creation failed - aborting deploy
		echo ============================================================
		exit /b 1
	)
:end
