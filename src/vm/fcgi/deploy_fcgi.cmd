echo off

rmdir /s /q deploy
mkdir deploy
mkdir deploy\bin
mkdir deploy\lib\objeck-lang

if "%1" == "debug" (
	copy Debug\obr_fcgi.exe deploy\bin
	copy Debug\*.pdb deploy\bin
	copy Debug\fcgi_lib.dll deploy\lib\objeck-lang
) else (
	copy Release\obr_fcgi.exe deploy\bin
	copy Release\fcgi_lib.dll deploy\lib\objeck-lang
)

copy ..\..\compiler\*.obl deploy\bin
copy ..\..\lib\openssl\win32\bin\*.dll deploy\bin
copy ..\..\lib\fcgi\windows\lib\*.dll deploy\bin