@echo off

del *.obe gens\*.obl gens\*.txt

obc -src gtk3_codegen.obs -lib xml

if [%1] NEQ [] (
	obr gtk3_codegen %1 %2
	copy /y gens\objk_code.txt gens\gtk3_codegen.obs
	obc -src gens\gtk3_codegen.obs -tar lib
	
	copy /y gens\cxx_code.txt gens\gtk3_codegen.cpp
	REM compiler
)