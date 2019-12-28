REM clean up
rmdir /s /q deploy_arm
mkdir deploy_arm

SET VISUAL_GDB="\Program Files (x86)\Sysprogs\VisualGDB\VisualGDB.exe"

REM build compiler
%VISUAL_GDB% /rebuild /config:Release ..\compiler\arm_compiler\arm_compiler\arm_compiler.vgdbcmake
mkdir deploy_arm\bin
copy ..\compiler\arm_compiler\arm_compiler\VisualGDB\Release\obc deploy_arm\bin

REM build vm
%VISUAL_GDB% /rebuild /config:Release ..\vm\arm_vm\arm_vm\arm_vm.vgdbcmake
mkdir deploy_arm\bin
copy ..\vm\arm_vm\arm_vm\VisualGDB\Release\obr deploy_arm\bin

REM build debugger
%VISUAL_GDB% /rebuild /config:Release ..\debugger\arm_debugger\arm_debugger.vgdbcmake
mkdir deploy_arm\bin
copy ..\debugger\arm_debugger\VisualGDB\Release\obd deploy_arm\bin

REM libraries
mkdir deploy_arm\lib
mkdir deploy_arm\lib\sdl
mkdir deploy_arm\lib\sdl\fonts
copy ..\lib\*.obl deploy_arm\lib
del deploy_arm\lib\gtk2.obl
del /q deploy_arm\bin\a.*
copy ..\vm\misc\*.pem deploy_arm\lib

REM openssl support
mkdir deploy_arm\lib\native
cd ..\lib\openssl\arm_openssl
%VISUAL_GDB% /rebuild /config:Release arm_openssl.vgdbcmake
copy VisualGDB\Release\*.so ..\..\..\Release\deploy_arm\lib\native

REM odbc support
cd ..\..\odbc\arm_odbc
%VISUAL_GDB% /rebuild /config:Release arm_odbc.vgdbcmake
copy VisualGDB\Release\*.so ..\..\..\Release\deploy_arm\lib\native

REM sdl support
cd ..\..\sdl\arm_sdl
%VISUAL_GDB% /rebuild /config:Release arm_sdl.vgdbcmake
copy VisualGDB\Release\*.so ..\..\..\Release\deploy_arm\lib\native
copy ..\lib\fonts\*.ttf ..\..\..\Release\deploy_arm\lib\sdl\fonts
cd ..\..\..\release

REM copy examples
mkdir deploy_arm\examples\
mkdir deploy_arm\examples\doc\
mkdir deploy_arm\examples\tiny\
set ZIP_BIN="\Program Files\7-Zip"

mkdir deploy_arm\examples\media\
del  /s /q ..\..\programs\*.obe
xcopy /e ..\..\programs\deploy\*.obs deploy_arm\examples\
xcopy /e ..\..\programs\deploy\media\*.png deploy_arm\examples\media\
xcopy /e ..\..\programs\deploy\media\*.wav deploy_arm\examples\media\
xcopy /e ..\..\programs\doc\* deploy_arm\examples\doc\
xcopy /e ..\..\programs\tiny\* deploy_arm\examples\tiny\
del  /s /q ..\..\programs\tiny\*.obe
del  /s /q ..\..\programs\tiny\*.e

REM build and update docs
mkdir deploy_arm\doc 
mkdir deploy_arm\doc\syntax
xcopy /e ..\..\docs\syntax\* deploy_arm\doc\syntax
copy ..\..\docs\readme.html deploy_arm
copy ..\..\LICENSE deploy_arm

REM api docs
%ZIP_BIN%\7z.exe e ..\..\docs\api.zip -odeploy_arm\doc\api

REM finished
if [%1] NEQ [deploy] goto end
	rmdir /s /q "%USERPROFILE%\Desktop\ReleaseARM"
	mkdir "%USERPROFILE%\Desktop\ReleaseARM"
	%ZIP_BIN%\7z.exe a -r -ttar "%USERPROFILE%\Desktop\ReleaseARM\objeck-lang-arm32.tar" ".\deploy_arm\*"
	%ZIP_BIN%\7z.exe a -tgzip "%USERPROFILE%\Desktop\ReleaseARM\objeck-lang-arm32.tgz" "%USERPROFILE%\Desktop\ReleaseARM\objeck-lang-arm32.tar"
	del "%USERPROFILE%\Desktop\ReleaseARM\objeck-lang-arm32.tar"
:end