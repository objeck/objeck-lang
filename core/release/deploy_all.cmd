REM build targets
call deploy_amd64.cmd %1 %2
call deploy_arm64.cmd %1 %2

REM build LSP
setlocal
rmdir /q /s "%USERPROFILE%\Desktop\objeck-lsp"
mkdir "%USERPROFILE%\Desktop\objeck-lsp"
set CUR_DIR=%CD%
set PATH=%CUR_DIR%\deploy64\bin;%PATH%
set OBJECK_LIB_PATH=%CUR_DIR%\deploy64\lib
pushd ..\..\..\objeck-lsp
call deploy_lsp.cmd deploy
xcopy objeck-lsp-*.zip "%USERPROFILE%\Desktop\objeck-lsp"
popd