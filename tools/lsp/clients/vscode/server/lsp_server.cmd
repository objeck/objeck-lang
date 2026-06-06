set OBJECK_INSTALL_DIR=%~1

set OBJECK_LIB_PATH=%OBJECK_INSTALL_DIR%\lib
set PATH=%PATH%;%OBJECK_INSTALL_DIR%\bin

"%OBJECK_INSTALL_DIR%\bin\obr" "%~dp0objeck_lsp.obe" "%~dp0objk_apis.json" pipe
