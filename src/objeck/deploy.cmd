rmdir /s /q deploy
mkdir deploy
devenv /rebuild Release objeck.sln
copy Release\*.exe deploy
copy ..\compiler\*.obl deploy
xcopy /e ..\compiler\rc\* deploy\examples\
cd deploy
