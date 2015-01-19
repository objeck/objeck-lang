set LATEX_BIN="D:\Program Files\MiKTeX 2.9\miktex\bin\x64"

rmdir /s /q deploy
mkdir deploy

REM update version information
Powershell.exe -executionpolicy remotesigned -File  update_version.ps1
REM build binaries
devenv /rebuild Release objeck.sln
mkdir deploy\bin
copy Release\*.exe deploy\bin
copy ..\compiler\*.obl deploy\bin
del deploy\bin\fcgi.obl
del deploy\bin\gtk2.obl
REM openssl support
mkdir deploy\lib
mkdir deploy\lib\objeck-lang
cd ..\lib\openssl\openssl
devenv /rebuild Release openssl.sln
copy Release\*.dll ..\..\..\objeck\deploy\lib\objeck-lang
copy ..\win32\bin\*.dll ..\..\..\objeck\deploy\bin
cd ..\..\..\objeck
REM odbc support
cd ..\lib\odbc
devenv /rebuild Release odbc.sln
copy Release\*.dll ..\..\objeck\deploy\lib\objeck-lang
cd ..\..\objeck
REM copy examples
xcopy /e ..\compiler\rc\* deploy\examples\
REM build and update docs
mkdir deploy\doc
%LATEX_BIN%\pdflatex ..\..\docs\guide\objeck_lang.tex
copy objeck_lang.pdf deploy\doc 
copy objeck_lang.pdf ..\..\docs\guide\
mkdir deploy\doc\syntax
copy ..\..\docs\syntax\* deploy\doc\syntax
copy ..\..\docs\readme.htm deploy
call code_doc.cmd
REM finished
if [%1] NEQ [deploy] goto end
	rmdir /s /q "%HOMEDRIVE%%HOMEPATH%\Desktop\objeck-lang"
	mkdir "%HOMEDRIVE%%HOMEPATH%\Desktop\objeck-lang"
	xcopy /e deploy "%HOMEDRIVE%%HOMEPATH%\Desktop\objeck-lang"
:end