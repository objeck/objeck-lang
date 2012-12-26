rmdir /s /q deploy
mkdir deploy
devenv /rebuild Release objeck.sln
copy Release\*.exe deploy
copy ..\compiler\*.obl deploy
copy ..\vm\lib\openssl\win32\bin\*.dll deploy
xcopy /e ..\compiler\rc\* deploy\examples\
cd deploy
