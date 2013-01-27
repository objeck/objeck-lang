set LATEX_BIN="C:\Program Files\MiKTeX 2.9\miktex\bin\x64"

rmdir /s /q deploy
mkdir deploy
REM build binaries
devenv /rebuild Release objeck.sln
mkdir deploy\bin
copy Release\*.exe deploy\bin
copy ..\compiler\*.obl deploy\bin
del deploy\bin\fcgi.obl
REM openssl support
mkdir deploy\bin\lib\openssl
cd ..\vm\lib\openssl\openssl
devenv /rebuild Release openssl.sln
copy Release\*.dll ..\..\..\..\objeck\deploy\bin\lib\openssl
copy ..\win32\bin\*.dll ..\..\..\..\objeck\deploy\bin
cd ..\..\..\..\objeck
REM odbc support
mkdir deploy\bin\lib\odbc
cd ..\vm\lib\odbc
devenv /rebuild Release odbc.sln
copy Release\*.dll ..\..\..\objeck\deploy\bin\lib\odbc
copy ..\win32\bin\*.dll ..\..\..\objeck\deploy\bin
cd ..\..\..\objeck
REM copy examples
xcopy /e ..\compiler\rc\* deploy\examples\
REM build and update docs
mkdir deploy\doc
%LATEX_BIN%\pdflatex ..\..\docs\guide\objeck_lang.tex
copy ..\..\docs\guide\objeck_lang.pdf deploy\doc
mkdir deploy\doc\syntax
copy ..\..\docs\syntax\* deploy\doc\syntax
copy ..\..\docs\readme.rtf deploy
REM finished
if [%1] NEQ [deploy] goto end
	rmdir /s /q %HOMEPATH%\Desktop\objeck-lang
	mkdir %HOMEPATH%\Desktop\objeck-lang
	xcopy /e deploy %HOMEPATH%\Desktop\objeck-lang
:end