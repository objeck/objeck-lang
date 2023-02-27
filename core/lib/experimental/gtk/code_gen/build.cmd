@echo off

del *.obe

obc -src gtk_parser.obs -lib xml

if [%1] NEQ [] (
	obr gtk_parser %1
	copy /y gens\objk_code.txt gens\objk_code.obs
	obc -src gens\objk_code.obs -tar lib
)