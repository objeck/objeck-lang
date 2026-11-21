@echo off
SETLOCAL

REM ============================================================
REM  Objeck LSP - refresh a local language server after a deploy
REM
REM  Usage: refresh_local_lsp.cmd <deploy_dir>
REM    deploy_dir  the freshly built tree (core\release\deploy-x64)
REM
REM  An editor's language server is a self-contained copy in
REM  %USERPROFILE%\.objeck-lsp (see install.cmd), so it keeps running the
REM  release it was installed from while the toolchain moves on. A server three
REM  releases behind crashed on a program the current libraries compile.
REM  This rebuilds the server and runs update_lsp.cmd, so a deploy leaves the
REM  two in step.
REM
REM  A running server holds obr.exe and the native libraries open. Windows will
REM  not overwrite a file in use (and update_lsp.cmd copies without a word when
REM  it cannot), but it will rename one, so the old runtime files are moved aside
REM  as *.old first: the running server finishes on them and the editor's next
REM  start loads the new ones. The result is checked against the deploy tree.
REM
REM  It does nothing where there is no installed server (CI, other machines),
REM  and nothing when OBJECK_SKIP_LSP_REFRESH is set. A failure prints a
REM  warning and never fails the deploy.
REM ============================================================

if defined OBJECK_SKIP_LSP_REFRESH exit /b 0
if "%~1"=="" exit /b 0
if not exist "%USERPROFILE%\.objeck-lsp\objeck_lsp.obe" exit /b 0
if not exist "%~1\bin\obr.exe" exit /b 0

SET LSP_HOME=%USERPROFILE%\.objeck-lsp
SET LOG=%TEMP%\objeck-lsp-refresh.log
echo   refreshing the language server in %LSP_HOME%

call "%~dp0..\server\build_server.cmd" > "%LOG%" 2>&1
if errorlevel 1 (
    echo   warning: the language server did not build; it was left as it was ^(see %LOG%^)
    exit /b 0
)

REM move the runtime aside; a leftover from an earlier refresh goes first, unless a server still runs from it
del /q "%LSP_HOME%\bin\*.old" "%LSP_HOME%\lib\native\*.old" >nul 2>&1
for %%F in ("%LSP_HOME%\bin\*.exe" "%LSP_HOME%\bin\*.dll" "%LSP_HOME%\lib\native\*.dll") do ren "%%~F" "%%~nxF.old" >nul 2>&1

call "%~dp0update_lsp.cmd" "%~1" "%~dp0..\server" >> "%LOG%" 2>&1
if errorlevel 1 (
    echo   warning: the language server was not updated ^(see %LOG%^)
    exit /b 0
)

REM prove the files that matter arrived
SET STALE=
fc /b "%~1\bin\obr.exe" "%LSP_HOME%\bin\obr.exe" >nul 2>&1 || SET STALE=%STALE% bin\obr.exe
fc /b "%~1\lib\native\libobjk_diags.dll" "%LSP_HOME%\lib\native\libobjk_diags.dll" >nul 2>&1 || SET STALE=%STALE% lib\native\libobjk_diags.dll
fc /b "%~1\lib\lang.obl" "%LSP_HOME%\lib\lang.obl" >nul 2>&1 || SET STALE=%STALE% lib\lang.obl
fc /b "%~dp0..\server\objeck_lsp.obe" "%LSP_HOME%\objeck_lsp.obe" >nul 2>&1 || SET STALE=%STALE% objeck_lsp.obe
if defined STALE (
    echo   warning: not updated:%STALE% ^(see %LOG%^)
    echo            close the editor and run: tools\lsp\scripts\update_lsp.cmd "%~1"
    exit /b 0
)

REM the old files nothing holds open can go now; those a running server holds stay until it exits
del /q "%LSP_HOME%\bin\*.old" "%LSP_HOME%\lib\native\*.old" >nul 2>&1

echo   language server refreshed
if exist "%LSP_HOME%\bin\obr.exe.old" echo   a server is still running on the old files: restart it in the editor ^(Sublime: LSP: Restart Server^)
exit /b 0
