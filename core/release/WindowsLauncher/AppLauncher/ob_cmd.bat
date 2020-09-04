@echo off
@echo =========================================
@echo Objeck Command Prompt v3.5.9
@echo Copyright(c) 2008-2020, Randy Hollines
@echo =========================================
SET PATH=%PATH%;D:\Code\objeck-lang\core\release\deploy64\bin
SET OBJECK_LIB_PATH=D:\Code\objeck-lang\core\release\deploy64\lib
IF [%1] == [] GOTO end
CD %1
:end