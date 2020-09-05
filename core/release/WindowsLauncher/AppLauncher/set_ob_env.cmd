@echo off
@echo =========================================
@echo Objeck Command Prompt (obc, obr, obd)
@echo Copyright(c) 2008-2020, Randy Hollines
@echo =========================================
set PATH=%PATH%;..\bin;..\lib\sdl
set OBJECK_LIB_PATH=D:\Code\objeck-lang\core\release\deploy64\lib
if [%1]==[] goto end
cd %1
:end