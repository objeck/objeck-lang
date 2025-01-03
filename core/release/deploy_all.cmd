REM build targets
call deploy_windows.cmd x64 %1 %2
call deploy_windows.cmd arm64 %1 %2

REM build LSP
if [%1] NEQ [deploy] goto end
	setlocal
	rmdir /q /s "%USERPROFILE%\Desktop\objeck-lsp"
	mkdir "%USERPROFILE%\Desktop\objeck-lsp"
	set PATH=%CD%\deploy-x64\bin;%PATH%
	set OBJECK_LIB_PATH=%CD%\deploy-x64\lib
	pushd ..\..\..\objeck-lsp
	call deploy_lsp.cmd deploy
	xcopy objeck-lsp-*.zip "%USERPROFILE%\Desktop\objeck-lsp"
	popd
:end