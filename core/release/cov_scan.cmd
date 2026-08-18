@echo off
setlocal enabledelayedexpansion

REM ===========================================================================
REM Coverity Scan upload, Windows. Manual developer script -- CI does not run
REM this. Companion to cov_scan.sh, which covers the Linux build.
REM
REM Usage:  cov_scan.cmd            (from core\release)
REM
REM ---------------------------------------------------------------------------
REM Token -- never in the tree:
REM
REM   set COVERITY_TOKEN=...        from scan.coverity.com -> Project Settings
REM   set COVERITY_TOKEN_FILE=...   or a file containing just the token
REM
REM COVERITY_TOKEN wins when both are set. With neither, the default path below
REM is read, so a bare cov_scan.cmd works with no env prefix. Keep that file
REM OUTSIDE the repository -- an in-tree token is one 'git add -A' from being
REM published, which is exactly how the original leaked (public 2019-08-11 to
REM 2026-08, and now rotated). Never paste the value back into this file.
REM
REM ---------------------------------------------------------------------------
REM Toolchain:
REM
REM   set COVERITY_HOME=...   install root -- the dir holding bin\cov-build.exe
REM
REM Unset, the highest-sorting cov-analysis-win64-* under
REM %USERPROFILE%\Documents\Code is used, then PATH. Discovery is by glob rather
REM than a literal path so a toolchain upgrade does not silently go stale in the
REM tree the way the old hardcoded home directory did.
REM
REM ---------------------------------------------------------------------------
REM Build:
REM
REM   set COVERITY_BUILD=...   full build command to capture (optional)
REM   set OBJECK_TOOLSET=v143  toolset override, for a VS older than the projects
REM
REM Default is a REBUILD of core\release\objeck.sln -- compiler, VM, debugger,
REM module and REPL. The rebuild is not optional: cov-build can only capture
REM translation units that actually compile, so an incremental build emits
REM nothing and would upload an empty scan.
REM
REM COVERAGE GAP, deliberate and worth knowing: this default does NOT cover the
REM native libraries (crypto, lame, diags, odbc, onnx, opencv), which the Linux
REM scan does reach via deploy_posix.sh -- the 2026-08-17 ONNX findings came from
REM there. Building them on Windows means deploy_windows.cmd, which wipes
REM deploy-x64 before it checks for a VS environment and cannot take the toolset
REM override. Point COVERITY_BUILD at it deliberately if you want that coverage:
REM   set COVERITY_BUILD=deploy_windows.cmd x64
REM
REM The submitted version is read from version.h, never hardcoded -- a literal
REM would keep reporting a stale version to Coverity after every bump.
REM ===========================================================================

pushd "%~dp0"

REM --------------------------------------------------------------- token ----
if not defined COVERITY_TOKEN_FILE set "COVERITY_TOKEN_FILE=%USERPROFILE%\Documents\Code\cov_token.dat"

if not defined COVERITY_TOKEN (
	if exist "%COVERITY_TOKEN_FILE%" (
		for /f "usebackq delims=" %%T in ("%COVERITY_TOKEN_FILE%") do (
			if not defined COVERITY_TOKEN set "COVERITY_TOKEN=%%T"
		)
	)
)

if not defined COVERITY_TOKEN (
	echo ERROR: no Coverity token. Set COVERITY_TOKEN, or put the token in 1>&2
	echo        "%COVERITY_TOKEN_FILE%" 1>&2
	echo        ^(override that path with COVERITY_TOKEN_FILE^). 1>&2
	popd & exit /b 1
)

REM ----------------------------------------------------------- toolchain ----
if not defined COVERITY_HOME (
	for /f "delims=" %%D in ('dir /b /ad /o-n "%USERPROFILE%\Documents\Code\cov-analysis-win64-*" 2^>nul') do (
		if not defined COVERITY_HOME set "COVERITY_HOME=%USERPROFILE%\Documents\Code\%%D"
	)
)

set "COV_BUILD="
set "COV_CONFIGURE="
if defined COVERITY_HOME (
	if exist "%COVERITY_HOME%\bin\cov-build.exe" (
		set "COV_BUILD=%COVERITY_HOME%\bin\cov-build.exe"
		set "COV_CONFIGURE=%COVERITY_HOME%\bin\cov-configure.exe"
		set "COV_MANAGE=%COVERITY_HOME%\bin\cov-manage-emit.exe"
	)
)
if not defined COV_BUILD (
	for %%P in (cov-build.exe) do set "COV_BUILD=%%~$PATH:P"
	for %%P in (cov-configure.exe) do set "COV_CONFIGURE=%%~$PATH:P"
	for %%P in (cov-manage-emit.exe) do set "COV_MANAGE=%%~$PATH:P"
)
if not defined COV_BUILD (
	echo ERROR: cov-build not found. Set COVERITY_HOME to your Coverity install 1>&2
	echo        root, or put its bin\ directory on PATH. 1>&2
	popd & exit /b 1
)
if not exist "%COV_CONFIGURE%" (
	echo ERROR: found cov-build but not cov-configure alongside it -- the install 1>&2
	echo        at "%COVERITY_HOME%" looks incomplete. 1>&2
	popd & exit /b 1
)

REM ------------------------------------------------------------- version ----
set "VERSION_H=..\shared\version.h"
if not exist "%VERSION_H%" (
	echo ERROR: cannot read %VERSION_H% -- run this script from core\release. 1>&2
	popd & exit /b 1
)

set "VERSION="
for /f tokens^=2^ delims^=^" %%V in ('"%SystemRoot%\System32indstr.exe" /c:"#define VERSION_STRING" "%VERSION_H%"') do set "VERSION=%%V"
if not defined VERSION (
	echo ERROR: no VERSION_STRING found in %VERSION_H% -- has the define been renamed? 1>&2
	popd & exit /b 1
)

REM ---------------------------------------------------- VS build environment -
if not defined VCINSTALLDIR (
	set "VSWHERE=%ProgramFiles(x86)%\Microsoft Visual Studio\Installer\vswhere.exe"
	if not exist "!VSWHERE!" (
		echo ERROR: no VS build environment and vswhere.exe not found. Run this from 1>&2
		echo        a Developer Command Prompt. 1>&2
		popd & exit /b 1
	)
	set "VSPATH="
	for /f "usebackq delims=" %%I in (`"!VSWHERE!" -latest -products * -requires Microsoft.Component.MSBuild -property installationPath`) do set "VSPATH=%%I"
	if not defined VSPATH (
		echo ERROR: vswhere found no Visual Studio install with MSBuild. 1>&2
		popd & exit /b 1
	)
	call "!VSPATH!\Common7\Tools\VsDevCmd.bat" -arch=amd64 -host_arch=amd64 >nul
	if errorlevel 1 (
		echo ERROR: failed to initialize the VS build environment. 1>&2
		popd & exit /b 1
	)
)

REM --------------------------------------------------------- build command ---
if not defined COVERITY_BUILD (
	set "MSBUILD_ARGS=-p:Configuration=Release -p:Platform=x64 -t:Rebuild -m -v:minimal -nologo"
	if defined OBJECK_TOOLSET set "MSBUILD_ARGS=!MSBUILD_ARGS! -p:PlatformToolset=!OBJECK_TOOLSET!"
	set "COVERITY_BUILD=msbuild objeck.sln !MSBUILD_ARGS!"
)

REM The intermediate directory MUST be named cov-int, and the archive must contain
REM it at the top level -- that is what Coverity Scan unpacks and analyzes, and
REM it is what cov_scan.sh produces (tar -czf objeck-int.tgz -C /tmp/ cov-int/).
REM Wrapping it under any other name uploads fine and then fails server-side.
set "COV_DIR=%TEMP%\cov-int"
set "COV_CONFIG=%TEMP%\objeck-cov-conf\coverity_config.xml"
set "COV_LOG=%TEMP%\objeck-cov-build.log"
set "COV_TU_LIST=%TEMP%\objeck-cov-tu.txt"
set "ARCHIVE=%TEMP%\objeck-int.tgz"

echo.
echo ============================================================
echo  Coverity Scan: Objeck %VERSION% ^(Windows x64^)
echo ============================================================
echo  cov-build : %COV_BUILD%
echo  build     : %COVERITY_BUILD%
echo  intdir    : %COV_DIR%
echo.

REM Staging lives under %TEMP%, never in the tree -- the emit directory is large
REM and an in-tree copy is one 'git add -A' from being committed.
if exist "%COV_DIR%" rmdir /s /q "%COV_DIR%"
if exist "%TEMP%\objeck-cov-conf" rmdir /s /q "%TEMP%\objeck-cov-conf"
if exist "%ARCHIVE%" del /q "%ARCHIVE%"

REM cov-configure must run before cov-build or MSVC compilations are not
REM recognised and the emit comes back empty.
echo Configuring the MSVC compiler...
"%COV_CONFIGURE%" --config "%COV_CONFIG%" --msvc >nul
if errorlevel 1 (
	echo ERROR: cov-configure failed. 1>&2
	popd & exit /b 1
)

echo Building under cov-build ^(full rebuild, this takes a while^)...
"%COV_BUILD%" --dir "%COV_DIR%" --config "%COV_CONFIG%" cmd /c "%COVERITY_BUILD%" > "%COV_LOG%" 2>&1
set "BUILD_RC=%ERRORLEVEL%"
type "%COV_LOG%" | "%SystemRoot%\System32indstr.exe" /i /c:"compilation units" /c:"ready for analysis" /c:"error"

if not "%BUILD_RC%"=="0" (
	echo. 1>&2
	echo ERROR: the build failed under cov-build ^(exit %BUILD_RC%^). Full log: 1>&2
	echo        %COV_LOG% 1>&2
	popd & exit /b 1
)

REM A build that compiled nothing still exits 0 and still uploads -- Coverity
REM accepts an empty emit and reports a CLEAN scan, which is indistinguishable
REM from having no defects. Refuse to submit one.
REM
REM Both checks below were verified against a deliberately empty capture, because
REM the obvious ones do not work: cov-build does NOT print "0 compilation units"
REM (it prints the warning matched here), the emit\ directory IS created even when
REM nothing is captured, and cov-manage-emit exits 0 on an empty emit. Only the
REM warning string and the LINE COUNT distinguish the two cases.
"%SystemRoot%\System32indstr.exe" /c:"No files were emitted" "%COV_LOG%" >nul 2>&1
if not errorlevel 1 (
	echo. 1>&2
	echo ERROR: cov-build emitted no files -- nothing would be analyzed. 1>&2
	echo        Usually an incremental build rather than a rebuild, or a compiler 1>&2
	echo        cov-configure does not recognise. Not uploading. 1>&2
	echo        Log: %COV_LOG% 1>&2
	popd & exit /b 1
)

REM Counted via a temp file rather than a nested-quote FOR /F pipeline. The
REM inline form ('"cmd" args | find /c /v ""') does not parse in cmd -- it fails
REM with "invalid command list", leaves the count at 0, and then rejects a
REM PERFECTLY GOOD capture. That happened on the first real run here.
set "TU_COUNT=0"
if exist "%COV_MANAGE%" (
	"%COV_MANAGE%" --dir "%COV_DIR%" list > "%COV_TU_LIST%" 2>nul
	for /f %%N in ('"%SystemRoot%\System32ind.exe" /c /v "" ^< "%COV_TU_LIST%"') do set "TU_COUNT=%%N"
	del /q "%COV_TU_LIST%" 2>nul
	if "!TU_COUNT!"=="0" (
		echo. 1>&2
		echo ERROR: the emit contains 0 translation units -- nothing would be analyzed. 1>&2
		echo        Not uploading. Log: %COV_LOG% 1>&2
		popd & exit /b 1
	)
	echo Captured !TU_COUNT! translation unit^(s^).
)

REM Coverity reports the share of attempted units it managed to emit. Anything
REM well short of 100%% means whole projects were skipped and would be reported
REM as clean because they were never analyzed -- the same blind spot as an empty
REM emit, just harder to notice. Surface it loudly rather than silently submit.
for /f "tokens=1 delims= " %%P in ('"%SystemRoot%\System32indstr.exe" /c:"are ready for analysis" "%COV_LOG%"') do set "READY=%%P"
"%SystemRoot%\System32indstr.exe" /c:"(100%%)" "%COV_LOG%" >nul 2>&1
if errorlevel 1 (
	echo.
	echo WARNING: capture is INCOMPLETE -- not every compilation unit was emitted.
	"%SystemRoot%\System32indstr.exe" /c:"compilation units" "%COV_LOG%"
	echo          Whole projects missing from the emit are reported as clean because
	echo          they were never analyzed. Check %COV_DIR%\build-log.txt for
	echo          "not supported in the current release" before trusting the results.
	echo.
)

echo.
echo Archiving the intermediate directory...
REM -C into %TEMP% and add the bare directory name, so the archive holds
REM cov-int/... and not an absolute or differently-named path. Same shape as the
REM Linux script's 'tar -czf objeck-int.tgz -C /tmp/ cov-int/'.
tar -czf "%ARCHIVE%" -C "%TEMP%" cov-int
set "TAR_RC=%ERRORLEVEL%"
if not "%TAR_RC%"=="0" (
	echo ERROR: tar failed ^(exit %TAR_RC%^). 1>&2
	popd & exit /b 1
)

REM Confirm the archive really holds cov-int/ at the top level. A wrongly-named or
REM nested directory uploads with HTTP 200 and only fails later, server-side,
REM where the cause is invisible from here -- so check it now, before spending
REM the upload.
tar -tzf "%ARCHIVE%" | "%SystemRoot%\System32indstr.exe" /b /c:"cov-int/" >nul 2>&1
if errorlevel 1 (
	echo. 1>&2
	echo ERROR: %ARCHIVE% does not contain a top-level cov-int/ directory. 1>&2
	echo        Coverity Scan would accept the upload and then fail to analyze it. 1>&2
	echo        Archive contains: 1>&2
	tar -tzf "%ARCHIVE%" 2>nul | "%SystemRoot%\System32indstr.exe" /n "^" | "%SystemRoot%\System32indstr.exe" /b /c:"1:" /c:"2:" 1>&2
	popd & exit /b 1
)

echo Uploading to Coverity Scan...
curl --fail-with-body ^
  --form token="%COVERITY_TOKEN%" ^
  --form email=objeck@gmail.com ^
  --form file=@"%ARCHIVE%" ^
  --form version="%VERSION%" ^
  --form description="Objeck %VERSION% (Windows x64)" ^
  https://scan.coverity.com/builds?project=Objeck
if errorlevel 1 (
	echo. 1>&2
	echo ERROR: upload failed. The archive is kept at %ARCHIVE% 1>&2
	echo        so it can be retried without another full rebuild. 1>&2
	popd & exit /b 1
)

del /q "%ARCHIVE%"
rmdir /s /q "%COV_DIR%"

echo.
echo ============================================================
echo  Submitted Objeck %VERSION% ^(Windows x64^)
echo  Results: https://scan.coverity.com/projects/objeck
echo ============================================================
popd
exit /b 0
