@echo off

del /q *.obe 2>NUL

obc -src gtk3_codegen.obs,gtk3_model.obs -lib xml -dest gtk_binder

if [%1] NEQ [] (
	obr gtk_binder %1
)