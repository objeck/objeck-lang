@echo off

del *.obe
rmdir /s /q out
mkdir out

obc -src gtk3_codegen.obs -lib xml

if [%1] NEQ [] (
	obr gtk3_codegen %1 %2
	copy /y out\objk_code.txt out\gtk3_codegen.obs
	obc -src out\gtk3_codegen.obs -tar lib
	
	copy /y out\cxx_code.txt out\gtk3_codegen.cpp
	REM compiler
)