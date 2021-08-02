@echo off

set PATH=%PATH%;..\..\release\deploy64\bin
set OBJECK_LIB_PATH=..\..\release\deploy64\lib
obc -src ..\..\compiler\lib_src\diags.obs -tar lib -dest %OBJECK_LIB_PATH%\diags.obl
copy /y %OBJECK_LIB_PATH%\diags.obl ..
copy /y Debug\win64\libobjk_diags.dll %OBJECK_LIB_PATH%\native
