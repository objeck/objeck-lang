REM clean up
rmdir /s /q deploy_arm64
mkdir deploy_arm64
mkdir deploy_arm64\bin

REM build compiler
devenv ..\compiler\arm64_compiler\arm64_compiler.sln /rebuild "Release|VisualGDB"
copy ..\compiler\arm64_compiler\VisualGDB\Release\obc deploy_arm64\bin

REM build vm
devenv ..\vm\arm64_vm\arm64_vm.sln /rebuild "Release|VisualGDB"
copy ..\vm\arm64_vm\VisualGDB\Release\arm64_vm deploy_arm64\bin\obr

REM build debugger
devenv ..\debugger\arm64_debugger\arm64_debugger.sln /rebuild "Release|VisualGDB"
copy ..\debugger\arm64_debugger\VisualGDB\Release\arm64_debugger deploy_arm64\bin\obd

REM libraries
mkdir deploy_arm64\lib
mkdir deploy_arm64\lib\sdl
mkdir deploy_arm64\lib\sdl\fonts
copy ..\lib\*.obl deploy_arm64\lib
del deploy_arm64\lib\gtk2.obl
del /q deploy_arm64\bin\a.*
copy ..\vm\misc\*.pem deploy_arm64\lib

mkdir deploy_arm64\lib\native

REM openssl support
cd ..\lib\openssl
devenv arm_openssl64\arm_openssl64.sln /rebuild "Release|VisualGDB"
copy arm_openssl64\VisualGDB\Release\arm_openssl64.so ..\..\release\deploy_arm64\lib\native\libobjk_openssl.so

REM odbc support
cd ..\..\odbc\arm_odbc
devenv arm64_obc\arm64_odbc.sln /rebuild "Release|VisualGDB"
copy arm64_obc\VisualGDB\Release\arm64_odbc.so ..\..\release\deploy_arm64\lib\native\libobjk_odbc.so

REM sdl support
cd ..\..\sdl\arm_sdl
REM <= %VISUAL_GDB% /rebuild /config:Release arm_sdl.vgdbcmake
REM <= copy VisualGDB\Release\*.so ..\..\..\Release\deploy_arm64\lib\native
copy ..\lib\fonts\*.ttf ..\..\..\Release\deploy_arm64\lib\sdl\fonts

REM diags support
cd ..\..\diags\arm_diags
REM <= %VISUAL_GDB% /rebuild /config:Release arm_diags.vgdbcmake
REM <= copy VisualGDB\Release\*.so ..\..\..\Release\deploy_arm64\lib\native

cd ..\..\..\release

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
	%ZIP_BIN%\7z.exe a -r -ttar "%USERPROFILE%\Desktop\ReleaseARM64\objeck-lang-arm32.tar" ".\deploy_arm64\*"
	%ZIP_BIN%\7z.exe a -tgzip "%USERPROFILE%\Desktop\ReleaseARM64\objeck-lang-arm32.tgz" "%USERPROFILE%\Desktop\ReleaseARM64\objeck-lang-arm32.tar"
	del "%USERPROFILE%\Desktop\ReleaseARM64\objeck-lang-arm32.tar"
:end