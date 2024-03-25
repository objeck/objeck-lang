REM cls && build 2024.3.0 "\\wsl.localhost\Ubuntu\home\{user}\Documents\Code\objeck-lang" /home/{user}/Documents/Code/objeck-lang

@echo off

set arg_count=0
for %%x in (%*) do set /a arg_count += 1

if not "%arg_count%"=="3" (
	goto usage
)

set ZIP_BIN="\Program Files\7-Zip\7z.exe"

wsl --cd %3/core/release -e git reset --hard origin/master
wsl --cd %3/core/release -e ./deploy_posix.sh 64

docker rm objeck_nightly
docker image prune --force --all

xcopy /e /q %2\core\release\deploy deploy\
docker build -t objeck:%1 . 
docker save -o tmp.tar objeck:%1
rename tmp.tar objeck-%1.tar
%ZIP_BIN% a -ttar -so objeck-%1.tar | %ZIP_BIN% a -si objeck-%1.tgz

rmdir /s /q deploy
del /q *.tar

docker run -d --name objeck_nightly -it objeck:%1
REM docker run --rm -it objeck:%1 /bin/bash
goto end

:usage
echo Usage: [version] [local_objeck_path] [linx_objeck_path]

:end