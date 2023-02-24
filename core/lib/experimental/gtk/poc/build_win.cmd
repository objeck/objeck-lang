@echo off
setlocal

if [%1] == [clib] (
	devenv poc.sln /rebuild "Debug|x64"
	copy vs\Debug\x64\libobjk_gtk3_test.dll ..\..\..\..\release\deploy64\lib\native
)

set PATH=%PATH%;..\..\..\..\release\deploy64\bin;..\win\bin
set OBJECK_LIB_PATH=..\..\..\..\release\deploy64\lib
obc -src gtk3_test.obs -tar lib -dest ..\..\..\..\release\deploy64\lib\gtk3_test.obl

obc -src app_test.obs -lib gtk3_test

if [%2] == [brun] (
	obr gtk3_test
)