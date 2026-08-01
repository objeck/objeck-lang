@echo off
setlocal EnableExtensions EnableDelayedExpansion

rem The install directory arrives in OBJECK_INSTALL_DIR; %~1 is kept as a
rem fallback for older clients that still pass it on the command line.
rem The value comes from VS Code settings, so it is only ever expanded with
rem !delayed! syntax -- that substitutes after the line is parsed, so quotes
rem and other metacharacters in the path cannot start a new command.
if not defined OBJECK_INSTALL_DIR set "OBJECK_INSTALL_DIR=%~1"

set "OBJECK_LIB_PATH=!OBJECK_INSTALL_DIR!\lib"
set "PATH=!PATH!;!OBJECK_INSTALL_DIR!\bin"

"!OBJECK_INSTALL_DIR!\bin\obr" "%~dp0objeck_lsp.obe" "%~dp0objk_apis.json" pipe
