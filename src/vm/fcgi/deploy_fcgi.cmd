echo off

set VS_ROOT="C:\Program Files (x86)\Microsoft Visual Studio 12.0\VC"

rmdir /s /q deploy
mkdir deploy
mkdir deploy\bin
mkdir deploy\lib\objeck-lang

if "%1" == "debug" (
	copy Debug\obr_fcgi.exe deploy\bin
	copy Debug\fcgi_lib.dll deploy\lib\objeck-lang
	copy Debug\*.pdb deploy\bin
	copy %VS_ROOT%\redist\Debug_NonRedist\x86\Microsoft.VC120.DebugCRT\*.dll deploy\bin
) else (
	copy Release\obr_fcgi.exe deploy\bin
	copy Release\fcgi_lib.dll deploy\lib\objeck-lang
	copy %VS_ROOT%\redist\x86\Microsoft.VC120.CRT\*.dll deploy\bin	
	REM copy Release\*.pdb deploy\bin
)

copy ..\..\compiler\*.obl deploy\bin
copy ..\..\lib\openssl\win32\bin\*.dll deploy\bin
copy ..\..\lib\fcgi\windows\lib\*.dll deploy\bin