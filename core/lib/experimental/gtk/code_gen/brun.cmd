@echo off

del /q *.obe

obc -src gtk3_codegen.obs,gtk3_model.obs -lib xml -dest gtk_binder

if [%1] NEQ [] (
	obr gtk_binder %1
)