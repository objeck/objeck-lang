rmdir /s /q deploy
xcopy /e /q \\wsl.localhost\Ubuntu\home\objeck\Documents\Code\objeck-lang\core\release\deploy deploy\
docker build -t objeck:2024.3.0 . 
docker run -d --name objeck_nightly -it objeck:2024.3.0
docker attach objeck_nightly
