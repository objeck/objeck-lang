@echo off

taskkill /f /t /im "obr.exe"
obc -src json_rpc -lib net,json

if [%1] NEQ [brun] goto end
	start /b obr json_rpc server
	obr json_rpc client
:end	