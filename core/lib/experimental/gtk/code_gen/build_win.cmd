@echo off

del *.obe
del gens\*.txt

obc -src gtk3_binder.obs -lib xml

if [%1] NEQ [] (
	obr gtk3_binder %1
	copy /y gens\objk_code.txt gens\objk_code.obs
	obc -src gens\objk_code.obs -tar lib
)