set CC=cl
set LIB=lib_foo
set DEPLOY_DIR=deploy64

set PATH=%PATH%;..\..\..\core\release\%DEPLOY_DIR%\bin
set OBJECK_LIB_PATH=..\..\..\core\release\%DEPLOY_DIR%\lib

del /q *.o *.dll *.obe
del /q ..\..\..\core\release\%DEPLOY_DIR%\lib\native\%LIB%.dll

 %CC% %LIB%.cpp /LD /EHsc -I ..\..\..\core\lib\zlib\win -I ..\..\..\core\lib\openssl\win\include /link /LIBPATH:"C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools\VC\Tools\MSVC\14.35.32215\lib\onecore\x64" /LIBPATH:"C:\Program Files (x86)\Windows Kits\10\Lib\10.0.22000.0\ucrt\x64" /LIBPATH:"C:\Program Files (x86)\Windows Kits\10\Lib\10.0.22000.0\um\x64" 
copy /y %LIB%.dll ..\..\..\core\release\%DEPLOY_DIR%\lib\native

obc foo && obr foo