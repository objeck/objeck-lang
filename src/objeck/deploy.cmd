set LATEX_BIN="D:\Program Files\MiKTeX 2.9\miktex\bin\x64"

rmdir /s /q deploy
rmdir /s /q fcgi_deploy

mkdir deploy
mkdir fcgi_deploy

REM update version information
powershell.exe -executionpolicy remotesigned -file  update_version.ps1
REM build binaries
devenv /rebuild Release objeck.sln
mkdir deploy\bin
copy Release\*.exe deploy\bin
del deploy\bin\obr_fcgi.exe
copy ..\compiler\*.obl deploy\bin
del deploy\bin\fcgi.obl
del deploy\bin\gtk2.obl
del /q deploy\bin\a.*
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
REM %LATEX_BIN%\pdflatex ..\..\docs\guide\objeck_lang.tex
REM copy objeck_lang.pdf deploy\doc 
copy ..\..\docs\guide\objeck_lang.pdf deploy\doc 
mkdir deploy\doc\syntax
copy ..\..\docs\syntax\* deploy\doc\syntax
copy ..\..\docs\readme.htm deploy
call code_doc.cmd
REM create and build fcgi
xcopy /e deploy\* fcgi_deploy
del fcgi_deploy\bin\obr.exe
del fcgi_deploy\bin\obd.exe
rmdir fcgi_deploy\binrmdir /s /q fcgi_deploy\doc
rmdir fcgi_deploy\binrmdir /s /q fcgi_deploy\examples
copy Release\obr_fcgi.exe fcgi_deploy\bin
copy redistrib\*.dll fcgi_deploy\bin
mkdir fcgi_deploy\examples
copy ..\compiler\web\* fcgi_deploy\examples
copy /y ..\..\docs\fcgi_readme.htm fcgi_deploy\readme.htm

REM finished
if [%1] NEQ [deploy] goto end
	rmdir /s /q "%HOMEDRIVE%%HOMEPATH%\Desktop\objeck-lang"
	mkdir "%HOMEDRIVE%%HOMEPATH%\Desktop\objeck-lang"
	xcopy /e deploy "%HOMEDRIVE%%HOMEPATH%\Desktop\objeck-lang"
:end