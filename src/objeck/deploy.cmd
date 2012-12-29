rmdir /s /q deploy
mkdir deploy
REM build binaries
devenv /rebuild Release objeck.sln
copy Release\*.exe deploy
copy ..\compiler\*.obl deploy
del deploy\fcgi.obl
REM openssl support
mkdir deploy\lib\openssl
cd ..\vm\lib\openssl\openssl
devenv /rebuild Release openssl.sln
copy Release\*.dll ..\..\..\..\objeck\deploy\lib\openssl
copy ..\win32\bin\*.dll ..\..\..\..\objeck\deploy
cd ..\..\..\..\objeck
REM odbc support
mkdir deploy\lib\odbc
cd ..\vm\lib\odbc
devenv /rebuild Release odbc.sln
copy Release\*.dll ..\..\..\objeck\deploy\lib\odbc
copy ..\win32\bin\*.dll ..\..\..\objeck\deploy
cd ..\..\..\objeck
REM copy examples
xcopy /e ..\compiler\rc\* deploy\examples\
cd deploy