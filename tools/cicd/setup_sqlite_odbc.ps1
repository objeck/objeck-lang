<#
.SYNOPSIS
Installs the SQLite ODBC driver for the current user and registers the
objeck_sqlite_test data source the ODBC regression tests connect to.

.DESCRIPTION
The ODBC regression tests (odbc_sqlite_test, odbc_whole_values,
odbc_prepared_reuse, query_source_odbc, and the ODBC half of
native_lib_gc_park) open the data source "objeck_sqlite_test", an in-memory
SQLite database reached through Christian Werner's sqliteodbc driver. On Linux
the libsqliteodbc package and ~/.odbc.ini provide it; this script is the
Windows equivalent, used by ci-build.yml and runnable on a developer box.

Nothing here needs administrator rights: the driver DLL lands in a directory
of the caller's choosing and the data source is registered under
HKCU\Software\ODBC\ODBC.INI with the DLL's full path as its Driver, which is
all the ODBC Driver Manager needs to load it for SQLConnect. No system-wide
registration is touched.

Two ways to get the DLL, both pinned to sqliteodbc 0.99991 (2023-10-23) and
verified by checksum before anything is extracted or compiled:

  x64 (default)   The upstream 64-bit installer, sqliteodbc_w64.exe. It is
                  not run: 7-Zip unpacks the NSIS archive and sqlite3odbc.dll
                  is lifted out of it. SHA-256 verified.

  ARM64, or       Upstream ships no ARM64 build, so the driver is compiled
  -FromSource     from the 0.99991 source tarball and the SQLite 3.54.1
                  amalgamation with the MSVC toolchain on PATH (cl and link,
                  as ilammy/msvc-dev-cmd leaves them). The tarball's SHA-256
                  is the one Homebrew's sqliteodbc formula records; the
                  amalgamation's SHA3-256 is the one sqlite.org publishes
                  (checked with Python, the only SHA3 at hand), and its
                  SHA-256 is pinned as well. The compile follows upstream's
                  sqlite3odbc.mak and Makefile.mingw64-cross: same defines,
                  same export list, plus legacy_stdio_definitions.lib, which
                  odbccp32.lib needs under a current CRT.

Once the DLL is in place the script opens the data source itself through
System.Data.Odbc and runs `select sqlite_version()`, so a driver that does
not load fails here, with the driver manager's message, rather than as a
regression-suite skip.

.PARAMETER Arch
x64 or ARM64. Defaults to the machine's architecture.

.PARAMETER Dir
Where the driver DLL (and, for a source build, the build tree) goes.
Defaults to $env:RUNNER_TEMP\sqliteodbc on a runner, else
$env:LOCALAPPDATA\objeck\sqliteodbc.

.PARAMETER Downloads
Where downloads are kept. A file already present whose checksum verifies is
not fetched again, so the directory can be cached between CI runs.
Defaults to <Dir>\downloads.

.PARAMETER FromSource
Build from source on x64 as well (the path ARM64 always takes), so the
source recipe can be exercised on a machine that can also run the result.

.PARAMETER Dsn
The data source name. The tests expect objeck_sqlite_test.

.EXAMPLE
pwsh tools/cicd/setup_sqlite_odbc.ps1 -Arch x64 -Dir "$env:RUNNER_TEMP\sqliteodbc"
#>
[CmdletBinding()]
param(
  [ValidateSet('x64', 'ARM64')]
  [string]$Arch = $(if ($env:PROCESSOR_ARCHITECTURE -eq 'ARM64') { 'ARM64' } else { 'x64' }),
  [string]$Dir = $(if ($env:RUNNER_TEMP) { Join-Path $env:RUNNER_TEMP 'sqliteodbc' } else { Join-Path $env:LOCALAPPDATA 'objeck\sqliteodbc' }),
  [string]$Downloads = '',
  [switch]$FromSource,
  [string]$Dsn = 'objeck_sqlite_test'
)

$ErrorActionPreference = 'Stop'
Set-StrictMode -Version 2

# ---- pinned upstream artifacts ------------------------------------------------
# sqliteodbc 0.99991, Christian Werner, https://ch-werner.hier-im-netz.de/sqliteodbc/
# (the site formerly at www.ch-werner.de; both hosts serve byte-identical files).
# Upstream publishes no checksums. The installer hash was taken from the
# download on 2026-10-24; the DLL it carries is byte-identical to the one the
# installer had placed in System32 on the maintainer's machine in 2026-05. The
# tarball hash is the one Homebrew's sqliteodbc formula pins independently.
$SQLITEODBC_VERSION   = '0.99991'
$SQLITEODBC_BASE      = 'https://ch-werner.hier-im-netz.de/sqliteodbc'
$INSTALLER_NAME       = 'sqliteodbc_w64.exe'
$INSTALLER_SHA256     = 'a4804e4f54f42c721df1323c5fcac101a8c7a577e7f20979227324ceab572d51'
$TARBALL_NAME         = "sqliteodbc-$SQLITEODBC_VERSION.tar.gz"
$TARBALL_SHA256       = '4d94adb8d3cde1fa94a28aeb0dfcc7be73145bcdfcdf3d5e225434db31dc8a5c'
# SQLite 3.54.1 amalgamation, https://sqlite.org/download.html, which publishes the SHA3-256.
$SQLITE_VERSION       = '3.54.1'
$AMALGAMATION_NAME    = 'sqlite-amalgamation-3540100.zip'
$AMALGAMATION_URL     = "https://sqlite.org/2026/$AMALGAMATION_NAME"
$AMALGAMATION_SHA3    = '92281c86e2957f9b9305890f9ac04204caa7b677cc868dc326d942689df28e30'
$AMALGAMATION_SHA256  = '0a9c8e34352dad46006aa82836d5383bff9dc6bc9ce5a60553c05aa30e6ad1f9'

$DRIVER_NAME = 'SQLite3 ODBC Driver'

if (-not $Downloads) { $Downloads = Join-Path $Dir 'downloads' }
New-Item -ItemType Directory -Force -Path $Dir, $Downloads | Out-Null
$Dir = (Resolve-Path $Dir).Path
$Downloads = (Resolve-Path $Downloads).Path
$dll = Join-Path $Dir 'sqlite3odbc.dll'

# Write-Host throughout: a function's Write-Output lines would join its return
# value, and Fetch returns a path.
function Step([string]$text) { Write-Host "--- $text" }

function Sha256([string]$path) { (Get-FileHash -Algorithm SHA256 $path).Hash.ToLowerInvariant() }

function Sha3_256([string]$path) {
  # Get-FileHash has no SHA3. Python's hashlib does, and every CI image has Python.
  $py = Get-Command python -ErrorAction SilentlyContinue
  if (-not $py) { $py = Get-Command python3 -ErrorAction SilentlyContinue }
  if (-not $py) { throw "python is needed to verify the SHA3-256 sqlite.org publishes for $path" }
  # single quotes inside: Windows PowerShell strips inner double quotes from a native command's arguments
  $out = & $py.Source -I -c "import hashlib,sys; print(hashlib.sha3_256(open(sys.argv[1],'rb').read()).hexdigest())" $path
  if ($LASTEXITCODE -ne 0) { throw "python could not hash $path" }
  return ($out | Select-Object -Last 1).Trim().ToLowerInvariant()
}

# Fetch <url> to <Downloads>\<name> unless a copy with the right SHA-256 is already
# there. The checksum is checked after every download; a mismatch deletes the file
# and fails -- nothing downstream ever sees an unverified byte.
function Fetch([string]$name, [string]$url, [string]$sha256) {
  $path = Join-Path $Downloads $name
  if (Test-Path $path) {
    if ((Sha256 $path) -eq $sha256) {
      Write-Host "  $name already present, SHA-256 verified"
      return $path
    }
    Write-Host "  $name present but its SHA-256 does not match; refetching"
    Remove-Item -Force $path
  }
  # Same transient-CDN-failure class as the vcpkg installs in ci-build.yml: bounded retry.
  $max = 4
  for ($i = 1; $i -le $max; $i++) {
    try {
      Write-Host "  downloading $url"
      Invoke-WebRequest -Uri $url -OutFile $path -UseBasicParsing
      break
    }
    catch {
      if ($i -eq $max) { throw }
      Write-Host "  download failed (attempt $i/$max): $($_.Exception.Message); retrying in 15s"
      Start-Sleep -Seconds 15
    }
  }
  $got = Sha256 $path
  if ($got -ne $sha256) {
    Remove-Item -Force $path
    throw "SHA-256 mismatch for ${name}: expected $sha256, got $got. The download was discarded."
  }
  Write-Host "  SHA-256 verified: $got"
  return $path
}

# ---- 1. the driver DLL ---------------------------------------------------------
$buildFromSource = $FromSource -or ($Arch -eq 'ARM64')
if (-not $buildFromSource) {
  Step "sqliteodbc $SQLITEODBC_VERSION x64: unpack the upstream installer (not run)"
  $installer = Fetch $INSTALLER_NAME "$SQLITEODBC_BASE/$INSTALLER_NAME" $INSTALLER_SHA256
  $sevenZip = Get-Command 7z -ErrorAction SilentlyContinue
  if (-not $sevenZip) {
    $candidate = Join-Path $env:ProgramFiles '7-Zip\7z.exe'
    if (Test-Path $candidate) { $sevenZip = Get-Command $candidate } else { throw '7z is needed to unpack the NSIS installer' }
  }
  $unpack = Join-Path $Dir 'installer'
  if (Test-Path $unpack) { Remove-Item -Recurse -Force $unpack }
  & $sevenZip.Source x -y "-o$unpack" $installer | Out-Null
  if ($LASTEXITCODE -ne 0) { throw "7z could not unpack $installer" }
  $unpacked = Join-Path $unpack 'sqlite3odbc.dll'
  if (-not (Test-Path $unpacked)) { throw "the installer did not contain sqlite3odbc.dll" }
  Copy-Item -Force $unpacked $dll
  Write-Host "  sqlite3odbc.dll $((Get-Item $dll).VersionInfo.FileVersion), SHA-256 $(Sha256 $dll)"
}
else {
  Step "sqliteodbc $SQLITEODBC_VERSION ${Arch}: build from source (upstream ships no ARM64 driver)"
  foreach ($tool in 'cl', 'link') {
    if (-not (Get-Command $tool -ErrorAction SilentlyContinue)) {
      throw "$tool is not on PATH; run from an MSVC developer environment for $Arch"
    }
  }
  $tarball = Fetch $TARBALL_NAME "$SQLITEODBC_BASE/$TARBALL_NAME" $TARBALL_SHA256
  $amalgamation = Fetch $AMALGAMATION_NAME $AMALGAMATION_URL $AMALGAMATION_SHA256
  $sha3 = Sha3_256 $amalgamation
  if ($sha3 -ne $AMALGAMATION_SHA3) {
    throw "SHA3-256 mismatch for ${AMALGAMATION_NAME}: sqlite.org publishes $AMALGAMATION_SHA3, got $sha3"
  }
  Write-Host "  SHA3-256 verified against sqlite.org: $sha3"

  $build = Join-Path $Dir 'build'
  if (Test-Path $build) { Remove-Item -Recurse -Force $build }
  New-Item -ItemType Directory -Force -Path $build | Out-Null
  # Windows ships bsdtar as tar.exe; Expand-Archive handles the zip.
  & tar -xzf $tarball -C $build "sqliteodbc-$SQLITEODBC_VERSION/sqlite3odbc.c" "sqliteodbc-$SQLITEODBC_VERSION/sqlite3odbc.h" `
      "sqliteodbc-$SQLITEODBC_VERSION/sqlite3odbc.def" "sqliteodbc-$SQLITEODBC_VERSION/resource.h.in" "sqliteodbc-$SQLITEODBC_VERSION/license.terms"
  if ($LASTEXITCODE -ne 0) { throw "tar could not extract $tarball" }
  Expand-Archive -Path $amalgamation -DestinationPath $build -Force
  $src = Join-Path $build "sqliteodbc-$SQLITEODBC_VERSION"
  $amal = Join-Path $build ($AMALGAMATION_NAME -replace '\.zip$', '')
  foreach ($f in 'sqlite3.c', 'sqlite3.h', 'sqlite3ext.h') { Copy-Item -Force (Join-Path $amal $f) $src }
  # upstream's fixup generates resource3.h from resource.h.in: VERSION as a
  # string and VERSION_C with the dot turned into a comma
  (Get-Content (Join-Path $src 'resource.h.in')) -replace '--VERS--', $SQLITEODBC_VERSION -replace '--VERS_C--', ($SQLITEODBC_VERSION -replace '\.', ',') |
    Set-Content -Path (Join-Path $src 'resource3.h') -Encoding ascii
  # the .def's DESCRIPTION/SEGMENTS lines are 16-bit era; link ignores them with a warning each
  (Get-Content (Join-Path $src 'sqlite3odbc.def')) | Where-Object { $_ -notmatch '^(DESCRIPTION|SEGMENTS|DLL_TEXT|INIT_TEXT|HEAPSIZE)\b' } |
    Set-Content -Path (Join-Path $src 'sqlite3odbc.def') -Encoding ascii

  Push-Location $src
  try {
    # Defines: sqlite3odbc.mak (the MSVC makefile) plus the SQLITE3_FLAGS,
    # SQLITE3_A10N_FLAGS and ODBC_FLAGS of Makefile.mingw64-cross, which is
    # how the shipped 64-bit DLL is built.
    $defines = @(
      '-D_WIN32', '-DWIN32=1', '-DNDEBUG=1', '-DNO_TCL', '-DTHREADSAFE=1',
      '-DSQLITE_THREADSAFE=1', '-DSQLITE_OS_WIN=1', '-DSQLITE_ENABLE_COLUMN_METADATA=1', '-DSQLITE_SOUNDEX=1', '-DSQLITE_ASCII=1',
      '-DHAVE_SQLITE3COLUMNTABLENAME=1', '-DHAVE_SQLITE3COLUMNDATABASENAME=1', '-DHAVE_SQLITE3COLUMNORIGINNAME=1',
      '-DHAVE_SQLITE3LOADEXTENSION=1', '-DHAVE_SQLITE3PREPAREV2=1', '-DHAVE_SQLITE3VFS=1', '-DHAVE_SQLITE3PROFILE=1',
      '-DHAVE_SQLITE3CLOSEV2=1', '-DHAVE_SQLITE3STRNICMP=1', '-DHAVE_SQLITE3TABLECOLUMNMETADATA=1',
      '-DHAVE_LONG_LONG=1', '-DHAVE_SQLROWOFFSET=1', '-DHAVE_SQLLEN=1', '-DHAVE_SQLULEN=1', '-DHAVE_SQLROWCOUNT=1',
      '-DHAVE_SQLSETPOSIROW=1', '-DPTRDIFF_T=ptrdiff_t', '-DWITHOUT_SHELL=1'
    )
    Write-Host "  cl /O2 /MD sqlite3odbc.c sqlite3.c (SQLite $SQLITE_VERSION)"
    & cl /nologo /O2 /MD /W1 /I. @defines /c sqlite3odbc.c sqlite3.c
    if ($LASTEXITCODE -ne 0) { throw "cl failed ($LASTEXITCODE)" }
    & link /nologo /DLL /DEF:sqlite3odbc.def /OUT:sqlite3odbc.dll sqlite3odbc.obj sqlite3.obj `
        odbc32.lib odbccp32.lib kernel32.lib user32.lib comdlg32.lib legacy_stdio_definitions.lib
    if ($LASTEXITCODE -ne 0) { throw "link failed ($LASTEXITCODE)" }
    Copy-Item -Force (Join-Path $src 'sqlite3odbc.dll') $dll
  }
  finally { Pop-Location }
  $machine = (& dumpbin /headers $dll | Select-String 'machine \(').Line.Trim()
  Write-Host "  built sqlite3odbc.dll: $machine, SHA-256 $(Sha256 $dll)"
}

# ---- 2. the data source, per user ----------------------------------------------
Step "register user DSN '$Dsn' -> $dll (in-memory database)"
$odbcIni = 'HKCU:\Software\ODBC\ODBC.INI'
$dsnKey = Join-Path $odbcIni $Dsn
$sourcesKey = Join-Path $odbcIni 'ODBC Data Sources'
New-Item -Path $dsnKey -Force | Out-Null
New-Item -Path $sourcesKey -Force | Out-Null
# The Driver value is the path the Driver Manager loads for this DSN; the
# "ODBC Data Sources" entry is what lists it (and what SQLDataSources returns).
Set-ItemProperty -Path $dsnKey -Name 'Driver' -Value $dll
Set-ItemProperty -Path $dsnKey -Name 'Database' -Value ':memory:'
Set-ItemProperty -Path $dsnKey -Name 'Description' -Value 'Objeck ODBC regression test DSN'
Set-ItemProperty -Path $sourcesKey -Name $Dsn -Value $DRIVER_NAME

# ---- 3. prove it loads ---------------------------------------------------------
Step "connect through the ODBC Driver Manager"
Add-Type -AssemblyName System.Data
$conn = New-Object System.Data.Odbc.OdbcConnection "DSN=$Dsn"
try {
  $conn.Open()
  $cmd = $conn.CreateCommand()
  $cmd.CommandText = 'select sqlite_version()'
  $sqliteVersion = $cmd.ExecuteScalar()
  Write-Host "  connected: driver '$($conn.Driver)', SQLite $sqliteVersion"
}
catch {
  throw "the data source '$Dsn' is registered but a connection failed: $($_.Exception.Message)"
}
finally { $conn.Dispose() }

Write-Host "ODBC data source '$Dsn' is ready: sqliteodbc $SQLITEODBC_VERSION ($Arch$(if ($buildFromSource) { ', built from source' })) at $dll"
