REM build targets
call deploy_amd64.cmd %1 %2
REM call deploy_arm64.cmd %1 %2

REM build LSP
if [%1] NEQ [deploy] goto end
	setlocal
	rmdir /q /s "%USERPROFILE%\Desktop\objeck-lsp"
	mkdir "%USERPROFILE%\Desktop\objeck-lsp"
	set PATH=%CD%\deploy64\bin;%PATH%
	set OBJECK_LIB_PATH=%CD%\deploy64\lib
	pushd ..\..\..\objeck-lsp
	call deploy_lsp.cmd deploy
	xcopy objeck-lsp-*.zip "%USERPROFILE%\Desktop\objeck-lsp"
	popd
:end