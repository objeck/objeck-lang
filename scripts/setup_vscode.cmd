@echo off
REM Sets up VS Code for Objeck development
REM Usage: scripts\setup_vscode.cmd [objeck_install_dir] [project_dir]
REM
REM Example: scripts\setup_vscode.cmd "C:\Program Files\Objeck" "C:\MyProject"
REM          scripts\setup_vscode.cmd   (uses defaults for dev environment)

SETLOCAL EnableDelayedExpansion

SET INSTALL_DIR=%~1
SET PROJECT_DIR=%~2

IF "%INSTALL_DIR%"=="" SET INSTALL_DIR=%~dp0..\core\release\deploy-x64
IF "%PROJECT_DIR%"=="" SET PROJECT_DIR=%CD%

REM Resolve to absolute path
pushd "%INSTALL_DIR%" 2>nul
IF ERRORLEVEL 1 (
    echo ERROR: Install directory not found: %INSTALL_DIR%
    exit /b 1
)
SET INSTALL_DIR=%CD%
popd

echo ========================================
echo  Objeck VS Code Setup
echo ========================================
echo  Install dir: %INSTALL_DIR%
echo  Project dir: %PROJECT_DIR%
echo.

REM Verify binaries exist
IF NOT EXIST "%INSTALL_DIR%\bin\obc.exe" (
    echo ERROR: obc.exe not found in %INSTALL_DIR%\bin
    exit /b 1
)

REM Create .vscode directory
mkdir "%PROJECT_DIR%\.vscode" 2>nul

REM Escape backslashes for JSON
SET "ESC_INSTALL=%INSTALL_DIR:\=\\%"
SET "ESC_PROJECT=%PROJECT_DIR:\=\\%"

REM Write settings.json
echo {> "%PROJECT_DIR%\.vscode\settings.json"
echo   "objk.win.install.dir": "%ESC_INSTALL%",>> "%PROJECT_DIR%\.vscode\settings.json"
echo   "files.associations": {>> "%PROJECT_DIR%\.vscode\settings.json"
echo     "*.obs": "objeck">> "%PROJECT_DIR%\.vscode\settings.json"
echo   }>> "%PROJECT_DIR%\.vscode\settings.json"
echo }>> "%PROJECT_DIR%\.vscode\settings.json"

REM Write tasks.json
(
echo {
echo   "version": "2.0.0",
echo   "tasks": [
echo     {
echo       "label": "Objeck: Compile",
echo       "type": "shell",
echo       "command": "%ESC_INSTALL%\\bin\\obc.exe",
echo       "args": ["-src", "${file}", "-dest", "${fileDirname}/${fileBasenameNoExtension}.obe"],
echo       "options": {"env": {"OBJECK_LIB_PATH": "%ESC_INSTALL%\\lib"}},
echo       "group": {"kind": "build", "isDefault": true},
echo       "problemMatcher": {"pattern": {"regexp": "^^(.+):\\((\\d+),(\\d+)\\):\\s+(.+^)$", "file": 1, "line": 2, "column": 3, "message": 4}}
echo     },
echo     {
echo       "label": "Objeck: Compile (Debug^)",
echo       "type": "shell",
echo       "command": "%ESC_INSTALL%\\bin\\obc.exe",
echo       "args": ["-src", "${file}", "-dest", "${fileDirname}/${fileBasenameNoExtension}.obe", "-debug"],
echo       "options": {"env": {"OBJECK_LIB_PATH": "%ESC_INSTALL%\\lib"}},
echo       "group": "build",
echo       "problemMatcher": {"pattern": {"regexp": "^^(.+):\\((\\d+),(\\d+)\\):\\s+(.+^)$", "file": 1, "line": 2, "column": 3, "message": 4}}
echo     }
echo   ]
echo }
) > "%PROJECT_DIR%\.vscode\tasks.json"

REM Write launch.json
(
echo {
echo   "version": "0.2.0",
echo   "configurations": [
echo     {
echo       "type": "objeck",
echo       "request": "launch",
echo       "name": "Debug Current Program",
echo       "program": "${fileDirname}/${fileBasenameNoExtension}.obe",
echo       "sourceDir": "${fileDirname}",
echo       "preLaunchTask": "Objeck: Compile (Debug^)"
echo     }
echo   ]
echo }
) > "%PROJECT_DIR%\.vscode\launch.json"

echo  Created:
echo    %PROJECT_DIR%\.vscode\settings.json
echo    %PROJECT_DIR%\.vscode\tasks.json
echo    %PROJECT_DIR%\.vscode\launch.json
echo.
echo  Next steps:
echo    1. Open %PROJECT_DIR% in VS Code
echo    2. Install the Objeck extension (.vsix^)
echo    3. Ctrl+Shift+B to compile, F5 to debug
echo.
echo ========================================
echo  Setup complete
echo ========================================
