@echo off

set OBJECK_ROOT=C:\Users\objec\Documents\Code\objeck-lang
set OBJECK_LIB_PATH=%OBJECK_ROOT%\core\release\deploy64\lib

obc -src %OBJECK_ROOT%\core\compiler\lib_src\openai.obs -lib json,net,misc -tar lib -dest %OBJECK_ROOT%\core\release\deploy64\lib\openai.obl

del /q *.obe
obc -src openai_images -lib openai,csv,net,json,misc
obr openai_images %1 %2 %3 %4