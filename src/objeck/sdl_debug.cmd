@echo off

rmdir /s /q ..\vm\lib
del ..\vm\*.dll

cd deploy\bin
obc -src 'C:\Users\Randy\Documents\Code\objeck-lang\src\compiler\lib_src\sdl.obs' -lib collect.obl -tar lib -dest sdl.obl
obc -src 'C:\Users\Randy\Documents\Code\objeck-lang\src\compiler\test_src\sdl\sdl0.obs' -lib sdl.obl,collect.obl -dest ..\a.obe
cd ..\..

copy ..\lib\sdl\lib\x86\SDL2.dll deploy\bin
copy ..\compiler\sdl.obl deploy\bin
copy ..\lib\sdl\sdl\release\*.dll deploy\lib\objeck-lang
xcopy /e deploy\lib\objeck-lang\* ..\vm\lib\objeck-lang\*
copy ..\lib\sdl\lib\x86\SDL2.dll ..\vm