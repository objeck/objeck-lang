REM cls && build "objeck:2024.3.0" "\\wsl.localhost\Ubuntu\home\objeck\Documents\Code\objeck-lang"

rmdir /s /q deploy

docker rm objeck_nightly
docker image prune --force --all

xcopy /e /q %2\core\release\deploy deploy\
docker build -t %1 . 
docker save -o tmp.tar %1
rename tmp.tar %1.tar

REM docker run -d --name objeck_nightly -it %1
REM docker attach objeck_nightly
REM docker run --rm -it %1 /bin/bash