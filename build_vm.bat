@echo off
call "C:\Program Files\Microsoft Visual Studio\2022\Professional\VC\Auxiliary\Build\vcvarsall.bat" amd64
cd /d "C:\Users\objec\Documents\Code\objeck-lang\core\release"
MSBuild objeck.sln /t:vm /p:Configuration=Release /p:Platform=x64 /m /v:minimal
