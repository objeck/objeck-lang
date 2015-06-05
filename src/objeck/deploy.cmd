set LATEX_BIN="D:\Program Files\MiKTeX 2.9\miktex\bin\x64"

rmdir /s /q deploy
rmdir /s /q deploy_fcgi

mkdir deploy
mkdir deploy_fcgi

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
xcopy /e deploy\* deploy_fcgi
del deploy_fcgi\bin\obr.exe
del deploy_fcgi\bin\obd.exe
rmdir deploy_fcgi\binrmdir /s /q deploy_fcgi\doc
rmdir deploy_fcgi\binrmdir /s /q deploy_fcgi\examples
copy Release\obr_fcgi.exe deploy_fcgi\bin
copy ..\compiler\fcgi.obl deploy_fcgi\bin
copy ..\lib\fcgi\windows\lib\*.dll deploy_fcgi\bin
copy redistrib\*.dll deploy_fcgi\bin
copy Release\libobjk_fcgi.dll deploy_fcgi\lib\objeck-lang
mkdir deploy_fcgi\examples
copy ..\compiler\web\* deploy_fcgi\examples
copy /y ..\..\docs\fcgi_readme.htm deploy_fcgi\readme.htm

REM finished
if [%1] NEQ [deploy] goto end
	rmdir /s /q "%HOMEDRIVE%%HOMEPATH%\Desktop\objeck-lang"
	mkdir "%HOMEDRIVE%%HOMEPATH%\Desktop\objeck-lang"
	xcopy /e deploy "%HOMEDRIVE%%HOMEPATH%\Desktop\objeck-lang"
:end