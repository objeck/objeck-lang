@echo off
SETLOCAL EnableDelayedExpansion

REM ============================================================
REM  Objeck LSP - Update Script (Windows)
REM
REM  Refreshes the self-contained LSP deployment at ~/.objeck-lsp/
REM  with updated runtime and server files.
REM
REM  Usage: update_lsp.cmd <objeck_install_dir> [lsp_server_dir]
REM    objeck_install_dir  Path to Objeck installation
REM    lsp_server_dir      Path to LSP server files (default: ..\server)
REM ============================================================

if "%~1"=="" goto usage

SET OBJECK_DIR=%~1
SET LSP_HOME=%USERPROFILE%\.objeck-lsp

if "%~2"=="" (
    SET SERVER_DIR=%~dp0..\server
) else (
    SET SERVER_DIR=%~2
)

REM validate inputs
if not exist "%OBJECK_DIR%\bin\obr.exe" (
    echo ERROR: Cannot find obr.exe in %OBJECK_DIR%\bin\
    exit /b 1
)

if not exist "%SERVER_DIR%\objeck_lsp.obe" (
    echo ERROR: Cannot find objeck_lsp.obe in %SERVER_DIR%\
    exit /b 1
)

if not exist "%LSP_HOME%" (
    echo ERROR: %LSP_HOME% does not exist. Run install.cmd first.
    exit /b 1
)

echo.
echo ========================================
echo  Objeck LSP - Update
echo ========================================
echo.

REM update runtime
echo [1/3] Updating runtime from %OBJECK_DIR%...
copy /y "%OBJECK_DIR%\bin\obr.exe" "%LSP_HOME%\bin\" >nul
copy /y "%OBJECK_DIR%\bin\obd.exe" "%LSP_HOME%\bin\" >nul 2>nul
xcopy /y /q "%OBJECK_DIR%\lib\*.*" "%LSP_HOME%\lib\" >nul
echo    Done.

REM update native libraries (includes libobjk_diags.dll which has the compiler)
echo [2/3] Updating native libraries from %OBJECK_DIR%...
if exist "%OBJECK_DIR%\lib\native" (
    if not exist "%LSP_HOME%\lib\native" mkdir "%LSP_HOME%\lib\native"
    xcopy /y /q "%OBJECK_DIR%\lib\native\*.dll" "%LSP_HOME%\lib\native\" >nul
    echo    Done.
) else (
    echo    Skipped - no native directory found.
)

REM update server
echo [3/3] Updating LSP server from %SERVER_DIR%...
copy /y "%SERVER_DIR%\objeck_lsp.obe" "%LSP_HOME%\" >nul
copy /y "%SERVER_DIR%\objk_apis.json" "%LSP_HOME%\" >nul
echo    Done.

echo.
echo ========================================
echo  Update complete
echo ========================================
echo.
echo  LSP home: %LSP_HOME%
echo.

goto end

:usage
    echo.
    echo  Usage: update_lsp.cmd ^<objeck_install_dir^> [lsp_server_dir]
    echo.
    echo  Arguments:
    echo    objeck_install_dir  Path to Objeck installation
    echo    lsp_server_dir      Path to LSP server files (default: ..\server)
    echo.
    echo  Examples:
    echo    User install:    update_lsp.cmd C:\Users\you\objeck
    echo    System install:  update_lsp.cmd "C:\Program Files\Objeck"
    echo.
    exit /b 1

:end
