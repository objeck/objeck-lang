cls
taskkill /f /t /im "obr.exe"
obc -src prgm321 -lib net,json
start /b obr prgm321 server

REM tasklist | find /I "obr"