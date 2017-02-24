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
mkdir deploy\lib
copy ..\lib\*.obl deploy\lib
del deploy\lib\gtk2.obl
del deploy\lib\sdl.obl
del deploy\lib\db.obl
del /q deploy\bin\a.*
copy ..\vm\misc\*.pem deploy\lib
REM openssl support
mkdir deploy\lib\native
cd ..\lib\openssl\openssl
devenv /rebuild Release openssl.sln
copy Release\*.dll ..\..\..\objeck\deploy\lib\native
copy ..\win32\bin\*.dll ..\..\..\objeck\deploy\bin
cd ..\..\..\objeck
REM odbc support
cd ..\lib\odbc
devenv /rebuild Release odbc.sln
copy Release\*.dll ..\..\objeck\deploy\lib\native
cd ..\..\objeck
REM copy examples
mkdir deploy\examples\
mkdir deploy\examples\doc\
mkdir deploy\examples\tiny\
xcopy /e ..\compiler\programs\deploy\*.obs deploy\examples\
xcopy /e ..\compiler\programs\doc\* deploy\examples\doc\
xcopy /e ..\compiler\programs\tiny\* deploy\examples\tiny\
del  /s /q ..\compiler\programs\tiny\*.obe
del  /s /q ..\compiler\programs\tiny\*.e
REM build and update docs
mkdir deploy\doc 
copy ..\..\docs\guide\objeck_lang.pdf deploy\doc 
mkdir deploy\doc\syntax
copy ..\..\docs\syntax\* deploy\doc\syntax
copy ..\..\docs\readme.htm deploy
call code_doc.cmd
REM create and build fcgi
copy ..\lib\fcgi.obl deploy\lib
xcopy /e deploy\* deploy_fcgi
del deploy_fcgi\bin\obc.exe
del deploy_fcgi\bin\obd.exe
rmdir /s /q deploy_fcgi\doc
rmdir /s /q deploy_fcgi\examples
copy Release\obr_fcgi.exe deploy_fcgi\bin
copy ..\lib\fcgi\windows\lib\*.dll deploy_fcgi\bin
copy redistrib\*.dll deploy_fcgi\bin
copy Release\libobjk_fcgi.dll deploy_fcgi\lib\native
mkdir deploy_fcgi\examples
copy ..\compiler\programs\web\* deploy_fcgi\examples
copy /y ..\..\docs\fcgi_readme.htm deploy_fcgi\readme.htm
mkdir deploy_fcgi\fcgi_readme_files
copy ..\..\docs\fcgi_readme_files\* deploy_fcgi\fcgi_readme_files

REM finished
if [%1] NEQ [deploy] goto end
	rmdir /s /q "%HOMEDRIVE%%HOMEPATH%\Desktop\objeck-lang"
	mkdir "%HOMEDRIVE%%HOMEPATH%\Desktop\objeck-lang"
	xcopy /e deploy "%HOMEDRIVE%%HOMEPATH%\Desktop\objeck-lang"
:end
