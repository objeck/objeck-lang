@echo off
SETLOCAL EnableDelayedExpansion

REM ============================================================
REM  Objeck LSP - Install Script (Windows)
REM
REM  Usage: install.cmd <objeck_install_dir> <editor>
REM    editor: vscode | sublime | neovim | emacs | helix | all
REM
REM  Run from the extracted release directory (objeck-lsp-VERSION/)
REM ============================================================

if "%~1"=="" goto usage
if "%~2"=="" goto usage

SET OBJECK_DIR=%~1
SET EDITOR=%~2
SET SCRIPT_DIR=%~dp0
SET RELEASE_DIR=%SCRIPT_DIR%..
SET LSP_HOME=%USERPROFILE%\.objeck-lsp

REM validate Objeck install
if not exist "%OBJECK_DIR%\bin\obr.exe" (
    echo ERROR: Cannot find obr.exe in %OBJECK_DIR%\bin\
    echo Make sure the Objeck install directory is correct.
    exit /b 1
)

REM validate release directory
if not exist "%RELEASE_DIR%\server\objeck_lsp.obe" (
    echo ERROR: Cannot find server\objeck_lsp.obe in release directory.
    echo Run this script from the extracted release directory.
    exit /b 1
)

echo.
echo ========================================
echo  Objeck LSP - Install
echo ========================================
echo.
echo  Objeck:     %OBJECK_DIR%
echo  LSP home:   %LSP_HOME%
echo  Editor:     %EDITOR%
echo.

REM --- create self-contained deployment ---
echo [1] Creating self-contained deployment at %LSP_HOME%...
mkdir "%LSP_HOME%" 2>nul
mkdir "%LSP_HOME%\bin" 2>nul
mkdir "%LSP_HOME%\lib" 2>nul
copy /y "%OBJECK_DIR%\bin\obr.exe" "%LSP_HOME%\bin\" >nul
if exist "%OBJECK_DIR%\bin\obd.exe" copy /y "%OBJECK_DIR%\bin\obd.exe" "%LSP_HOME%\bin\" >nul
xcopy /y /q /s "%OBJECK_DIR%\lib\*" "%LSP_HOME%\lib\" >nul
copy /y "%RELEASE_DIR%\server\objeck_lsp.obe" "%LSP_HOME%\" >nul
copy /y "%RELEASE_DIR%\server\objk_apis.json" "%LSP_HOME%\" >nul
copy /y "%RELEASE_DIR%\server\lsp_server.cmd" "%LSP_HOME%\" >nul
copy /y "%RELEASE_DIR%\server\lsp_server.sh" "%LSP_HOME%\" >nul
echo    Done: %LSP_HOME%

REM --- editor dispatch ---
if /i "%EDITOR%"=="vscode" goto install_vscode
if /i "%EDITOR%"=="sublime" goto install_sublime
if /i "%EDITOR%"=="neovim" goto install_neovim
if /i "%EDITOR%"=="emacs" goto install_emacs
if /i "%EDITOR%"=="helix" goto install_helix
if /i "%EDITOR%"=="vim" goto install_vim
if /i "%EDITOR%"=="all" goto install_all
echo ERROR: Unknown editor "%EDITOR%"
goto usage

REM ============================================================
:install_vscode
REM ============================================================
echo.
echo [VS Code] Installing extension...

SET VSIX_FILE=
for %%F in ("%RELEASE_DIR%\clients\vscode\*.vsix") do SET VSIX_FILE=%%F

if not defined VSIX_FILE (
    echo    WARNING: No .vsix file found in clients\vscode\
    goto done
)

where code >nul 2>&1
if !ERRORLEVEL! NEQ 0 (
    echo    WARNING: 'code' command not found. Install the extension manually:
    echo    Open VS Code, Extensions panel, drag-and-drop !VSIX_FILE!
    goto done
)

call code --install-extension "!VSIX_FILE!" --force
echo    Extension installed.

REM Copy rebuilt server to the installed extension to avoid version mismatch
REM (sync objeck_lsp.obe, objk_apis.json, and the launcher scripts)
for /d %%D in ("%USERPROFILE%\.vscode\extensions\objeck-lsp.objeck-lsp-*") do (
    if exist "%%D\server" (
        copy /y "%RELEASE_DIR%\server\objeck_lsp.obe" "%%D\server\objeck_lsp.obe" >nul 2>nul
        copy /y "%RELEASE_DIR%\server\objk_apis.json" "%%D\server\objk_apis.json" >nul 2>nul
        copy /y "%RELEASE_DIR%\clients\vscode\server\lsp_server.cmd" "%%D\server\lsp_server.cmd" >nul 2>nul
        copy /y "%RELEASE_DIR%\clients\vscode\server\lsp_server.sh" "%%D\server\lsp_server.sh" >nul 2>nul
    )
)
echo    Server synchronized.

REM Auto-configure VS Code settings
SET VSCODE_SETTINGS=%APPDATA%\Code\User\settings.json
SET ESC_DIR=%LSP_HOME:\=\\%
if not exist "%VSCODE_SETTINGS%" goto create_settings
findstr /C:"objk.win.install.dir" "%VSCODE_SETTINGS%" >nul 2>&1
if errorlevel 1 (
    echo    NOTE: Add to VS Code settings ^(%VSCODE_SETTINGS%^):
    echo      "objk.win.install.dir": "%ESC_DIR%"
) else (
    echo    VS Code settings already configured.
)
goto done
:create_settings
echo { > "%VSCODE_SETTINGS%"
echo   "objk.win.install.dir": "%ESC_DIR%" >> "%VSCODE_SETTINGS%"
echo } >> "%VSCODE_SETTINGS%"
echo    VS Code settings created.
goto done

REM ============================================================
:install_sublime
REM ============================================================
echo.
echo [Sublime Text] Installing...

SET SUBLIME_PKG=%APPDATA%\Sublime Text\Packages
SET SUBLIME_OBJECK=!SUBLIME_PKG!\Objeck

REM install syntax highlighting
if not exist "!SUBLIME_OBJECK!" mkdir "!SUBLIME_OBJECK!"
if exist "%RELEASE_DIR%\clients\sublime\objeck.sublime-syntax" (
    copy /y "%RELEASE_DIR%\clients\sublime\objeck.sublime-syntax" "!SUBLIME_OBJECK!\" >nul
    echo    Syntax file installed.
)
if exist "%RELEASE_DIR%\clients\sublime\.python-version" (
    copy /y "%RELEASE_DIR%\clients\sublime\.python-version" "!SUBLIME_OBJECK!\" >nul
)

REM write LSP settings
SET LSP_SETTINGS=!SUBLIME_PKG!\User\LSP.sublime-settings
SET LSP_PATH=%LSP_HOME:\=/%
echo    Writing LSP settings...
(
    echo {
    echo 	"clients": {
    echo 		"objeck": {
    echo 			"enabled": true,
    echo 			"command": [
    echo 				"!LSP_PATH!/bin/obr.exe",
    echo 				"!LSP_PATH!/objeck_lsp.obe",
    echo 				"!LSP_PATH!/objk_apis.json",
    echo 				"stdio"
    echo 			],
    echo 			"env": {
    echo 				"OBJECK_LIB_PATH": "!LSP_PATH!/lib",
    echo 				"OBJECK_STDIO": "binary"
    echo 			},
    echo 			"selector": "source.objeck-obs"
    echo 		}
    echo 	}
    echo }
) > "!LSP_SETTINGS!"
echo    LSP settings written to !LSP_SETTINGS!

REM install DAP adapter for the "Debugger" Sublime package
if exist "%RELEASE_DIR%\clients\sublime\dap\objeck_dap_adapter.py" (
    copy /y "%RELEASE_DIR%\clients\sublime\dap\objeck_dap_adapter.py" "!SUBLIME_OBJECK!\" >nul
    echo    DAP adapter installed: !SUBLIME_OBJECK!\objeck_dap_adapter.py

    SET OBJK_SETTINGS=!SUBLIME_PKG!\User\Objeck.sublime-settings
    SET OBJK_DIR_FWD=%OBJECK_DIR:\=/%
    (
        echo {
        echo     "obd_path": "!OBJK_DIR_FWD!/bin/obd.exe",
        echo     "objeck_lib_path": "!OBJK_DIR_FWD!/lib"
        echo }
    ) > "!OBJK_SETTINGS!"
    echo    Objeck settings written to !OBJK_SETTINGS!
)

echo.
echo    Next steps:
echo      1. Tools ^> LSP ^> Enable Language Server Globally ^> objeck
echo      2. ^(For debugging^) install the "Debugger" package via Package Control
echo      3. See clients\sublime\dap\objeck.sublime-project.example
goto done

REM ============================================================
:install_neovim
REM ============================================================
echo.
echo [Neovim] Installing...

REM Neovim on Windows uses %LOCALAPPDATA%\nvim\
SET NVIM_DIR=%LOCALAPPDATA%\nvim
SET NVIM_LSP=%NVIM_DIR%\lsp
SET NVIM_SYNTAX=%NVIM_DIR%\syntax
if not exist "%NVIM_LSP%" mkdir "%NVIM_LSP%" 2>nul
if not exist "%NVIM_SYNTAX%" mkdir "%NVIM_SYNTAX%" 2>nul

SET LSP_PATH=%LSP_HOME:\=/%
if exist "%RELEASE_DIR%\clients\neovim\objeck.lua" (
    powershell -NoProfile -Command "(Get-Content '%RELEASE_DIR%\clients\neovim\objeck.lua') -replace '<objeck_server_path>', '%LSP_PATH%' | Set-Content '%NVIM_LSP%\objeck.lua'"
    echo    Installed: %NVIM_LSP%\objeck.lua
) else (
    echo    WARNING: clients\neovim\objeck.lua not found in release.
)
if exist "%RELEASE_DIR%\clients\neovim\objeck.vim" (
    copy /y "%RELEASE_DIR%\clients\neovim\objeck.vim" "%NVIM_SYNTAX%\" >nul
    echo    Installed: %NVIM_SYNTAX%\objeck.vim
)

echo.
echo    Add to your init.lua:
echo      vim.filetype.add({ extension = { obs = 'objeck' } })
echo      vim.lsp.enable('objeck')
goto done

REM ============================================================
:install_emacs
REM ============================================================
echo.
echo [Emacs] Installing...

REM Emacs on Windows uses %APPDATA%/.emacs.d/ as user-emacs-directory
SET EMACS_DIR=%APPDATA%\.emacs.d
SET EMACS_LISP=%EMACS_DIR%\lisp
if not exist "%EMACS_LISP%" mkdir "%EMACS_LISP%" 2>nul

SET LSP_PATH=%LSP_HOME:\=/%
if exist "%RELEASE_DIR%\clients\emacs\objeck-mode.el" (
    powershell -NoProfile -Command "(Get-Content '%RELEASE_DIR%\clients\emacs\objeck-mode.el') -replace '<objeck_server_path>', '%LSP_PATH%' | Set-Content '%EMACS_LISP%\objeck-mode.el'"
    echo    Installed: %EMACS_LISP%\objeck-mode.el
) else (
    echo    WARNING: clients\emacs\objeck-mode.el not found in release.
)

REM create init.el if it doesn't exist
SET EMACS_INIT=%EMACS_DIR%\init.el
if not exist "%EMACS_INIT%" (
    echo (add-to-list 'load-path (expand-file-name "lisp" user-emacs-directory^)^)> "%EMACS_INIT%"
    echo (require 'objeck-mode^)>> "%EMACS_INIT%"
    echo    Created: %EMACS_INIT%
) else (
    echo    NOTE: %EMACS_INIT% already exists.
    echo    Ensure it contains:
    echo      (add-to-list 'load-path (expand-file-name "lisp" user-emacs-directory^)^)
    echo      (require 'objeck-mode^)
)
goto done

REM ============================================================
:install_helix
REM ============================================================
echo.
echo [Helix] Installing...

REM Helix on Windows uses %APPDATA%\helix\
SET HELIX_DIR=%APPDATA%\helix
if not exist "%HELIX_DIR%" mkdir "%HELIX_DIR%" 2>nul

SET LSP_PATH=%LSP_HOME:\=/%
SET HELIX_LANGS=%HELIX_DIR%\languages.toml
if exist "%RELEASE_DIR%\clients\helix\languages.toml" (
    if exist "%HELIX_LANGS%" (
        echo    NOTE: %HELIX_LANGS% already exists.
        echo    Merge the contents of clients\helix\languages.toml manually,
        echo    replacing ^<objeck_server_path^> with %LSP_PATH%
    ) else (
        powershell -NoProfile -Command "(Get-Content '%RELEASE_DIR%\clients\helix\languages.toml') -replace '<objeck_server_path>', '%LSP_PATH%' | Set-Content '%HELIX_LANGS%'"
        echo    Installed: %HELIX_LANGS%
    )
) else (
    echo    WARNING: clients\helix\languages.toml not found in release.
)
goto done

REM ============================================================
:install_vim
REM ============================================================
echo.
echo [Vim/gvim] Installing...

REM Windows gvim runtime dir is %USERPROFILE%\vimfiles
SET VIM_DIR=%USERPROFILE%\vimfiles
if not exist "%VIM_DIR%\syntax" mkdir "%VIM_DIR%\syntax" 2>nul
if not exist "%VIM_DIR%\ftdetect" mkdir "%VIM_DIR%\ftdetect" 2>nul
if not exist "%VIM_DIR%\plugin" mkdir "%VIM_DIR%\plugin" 2>nul
if not exist "%VIM_DIR%\pack\objeck\start" mkdir "%VIM_DIR%\pack\objeck\start" 2>nul

REM Syntax + ftdetect from clients/vim/ if shipped, else fall back to a
REM sibling objeck-lang source checkout.
SET VIM_SYNTAX_SRC=
if exist "%RELEASE_DIR%\clients\vim\syntax\objeck.vim" SET VIM_SYNTAX_SRC=%RELEASE_DIR%\clients\vim
if not defined VIM_SYNTAX_SRC if exist "%RELEASE_DIR%\..\objeck-lang\docs\syntax\vim\objeck.vim" SET VIM_SYNTAX_SRC=%RELEASE_DIR%\..\objeck-lang\docs\syntax\vim
if defined VIM_SYNTAX_SRC (
    copy /y "%VIM_SYNTAX_SRC%\objeck.vim" "%VIM_DIR%\syntax\" >nul 2>nul
    copy /y "%VIM_SYNTAX_SRC%\ftdetect\objeck.vim" "%VIM_DIR%\ftdetect\" >nul 2>nul
    echo    Syntax + ftdetect installed.
) else (
    echo    WARNING: vim syntax files not found.
)

REM yegappan/lsp via git clone
SET LSP_PLUGIN=%VIM_DIR%\pack\objeck\start\lsp
if not exist "%LSP_PLUGIN%" (
    where git >nul 2>&1
    if !ERRORLEVEL! EQU 0 (
        git clone --depth 1 https://github.com/yegappan/lsp "%LSP_PLUGIN%" >nul 2>&1
        echo    Installed: yegappan/lsp
    ) else (
        echo    NOTE: git not found, skipping yegappan/lsp clone
    )
)

REM vimspector via git clone
SET VS_PLUGIN=%VIM_DIR%\pack\objeck\start\vimspector
if not exist "%VS_PLUGIN%" (
    where git >nul 2>&1
    if !ERRORLEVEL! EQU 0 (
        git clone --depth 1 https://github.com/puremourning/vimspector "%VS_PLUGIN%" >nul 2>&1
        echo    Installed: puremourning/vimspector
    ) else (
        echo    NOTE: git not found, skipping vimspector clone
    )
)

REM Drop the Objeck vim config into auto-loaded plugin dir
if exist "%RELEASE_DIR%\clients\vim\objeck_vimrc.vim" (
    copy /y "%RELEASE_DIR%\clients\vim\objeck_vimrc.vim" "%VIM_DIR%\plugin\objeck.vim" >nul
    echo    Installed: %VIM_DIR%\plugin\objeck.vim
)

REM Vimspector adapter (~/.vimspector.json)
if exist "%RELEASE_DIR%\clients\vim\vimspector.json" (
    if exist "%USERPROFILE%\.vimspector.json" (
        echo    NOTE: %USERPROFILE%\.vimspector.json already exists, leaving it.
    ) else (
        copy /y "%RELEASE_DIR%\clients\vim\vimspector.json" "%USERPROFILE%\.vimspector.json" >nul
        echo    Installed: %USERPROFILE%\.vimspector.json
    )
)

echo.
echo    Next: open a .obs file in gvim, set a breakpoint with ^<F9^>,
echo          start debugging with ^<F5^>. See clients\vim\README.md.
goto done

REM ============================================================
:install_all
REM ============================================================

REM VS Code
echo.
echo [VS Code] Installing extension...
SET VSIX_FILE=
for %%F in ("%RELEASE_DIR%\clients\vscode\*.vsix") do SET VSIX_FILE=%%F
if defined VSIX_FILE (
    where code >nul 2>&1
    if !ERRORLEVEL! NEQ 0 (
        echo    WARNING: 'code' command not found. Install the extension manually.
    ) else (
        call code --install-extension "!VSIX_FILE!" --force
        echo    Extension installed.
        REM Sync rebuilt server files into deployed extension
        for /d %%D in ("%USERPROFILE%\.vscode\extensions\objeck-lsp.objeck-lsp-*") do (
            if exist "%%D\server" (
                copy /y "%RELEASE_DIR%\server\objeck_lsp.obe" "%%D\server\objeck_lsp.obe" >nul 2>nul
                copy /y "%RELEASE_DIR%\server\objk_apis.json" "%%D\server\objk_apis.json" >nul 2>nul
                copy /y "%RELEASE_DIR%\clients\vscode\server\lsp_server.cmd" "%%D\server\lsp_server.cmd" >nul 2>nul
                copy /y "%RELEASE_DIR%\clients\vscode\server\lsp_server.sh" "%%D\server\lsp_server.sh" >nul 2>nul
            )
        )
        echo    Server synchronized.
    )
) else (
    echo    WARNING: No .vsix file found in clients\vscode\
)

REM Sublime
echo.
echo [Sublime Text] Installing...
SET SUBLIME_PKG=%APPDATA%\Sublime Text\Packages
SET SUBLIME_OBJECK=!SUBLIME_PKG!\Objeck
if not exist "!SUBLIME_OBJECK!" mkdir "!SUBLIME_OBJECK!"
if exist "%RELEASE_DIR%\clients\sublime\objeck.sublime-syntax" (
    copy /y "%RELEASE_DIR%\clients\sublime\objeck.sublime-syntax" "!SUBLIME_OBJECK!\" >nul
    echo    Syntax file installed.
)
if exist "%RELEASE_DIR%\clients\sublime\.python-version" (
    copy /y "%RELEASE_DIR%\clients\sublime\.python-version" "!SUBLIME_OBJECK!\" >nul
)
SET LSP_SETTINGS=!SUBLIME_PKG!\User\LSP.sublime-settings
SET LSP_PATH=%LSP_HOME:\=/%
echo    Writing LSP settings...
(
    echo {
    echo 	"clients": {
    echo 		"objeck": {
    echo 			"enabled": true,
    echo 			"command": [
    echo 				"!LSP_PATH!/bin/obr.exe",
    echo 				"!LSP_PATH!/objeck_lsp.obe",
    echo 				"!LSP_PATH!/objk_apis.json",
    echo 				"stdio"
    echo 			],
    echo 			"env": {
    echo 				"OBJECK_LIB_PATH": "!LSP_PATH!/lib",
    echo 				"OBJECK_STDIO": "binary"
    echo 			},
    echo 			"selector": "source.objeck-obs"
    echo 		}
    echo 	}
    echo }
) > "!LSP_SETTINGS!"
echo    LSP settings written.

REM install Sublime DAP adapter for the "Debugger" package
if exist "%RELEASE_DIR%\clients\sublime\dap\objeck_dap_adapter.py" (
    copy /y "%RELEASE_DIR%\clients\sublime\dap\objeck_dap_adapter.py" "!SUBLIME_OBJECK!\" >nul
    echo    DAP adapter installed.
    SET OBJK_SETTINGS=!SUBLIME_PKG!\User\Objeck.sublime-settings
    SET OBJK_DIR_FWD=%OBJECK_DIR:\=/%
    (
        echo {
        echo     "obd_path": "!OBJK_DIR_FWD!/bin/obd.exe",
        echo     "objeck_lib_path": "!OBJK_DIR_FWD!/lib"
        echo }
    ) > "!OBJK_SETTINGS!"
)

REM Neovim (uses %LOCALAPPDATA%\nvim\ on Windows)
echo.
echo [Neovim] Installing...
SET NVIM_LSP=%LOCALAPPDATA%\nvim\lsp
SET NVIM_SYNTAX=%LOCALAPPDATA%\nvim\syntax
if not exist "%NVIM_LSP%" mkdir "%NVIM_LSP%" 2>nul
if not exist "%NVIM_SYNTAX%" mkdir "%NVIM_SYNTAX%" 2>nul
SET LSP_PATH=%LSP_HOME:\=/%
if exist "%RELEASE_DIR%\clients\neovim\objeck.lua" (
    powershell -NoProfile -Command "(Get-Content '%RELEASE_DIR%\clients\neovim\objeck.lua') -replace '<objeck_server_path>', '%LSP_PATH%' | Set-Content '%NVIM_LSP%\objeck.lua'"
    echo    Installed: %NVIM_LSP%\objeck.lua
)
if exist "%RELEASE_DIR%\clients\neovim\objeck.vim" (
    copy /y "%RELEASE_DIR%\clients\neovim\objeck.vim" "%NVIM_SYNTAX%\" >nul
)

REM Emacs (uses %APPDATA%/.emacs.d/ on Windows)
echo.
echo [Emacs] Installing...
SET EMACS_DIR=%APPDATA%\.emacs.d
SET EMACS_LISP=%EMACS_DIR%\lisp
if not exist "%EMACS_LISP%" mkdir "%EMACS_LISP%" 2>nul
SET LSP_PATH=%LSP_HOME:\=/%
if exist "%RELEASE_DIR%\clients\emacs\objeck-mode.el" (
    powershell -NoProfile -Command "(Get-Content '%RELEASE_DIR%\clients\emacs\objeck-mode.el') -replace '<objeck_server_path>', '%LSP_PATH%' | Set-Content '%EMACS_LISP%\objeck-mode.el'"
    echo    Installed: %EMACS_LISP%\objeck-mode.el
)
SET EMACS_INIT=%EMACS_DIR%\init.el
if not exist "%EMACS_INIT%" (
    echo (add-to-list 'load-path (expand-file-name "lisp" user-emacs-directory^)^)> "%EMACS_INIT%"
    echo (require 'objeck-mode^)>> "%EMACS_INIT%"
)

REM Helix (uses %APPDATA%\helix\ on Windows)
echo.
echo [Helix] Installing...
SET HELIX_DIR=%APPDATA%\helix
if not exist "%HELIX_DIR%" mkdir "%HELIX_DIR%" 2>nul
SET HELIX_LANGS=%HELIX_DIR%\languages.toml
if exist "%RELEASE_DIR%\clients\helix\languages.toml" (
    if exist "%HELIX_LANGS%" (
        echo    NOTE: %HELIX_LANGS% already exists. Merge clients\helix\languages.toml manually.
    ) else (
        powershell -NoProfile -Command "(Get-Content '%RELEASE_DIR%\clients\helix\languages.toml') -replace '<objeck_server_path>', '%LSP_PATH%' | Set-Content '%HELIX_LANGS%'"
        echo    Installed: %HELIX_LANGS%
    )
)

REM Vim / gvim
echo.
echo [Vim/gvim] Installing...
SET VIM_DIR=%USERPROFILE%\vimfiles
if not exist "%VIM_DIR%\syntax" mkdir "%VIM_DIR%\syntax" 2>nul
if not exist "%VIM_DIR%\ftdetect" mkdir "%VIM_DIR%\ftdetect" 2>nul
if not exist "%VIM_DIR%\plugin" mkdir "%VIM_DIR%\plugin" 2>nul
if not exist "%VIM_DIR%\pack\objeck\start" mkdir "%VIM_DIR%\pack\objeck\start" 2>nul
SET VIM_SYNTAX_SRC=
if exist "%RELEASE_DIR%\clients\vim\syntax\objeck.vim" SET VIM_SYNTAX_SRC=%RELEASE_DIR%\clients\vim
if not defined VIM_SYNTAX_SRC if exist "%RELEASE_DIR%\..\objeck-lang\docs\syntax\vim\objeck.vim" SET VIM_SYNTAX_SRC=%RELEASE_DIR%\..\objeck-lang\docs\syntax\vim
if defined VIM_SYNTAX_SRC (
    copy /y "%VIM_SYNTAX_SRC%\objeck.vim" "%VIM_DIR%\syntax\" >nul 2>nul
    copy /y "%VIM_SYNTAX_SRC%\ftdetect\objeck.vim" "%VIM_DIR%\ftdetect\" >nul 2>nul
)
where git >nul 2>&1
if !ERRORLEVEL! EQU 0 (
    if not exist "%VIM_DIR%\pack\objeck\start\lsp" git clone --depth 1 https://github.com/yegappan/lsp "%VIM_DIR%\pack\objeck\start\lsp" >nul 2>&1
    if not exist "%VIM_DIR%\pack\objeck\start\vimspector" git clone --depth 1 https://github.com/puremourning/vimspector "%VIM_DIR%\pack\objeck\start\vimspector" >nul 2>&1
)
if exist "%RELEASE_DIR%\clients\vim\objeck_vimrc.vim" (
    copy /y "%RELEASE_DIR%\clients\vim\objeck_vimrc.vim" "%VIM_DIR%\plugin\objeck.vim" >nul
)
if exist "%RELEASE_DIR%\clients\vim\vimspector.json" (
    if not exist "%USERPROFILE%\.vimspector.json" copy /y "%RELEASE_DIR%\clients\vim\vimspector.json" "%USERPROFILE%\.vimspector.json" >nul
)

echo.
echo    NOTE: Set VS Code setting "objk.win.install.dir" to your Objeck path.
echo    NOTE: In Sublime, enable the "objeck" language server globally.
echo    NOTE: Add vim.lsp.enable('objeck') to your Neovim init.lua.
echo    NOTE: Add (require 'objeck-mode) to your Emacs init.el.
echo    NOTE: Helix picks up languages.toml automatically.
echo    NOTE: gvim picks up the Objeck plugin automatically.
goto done

REM ============================================================
:done
REM ============================================================
echo.
echo ========================================
echo  Install complete
echo ========================================
echo.
goto end

:usage
echo.
echo  Usage: install.cmd ^<objeck_install_dir^> ^<editor^>
echo.
echo  Arguments:
echo    objeck_install_dir  Path to Objeck installation
echo    editor              One of: vscode, sublime, neovim, emacs, helix, vim, all
echo.
echo  Examples:
echo    User install:    install.cmd C:\Users\you\objeck vscode
echo    System install:  install.cmd "C:\Program Files\Objeck" all
echo.
exit /b 1

:end
