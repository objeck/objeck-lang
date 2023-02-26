@echo off

REM obc -src gtk_parser_test1.obs -lib xml
obc -src gtk_parser_test3.obs -lib xml

if [%1] NEQ [] (
REM	obr gtk_parser_test1 %1
	obr gtk_parser_test3 %1
)