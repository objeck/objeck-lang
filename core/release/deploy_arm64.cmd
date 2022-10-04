REM clean up
rmdir /s /q deploy_arm64
mkdir deploy_arm64
mkdir deploy_arm64\bin
mkdir deploy_arm64\lib
mkdir deploy_arm64\lib\native
mkdir deploy_arm64\lib\native\misc

REM build compiler
devenv ..\compiler\arm64_compiler\arm64_compiler.sln /rebuild "Release|VisualGDB"
copy ..\compiler\arm64_compiler\VisualGDB\Release\obc deploy_arm64\bin

REM build vm
devenv ..\vm\arm64_vm\arm64_vm.sln /rebuild "Release|VisualGDB"
copy ..\vm\arm64_vm\VisualGDB\Release\arm64_vm deploy_arm64\bin\obr

REM build debugger
devenv ..\debugger\arm64_debugger\arm64_debugger.sln /rebuild "Release|VisualGDB"
copy ..\debugger\arm64_debugger\VisualGDB\Release\arm64_debugger deploy_arm64\bin\obd

REM portable runtime
devenv ..\native_launcher\arm_native_launcher\arm_native_launcher.sln /rebuild "Release|VisualGDB"
copy ..\native_launcher\arm_native_launcher\VisualGDB\Release\obb deploy_arm64\bin\obb
copy ..\native_launcher\arm_native_launcher\VisualGDB\Release\obn deploy_arm64\lib\native\misc\obn
copy ..\vm\misc\config.prop deploy_arm64\lib\native\misc

REM libraries
mkdir deploy_arm64\lib\sdl
mkdir deploy_arm64\lib\sdl\fonts
copy ..\lib\*.obl deploy_arm64\lib
del deploy_arm64\lib\gtk2.obl
del /q deploy_arm64\bin\a.*
copy ..\vm\misc\*.pem deploy_arm64\lib

REM native libraries
cd ..\lib\linux_arm64
%ZIP_BIN%\7z.exe x linux_arm64_native.tgz
%ZIP_BIN%\7z.exe x linux_arm64_native.tar
copy native\*.so ..\..\release\deploy_arm64\lib\native
rmdir /s /q native
del /q *.tar

cd ..\..\release

REM copy examples
mkdir deploy_arm64\examples\
mkdir deploy_arm64\examples\doc\
mkdir deploy_arm64\examples\tiny\
set ZIP_BIN="\Program Files\7-Zip"

mkdir deploy_arm64\examples\media\
del  /s /q ..\..\programs\*.obe
xcopy /e ..\..\programs\deploy\*.obs deploy_arm64\examples\
xcopy /e ..\..\programs\deploy\media\*.png deploy_arm64\examples\media\
xcopy /e ..\..\programs\deploy\media\*.wav deploy_arm64\examples\media\
xcopy /e ..\..\programs\doc\* deploy_arm64\examples\doc\
xcopy /e ..\..\programs\tiny\* deploy_arm64\examples\tiny\
del  /s /q ..\..\programs\tiny\*.obe
del  /s /q ..\..\programs\tiny\*.e

REM build and update docs
mkdir deploy_arm64\doc 
mkdir deploy_arm64\doc\syntax
xcopy /e ..\..\docs\syntax\* deploy_arm64\doc\syntax
copy ..\..\docs\readme.html deploy_arm64
copy ..\..\LICENSE deploy_arm64

REM api docs
%ZIP_BIN%\7z.exe e ..\..\docs\api.zip -odeploy_arm64\doc\api

REM finished
if [%1] NEQ [deploy] goto end
	rmdir /s /q "%USERPROFILE%\Desktop\ReleaseARM64"
	mkdir "%USERPROFILE%\Desktop\ReleaseARM64"
	mkdir "%USERPROFILE%\Desktop\ReleaseARM64\objeck-lang"
	xcopy /e "deploy_arm64\" "%USERPROFILE%\Desktop\ReleaseARM64\objeck-lang\"
	%ZIP_BIN%\7z.exe a -r -ttar "%USERPROFILE%\Desktop\ReleaseARM64\objeck-lang-arm64.tar" "%USERPROFILE%\Desktop\ReleaseARM64\objeck-lang"
	%ZIP_BIN%\7z.exe a -tgzip "%USERPROFILE%\Desktop\ReleaseARM64\objeck-lang-arm64.tgz" "%USERPROFILE%\Desktop\ReleaseARM64\objeck-lang-arm64.tar"
	del "%USERPROFILE%\Desktop\ReleaseARM64\objeck-lang-arm64.tar"
:end