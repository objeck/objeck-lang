@echo off

del *.obe

obc -src gtk_parser.obs -lib xml

if [%1] NEQ [] (
REM	obr gtk_parser_test1 %1
	obr gtk_parser %1
)