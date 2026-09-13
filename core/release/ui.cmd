@echo off
REM ---------------------------------------------------------------------------
REM ui.cmd -- presentation for deploy_windows.cmd: a banner, one progress line
REM per stage, quiet tool output, and the log tail when a stage fails.
REM
REM   call "%~dp0ui.cmd" :ui_init %1 %2 %3     once, after UI_TOTAL is set
REM   call "%~dp0ui.cmd" :ui_step "label"      at each stage boundary
REM   call "%~dp0ui.cmd" :ui_warn "text"       a problem that does not stop the deploy
REM   call "%~dp0ui.cmd" :ui_fail              immediately before each "exit /b 1"
REM   call "%~dp0ui.cmd" :ui_ok "text"         when the run succeeds
REM
REM Tool output (devenv, msbuild, copy, xcopy, ...) is appended with %UI_REDIR%.
REM Run by hand, that is >>"%UI_LOG%" 2>&1 and the console shows only stages.
REM In CI (GITHUB_ACTIONS or CI is set), with VERBOSE=1, or with -v, it is
REM EMPTY: every line streams exactly as it did before this file existed, so a
REM CI failure is read from the same log it always was.
REM
REM Rules this file keeps:
REM  * Every routine ends "exit /b 0". One whose last command failed would hand
REM    errorlevel 1 to a caller whose next line may be "if errorlevel 1".
REM  * No parenthesised blocks around expanded paths or labels: a ")" in %TEMP%
REM    (C:\Users\Name (Work)\...) would close the block early.
REM  * Colour codes are whole variables, empty when colour is off. Writing
REM    %UI_E%[92m inline printed a literal "[92m" whenever ESC was empty.
REM  * CRLF line endings: cmd can miss labels in an LF-only batch file.
REM ---------------------------------------------------------------------------
goto %1

:ui_init
set "UI_ARCH=%~2"
set "UI_STEP=0"
set "UI_LABEL=setup"
set "UI_REDIR="
set "UI_CI=0"
if defined GITHUB_ACTIONS set "UI_CI=1"
if defined CI set "UI_CI=1"
set "UI_QUIET=1"
if "%UI_CI%"=="1" set "UI_QUIET=0"
if "%VERBOSE%"=="1" set "UI_QUIET=0"
if /i "%~3"=="-v" set "UI_QUIET=0"
if /i "%~3"=="--verbose" set "UI_QUIET=0"
if /i "%~4"=="-v" set "UI_QUIET=0"
if /i "%~4"=="--verbose" set "UI_QUIET=0"
set "UI_LOG=%TEMP%\objeck-deploy-%UI_ARCH%.log"
if "%UI_QUIET%"=="1" type nul > "%UI_LOG%"
if "%UI_QUIET%"=="1" set UI_REDIR=^>^>"%UI_LOG%" 2^>^&1
set "UI_E="
set "UI_RST="
set "UI_B="
set "UI_DIM="
set "UI_RED="
set "UI_GRN="
set "UI_YEL="
set "UI_BLU="
set "UI_CYN="
if "%UI_CI%"=="1" goto ui_init_nocolor
if defined NO_COLOR goto ui_init_nocolor
for /f %%a in ('echo prompt $E ^| cmd') do set "UI_E=%%a"
set "UI_RST=%UI_E%[0m"
set "UI_B=%UI_E%[1m"
set "UI_DIM=%UI_E%[90m"
set "UI_RED=%UI_E%[91m"
set "UI_GRN=%UI_E%[92m"
set "UI_YEL=%UI_E%[93m"
set "UI_BLU=%UI_E%[94m"
set "UI_CYN=%UI_E%[96m"
:ui_init_nocolor
set "UI_VER=dev"
for /f tokens^=2^ delims^=^" %%v in ('findstr /c:"define VERSION_STRING" "%~dp0..\shared\version.h" 2^>nul') do set "UI_VER=%%v"
call :ui_now UI_T0
if "%UI_CI%"=="1" goto ui_init_ci
echo.
echo %UI_CYN%  ___   _        _              _
echo  / _ \ ^| ^|__    (_)  ___   ___ ^| ^| __
echo ^| ^| ^| ^|^| '_ \   ^| ^| / _ \ / __^|^| ^|/ /
echo ^| ^|_^| ^|^| ^|_) ^|  ^| ^|^|  __/^| (__ ^|   ^<
echo  \___/ ^|_.__/  _/ ^| \___^| \___^|^|_^|\_\
echo               ^|__/%UI_RST%
echo.
echo   %UI_B%Objeck %UI_VER%%UI_RST%  %UI_DIM%windows-%UI_ARCH%, %UI_TOTAL% stages%UI_RST%
if "%UI_QUIET%"=="0" goto ui_init_loud
echo   %UI_DIM%tool output: %UI_LOG%%UI_RST%
echo   %UI_DIM%pass -v or set VERBOSE=1 to stream it%UI_RST%
:ui_init_loud
echo.
exit /b 0
:ui_init_ci
echo === Objeck %UI_VER% deploy: windows-%UI_ARCH%, %UI_TOTAL% stages ===
exit /b 0

:ui_step
set /a "UI_STEP+=1"
set "UI_LABEL=%~2"
call :ui_elapsed
set /a "_f=(UI_STEP-1)*28/UI_TOTAL, _e=28-_f, _p=(UI_STEP-1)*100/UI_TOTAL"
set "_n=  %UI_STEP%"
set "_n=%_n:~-2%/%UI_TOTAL%"
if "%UI_CI%"=="1" goto ui_step_ci
set "_b=############################"
set "_d=............................"
call set "_bar=%%_b:~0,%_f%%%%%_d:~0,%_e%%%"
set "_p=  %_p%"
set "_l=%UI_LABEL%                              "
echo   %UI_BLU%[%_bar%]%UI_RST% %_p:~-3%%%  %UI_DIM%%_n%%UI_RST%  %UI_B%%_l:~0,30%%UI_RST% %UI_DIM%%UI_ELAPSED%%UI_RST%
exit /b 0
:ui_step_ci
echo.
echo [%_n%] %UI_LABEL%  (%UI_ELAPSED%)
exit /b 0

:ui_warn
echo   %UI_YEL%!  %~2%UI_RST%
exit /b 0

:ui_fail
call :ui_elapsed
echo.
echo   %UI_RED%X  FAILED at stage %UI_STEP%/%UI_TOTAL%: %UI_LABEL%%UI_RST%  %UI_DIM%after %UI_ELAPSED%%UI_RST%
if not "%UI_QUIET%"=="1" exit /b 0
if not exist "%UI_LOG%" exit /b 0
echo   %UI_DIM%last 40 lines of %UI_LOG%:%UI_RST%
powershell -NoProfile -Command "Get-Content -LiteralPath $env:UI_LOG -Tail 40 | ForEach-Object { '    ' + $_ }"
echo   %UI_DIM%re-run with -v or VERBOSE=1 to stream every tool%UI_RST%
exit /b 0

:ui_ok
call :ui_elapsed
if "%UI_CI%"=="1" goto ui_ok_ci
echo   %UI_GRN%[############################]%UI_RST% 100%%
echo.
echo   %UI_GRN%*  %~2%UI_RST%  %UI_DIM%%UI_STEP% stages in %UI_ELAPSED%%UI_RST%
if "%UI_QUIET%"=="1" echo   %UI_DIM%tool output: %UI_LOG%%UI_RST%
exit /b 0
:ui_ok_ci
echo.
echo === %~2: %UI_STEP% stages in %UI_ELAPSED% ===
exit /b 0

:ui_elapsed
call :ui_now _t1
set /a "_el=_t1-UI_T0"
if %_el% LSS 0 set /a "_el+=86400"
set /a "_m=_el/60, _s=100+_el%%60"
set "UI_ELAPSED=%_m%m %_s:~-2%s"
exit /b 0

REM %TIME% is " 9:05:03.12" or "09:05:03,12" depending on locale. 100%%a%%100
REM reads both "9" and "09" as nine without the leading zero turning octal.
:ui_now
for /f "tokens=1-3 delims=:.," %%a in ("%TIME: =0%") do set /a "%1=(100%%a%%100)*3600+(100%%b%%100)*60+(100%%c%%100)"
exit /b 0
