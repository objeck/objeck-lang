taskkill /f /t /im "obr.exe"
obc -src prgm321 -lib net,json

if [%1] NEQ [brun] goto end
	start /b obr prgm321 server
	obr prgm321 client
:end	