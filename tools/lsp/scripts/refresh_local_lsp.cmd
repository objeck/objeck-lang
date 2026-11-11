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
REM  It does nothing where there is no installed server (CI, other machines),
REM  and nothing when OBJECK_SKIP_LSP_REFRESH is set. A failure prints a
REM  warning and never fails the deploy.
REM ============================================================

if defined OBJECK_SKIP_LSP_REFRESH exit /b 0
if "%~1"=="" exit /b 0
if not exist "%USERPROFILE%\.objeck-lsp\objeck_lsp.obe" exit /b 0
if not exist "%~1\bin\obr.exe" exit /b 0

SET LOG=%TEMP%\objeck-lsp-refresh.log
echo   refreshing the language server in %USERPROFILE%\.objeck-lsp

call "%~dp0..\server\build_server.cmd" > "%LOG%" 2>&1
if errorlevel 1 (
    echo   warning: the language server did not build; it was left as it was ^(see %LOG%^)
    exit /b 0
)

call "%~dp0update_lsp.cmd" "%~1" "%~dp0..\server" >> "%LOG%" 2>&1
if errorlevel 1 (
    echo   warning: the language server was not updated ^(see %LOG%^)
    exit /b 0
)

echo   language server refreshed
exit /b 0
