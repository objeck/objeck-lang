REM cls && build objeck:2024.3.0 "\\wsl.localhost\Ubuntu\home\{user}\Documents\Code\objeck-lang" "/home/{user}/Documents/Code/objeck-lang"

rmdir /s /q deploy
del /q *.tar

wsl --cd %3/core/release -e git reset --hard origin/master
wsl --cd %3/core/release -e ./deploy_posix.sh 64

docker rm objeck_nightly
docker image prune --force --all

xcopy /e /q %2\core\release\deploy deploy\
docker build -t objeck:%1 . 
docker save -o tmp.tar objeck:%1
rename tmp.tar objeck-%1.tar

docker run -d --name objeck_nightly -it objeck:%1
docker run --rm -it objeck:%1 /bin/bash
REM docker attach objeck_nightly