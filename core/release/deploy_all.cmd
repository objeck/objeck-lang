REM build all windows targets

call deploy_windows.cmd x64 deploy %1
call deploy_windows.cmd arm64 deploy %1

REM build LSP
if [%2] NEQ [deploy] goto end
	setlocal
	rmdir /q /s "%USERPROFILE%\Documents\Objeck-Build\objeck-lsp"
	mkdir "%USERPROFILE%\Documents\Objeck-Build\objeck-lsp"
	set PATH=%CD%\deploy-x64\bin;%PATH%
	set OBJECK_LIB_PATH=%CD%\deploy-x64\lib
	pushd ..\..\..\objeck-lsp
	call deploy_lsp.cmd deploy
	xcopy objeck-lsp-*.zip "%USERPROFILE%\Documents\Objeck-Build\objeck-lsp"
	popd
:end