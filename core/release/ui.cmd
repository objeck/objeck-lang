@REM ---------------------------------------------------------------------------
@REM ui.cmd -- presentation for deploy_windows.cmd.
@REM
@REM   call "%~dp0ui.cmd" :ui_init %1 %2 %3   once, after UI_TOTAL is set
@REM   call "%~dp0ui.cmd" :ui_step "label"    at each stage boundary
@REM   call "%~dp0ui.cmd" :ui_warn "text"     a problem that does not stop the deploy
@REM   call "%~dp0ui.cmd" :ui_ok "text"       when the run succeeds
@REM
@REM ui_init picks one of four modes:
@REM   relaunch  Run by hand, the default. Sets UI_RELAUNCH=1 and the deploy
@REM             re-runs itself under ui.ps1. cmd cannot redraw anything while
@REM             devenv blocks it, so a second process draws live progress while
@REM             the build writes to a log.
@REM   child     UI_CHILD=1, set by ui.ps1. All output goes to that log, and these
@REM             routines print only "##ui" marker lines for ui.ps1 to read.
@REM   ci        GITHUB_ACTIONS or CI is set. Output streams exactly as it did
@REM             before this file existed, plus plain "[ n/N] stage" lines.
@REM   verbose   -v, --verbose or VERBOSE=1 (or no powershell.exe): streaming
@REM             output with coloured stage lines.
@REM
@REM Rules this file keeps:
@REM  * No "@echo off". Echo state is shared with the caller, and turning it off
@REM    here silently removed every command echo from the rest of the deploy's
@REM    CI log. Every line starts with @ instead.
@REM  * Every routine ends "exit /b 0". One whose last command failed would hand
@REM    errorlevel 1 to a caller whose next line may be "if errorlevel 1".
@REM  * No parenthesised blocks around expanded labels: a ")" would end them.
@REM  * Colour codes are whole variables, empty when colour is off.
@REM  * %~dp0 is read once, in ui_init: inside a "call :label" %0 is the label.
@REM  * CRLF line endings: cmd can miss labels in an LF-only batch file.
@REM ---------------------------------------------------------------------------
@goto %1

:ui_init
@set "UI_DIR=%~dp0"
@set "UI_ARCH=%~2"
@set "UI_STEP=0"
@set "UI_RELAUNCH=0"
@set "UI_MODE=verbose"
@set "UI_E="
@set "UI_RST="
@set "UI_B="
@set "UI_DIM="
@set "UI_GRN="
@set "UI_YEL="
@set "UI_BLU="
@set "UI_CYN="
@if "%UI_CHILD%"=="1" goto ui_init_child
@if defined GITHUB_ACTIONS goto ui_init_ci
@if defined CI goto ui_init_ci
@REM A bad target or a missing Visual Studio environment: say nothing, and let
@REM the deploy's own checks print their messages as they always have.
@if /i not "%~2"=="x64" if /i not "%~2"=="arm64" goto ui_init_off
@if not defined VCINSTALLDIR goto ui_init_off
@if "%VERBOSE%"=="1" goto ui_init_verbose
@if /i "%~3"=="-v" goto ui_init_verbose
@if /i "%~3"=="--verbose" goto ui_init_verbose
@if /i "%~4"=="-v" goto ui_init_verbose
@if /i "%~4"=="--verbose" goto ui_init_verbose
@where powershell.exe >nul 2>&1
@if errorlevel 1 goto ui_init_verbose
@set "UI_RELAUNCH=1"
@exit /b 0

:ui_init_off
@set "UI_MODE=off"
@exit /b 0

:ui_init_child
@set "UI_MODE=child"
@echo ##ui init %UI_TOTAL%
@exit /b 0

:ui_init_ci
@set "UI_MODE=ci"
@call :ui_version
@call :ui_now UI_T0
@echo === Objeck %UI_VER% deploy: windows-%UI_ARCH%, %UI_TOTAL% stages ===
@exit /b 0

:ui_init_verbose
@call :ui_version
@call :ui_now UI_T0
@if defined NO_COLOR goto ui_init_banner
@for /f %%a in ('echo prompt $E ^| cmd') do @set "UI_E=%%a"
@set "UI_RST=%UI_E%[0m"
@set "UI_B=%UI_E%[1m"
@set "UI_DIM=%UI_E%[90m"
@set "UI_GRN=%UI_E%[92m"
@set "UI_YEL=%UI_E%[93m"
@set "UI_BLU=%UI_E%[94m"
@set "UI_CYN=%UI_E%[96m"
:ui_init_banner
@echo.
@echo %UI_CYN%  ___   _        _              _
@echo  / _ \ ^| ^|__    (_)  ___   ___ ^| ^| __
@echo ^| ^| ^| ^|^| '_ \   ^| ^| / _ \ / __^|^| ^|/ /
@echo ^| ^|_^| ^|^| ^|_) ^|  ^| ^|^|  __/^| (__ ^|   ^<
@echo  \___/ ^|_.__/  _/ ^| \___^| \___^|^|_^|\_\
@echo               ^|__/%UI_RST%
@echo.
@echo   %UI_B%Objeck %UI_VER%%UI_RST%  %UI_DIM%windows-%UI_ARCH%, %UI_TOTAL% stages, streaming output%UI_RST%
@exit /b 0

:ui_step
@if "%UI_MODE%"=="off" exit /b 0
@set /a "UI_STEP+=1"
@set "UI_LABEL=%~2"
@if "%UI_MODE%"=="child" goto ui_step_child
@call :ui_elapsed
@set "_n=  %UI_STEP%"
@set "_n=%_n:~-2%/%UI_TOTAL%"
@if "%UI_MODE%"=="ci" goto ui_step_ci
@echo.
@echo   %UI_BLU%==^>%UI_RST% %UI_B%[%_n%] %UI_LABEL%%UI_RST%  %UI_DIM%%UI_ELAPSED%%UI_RST%
@exit /b 0
:ui_step_ci
@echo.
@echo [%_n%] %UI_LABEL%  (%UI_ELAPSED%)
@exit /b 0
:ui_step_child
@echo ##ui step %UI_STEP% %~2
@exit /b 0

:ui_warn
@if "%UI_MODE%"=="off" exit /b 0
@if "%UI_MODE%"=="child" goto ui_warn_child
@echo   %UI_YEL%!  %~2%UI_RST%
@exit /b 0
:ui_warn_child
@echo ##ui warn %~2
@exit /b 0

:ui_ok
@if "%UI_MODE%"=="off" exit /b 0
@if "%UI_MODE%"=="child" goto ui_ok_child
@call :ui_elapsed
@if "%UI_MODE%"=="ci" goto ui_ok_ci
@echo.
@echo   %UI_GRN%*  %~2%UI_RST%  %UI_DIM%%UI_STEP% stages in %UI_ELAPSED%%UI_RST%
@exit /b 0
:ui_ok_ci
@echo.
@echo === %~2: %UI_STEP% stages in %UI_ELAPSED% ===
@exit /b 0
:ui_ok_child
@echo ##ui ok %~2
@exit /b 0

:ui_version
@set "UI_VER=dev"
@for /f tokens^=2^ delims^=^" %%v in ('findstr /c:"define VERSION_STRING" "%UI_DIR%..\shared\version.h" 2^>nul') do @set "UI_VER=%%v"
@exit /b 0

:ui_elapsed
@call :ui_now _t1
@set /a "_el=_t1-UI_T0"
@if %_el% LSS 0 set /a "_el+=86400"
@set /a "_m=_el/60, _s=100+_el%%60"
@set "UI_ELAPSED=%_m%m %_s:~-2%s"
@exit /b 0

@REM %TIME% is " 9:05:03.12" or "09:05:03,12" depending on locale. 100%%a%%100
@REM reads both "9" and "09" as nine without the leading zero turning octal.
:ui_now
@for /f "tokens=1-3 delims=:.," %%a in ("%TIME: =0%") do @set /a "%1=(100%%a%%100)*3600+(100%%b%%100)*60+(100%%c%%100)"
@exit /b 0
