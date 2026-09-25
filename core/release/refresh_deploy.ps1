<#
.SYNOPSIS
    Rebuild the Objeck toolchain with MSBuild and refresh deploy-<arch> in place.

.DESCRIPTION
    deploy_windows.cmd drives every build with `devenv`, which ships only with
    the full Visual Studio IDE and not with Build Tools. On a box that has only
    Build Tools it cannot run at all, so the rebuild-and-refresh loop has been
    done by hand: MSBuild the solution, copy four executables, re-embed two
    manifests, remember which build you installed.

    This is that loop, with the parts that are easy to get wrong made explicit.

    It differs from deploy_windows.cmd in three ways that matter:

      * It uses MSBuild located through vswhere, so no Developer Command Prompt
        and no IDE are required.
      * It NEVER deletes the deploy tree. deploy_windows.cmd rebuilds it from
        scratch, which is right for a release and wrong for the twentieth
        rebuild of an afternoon. Files are replaced individually.
      * It records what it installed. Every binary it copies is hashed into
        deploy-<arch>\DEPLOY_MANIFEST.txt with the commit it was built from, so
        "is this my compiler or someone else's?" has an answer. That question
        cost real time on 2026-09-24, when a deploy binary had been replaced by
        another machine's build reporting the same version string, and a test
        run against it proved the opposite of what it appeared to prove.

    It does NOT replace deploy_windows.cmd for cutting a release: it does not
    build the installer, generate API docs, or stage examples.

.PARAMETER Arch
    x64 (default) or arm64.

.PARAMETER Native
    Also build AND INSTALL the eleven native library solutions. Slow, and they change
    rarely, so this is off by default.

.PARAMETER SkipBuild
    Copy existing build output without rebuilding. Useful straight after a
    build done by hand or by an IDE.

.PARAMETER WhatIf
    Report what would be built and copied, and change nothing.

.EXAMPLE
    powershell -ExecutionPolicy Bypass -File core\release\refresh_deploy.ps1
    powershell -ExecutionPolicy Bypass -File core\release\refresh_deploy.ps1 -Native
    powershell -ExecutionPolicy Bypass -File core\release\refresh_deploy.ps1 -WhatIf
#>
[CmdletBinding()]
param(
    [ValidateSet('x64', 'arm64')]
    [string] $Arch = 'x64',
    [switch] $Native,
    [switch] $SkipBuild,
    [switch] $WhatIf
)

$ErrorActionPreference = 'Stop'

$repo = Resolve-Path (Join-Path $PSScriptRoot '..\..')
$target = Join-Path $repo ("core\release\deploy-" + $Arch)
$platform = if ($Arch -eq 'arm64') { 'ARM64' } else { 'x64' }

# Where the solution leaves each executable for an x64 build. ARM64 builds land
# in a single solution-level directory instead.
$binSources = @(
    'core\compiler\release\win64',
    'core\repl\release\win64',
    'core\vm\release\win64',
    'core\debugger\release\win64'
)

# Release|<platform> for all of these, except onnx which builds Release-DML.
$nativeProjects = @(
    @{ Path = 'core\utils\launcher\native_launcher.sln'; Config = 'Release' },
    @{ Path = 'core\utils\updater\vs\obu.vcxproj';       Config = 'Release' },
    @{ Path = 'core\lib\crypto\crypto.sln';              Config = 'Release' },
    @{ Path = 'core\lib\lame\lame.sln';                  Config = 'Release' },
    @{ Path = 'core\utils\WindowsApp\AppLauncher.sln';   Config = 'Release' },
    @{ Path = 'core\lib\diags\diag.sln';                 Config = 'Release' },
    @{ Path = 'core\lib\odbc\odbc.sln';                  Config = 'Release' },
    @{ Path = 'core\lib\matrix\matrix.sln';              Config = 'Release' },
    @{ Path = 'core\lib\opencv\opencv.sln';              Config = 'Release' },
    @{ Path = 'core\lib\onnx\onnx.sln';                  Config = 'Release-DML' },
    @{ Path = 'core\lib\sdl\sdl\sdl.sln';                Config = 'Release' }
)

function Find-MSBuild {
    $vswhere = Join-Path ${env:ProgramFiles(x86)} 'Microsoft Visual Studio\Installer\vswhere.exe'
    if (Test-Path $vswhere) {
        $found = & $vswhere -latest -products * `
            -requires Microsoft.Component.MSBuild `
            -find 'MSBuild\**\Bin\MSBuild.exe' 2>$null
        if ($found) { return ($found | Select-Object -First 1) }
    }
    $cmd = Get-Command MSBuild.exe -ErrorAction SilentlyContinue
    if ($cmd) { return $cmd.Source }
    return $null
}

function Find-Mt {
    # mt.exe lives in the Windows SDK. On an ARM64 target the x64 host tool is
    # the one that runs, which is why this does not look under an arm64 dir.
    $roots = @("${env:ProgramFiles(x86)}\Windows Kits\10\bin", "$env:ProgramFiles\Windows Kits\10\bin")
    foreach ($root in $roots) {
        if (-not (Test-Path $root)) { continue }
        $hit = Get-ChildItem -Path $root -Filter mt.exe -Recurse -ErrorAction SilentlyContinue |
               Where-Object { $_.FullName -match '\\x64\\' } |
               Sort-Object FullName -Descending |
               Select-Object -First 1
        if ($hit) { return $hit.FullName }
    }
    return $null
}

function Invoke-Build {
    param([string] $MSBuild, [string] $ProjectPath, [string] $Config)
    $full = Join-Path $repo $ProjectPath
    if (-not (Test-Path $full)) {
        throw "project not found: $ProjectPath"
    }
    Write-Host ("  building {0}  ({1}|{2})" -f $ProjectPath, $Config, $platform)
    if ($WhatIf) { return }
    & $MSBuild $full "-p:Configuration=$Config" "-p:Platform=$platform" -m -v:minimal -nologo
    if ($LASTEXITCODE -ne 0) {
        throw "MSBuild failed for $ProjectPath (exit $LASTEXITCODE)"
    }
}

# --- prerequisites, checked BEFORE anything is written ---------------------

Write-Host ''
Write-Host ("Objeck deploy refresh -- {0}" -f $Arch)
Write-Host ('=' * 52)

$msbuild = Find-MSBuild
if (-not $msbuild) {
    Write-Host 'MSBuild not found.' -ForegroundColor Red
    Write-Host 'Install Visual Studio Build Tools, or run from a Developer prompt.'
    exit 1
}
Write-Host ("  msbuild : {0}" -f $msbuild)

if (-not (Test-Path $target)) {
    Write-Host ("  target  : {0}  (will be created)" -f $target)
    if (-not $WhatIf) { New-Item -ItemType Directory -Path $target | Out-Null }
} else {
    Write-Host ("  target  : {0}" -f $target)
}
$targetBin = Join-Path $target 'bin'
if (-not (Test-Path $targetBin) -and -not $WhatIf) {
    New-Item -ItemType Directory -Path $targetBin | Out-Null
}

$commit = 'unknown'
try {
    $commit = (& git -C $repo rev-parse --short HEAD 2>$null)
    if (-not $commit) { $commit = 'unknown' }
} catch {
    $commit = 'unknown'
}
Write-Host ("  commit  : {0}" -f $commit)
if ($WhatIf) { Write-Host '  MODE    : WhatIf -- nothing will be built or copied' -ForegroundColor Yellow }
Write-Host ''

# --- build -----------------------------------------------------------------

if ($SkipBuild) {
    Write-Host 'Skipping build (-SkipBuild).'
} else {
    # The SOLUTION, not the individual .vcxproj: repl.vcxproj links objeck.lib
    # from the solution-level output directory and fails with LNK1181 alone.
    Invoke-Build -MSBuild $msbuild -ProjectPath 'core\release\objeck.sln' -Config 'Release'

    if ($Native) {
        foreach ($project in $nativeProjects) {
            Invoke-Build -MSBuild $msbuild -ProjectPath $project.Path -Config $project.Config
        }
    } else {
        Write-Host '  (native libraries skipped; pass -Native to build them)'
    }
}
Write-Host ''

# --- collect the executables ----------------------------------------------

$staged = @()
if ($Arch -eq 'arm64') {
    $arm = Join-Path $repo 'core\release\ARM64\Release'
    if (Test-Path $arm) {
        $staged += Get-ChildItem -Path $arm -Filter *.exe -ErrorAction SilentlyContinue
    }
} else {
    foreach ($relative in $binSources) {
        $dir = Join-Path $repo $relative
        if (Test-Path $dir) {
            $staged += Get-ChildItem -Path $dir -Filter *.exe -ErrorAction SilentlyContinue
        } else {
            Write-Host ("  note: no build output at {0}" -f $relative) -ForegroundColor Yellow
        }
    }
}

if ($staged.Count -eq 0) {
    Write-Host 'No executables found to install.' -ForegroundColor Red
    Write-Host 'Build first, or drop -SkipBuild.'
    exit 1
}

# --- install, recording what changed ---------------------------------------

# What was installed last time, keyed by name -> source hash.
#
# The comparison has to be against the SOURCE hash, not against the file now
# sitting in bin: mt.exe embeds the manifest in place, after the copy, so a
# manifested binary no longer hashes to its build output. Comparing the two
# reports obi.exe as replaced on every single run, and records in the manifest a
# hash that is not on disk -- which defeats the one question the file exists to
# answer. obr.exe hides this, because vm.vcxproj already embeds its manifest and
# mt.exe is then a no-op.
$previous = @{}
$manifestPath = Join-Path $target 'DEPLOY_MANIFEST.txt'
if (Test-Path $manifestPath) {
    foreach ($line in (Get-Content $manifestPath)) {
        if ($line -match '^\s*(\S+\.exe)\s+source=([0-9A-Fa-f]+)') {
            $previous[$matches[1]] = $matches[2]
        }
    }
}

Write-Host 'Installing:'
$records = @()
foreach ($file in $staged) {
    $destination = Join-Path $targetBin $file.Name
    $source = (Get-FileHash $file.FullName -Algorithm MD5).Hash.Substring(0, 12)
    $recorded = $previous[$file.Name]
    $present = Test-Path $destination

    if ($present -and $recorded -eq $source) {
        Write-Host ("  {0,-12} unchanged  source {1}" -f $file.Name, $source)
    } elseif ($WhatIf) {
        Write-Host ("  {0,-12} would go   source {1} -> {2}" -f $file.Name, $(if ($recorded) { $recorded } else { '(unrecorded)' }), $source)
    } else {
        Copy-Item $file.FullName $destination -Force
        Write-Host ("  {0,-12} installed  source {1} -> {2}" -f $file.Name, $(if ($recorded) { $recorded } else { '(unrecorded)' }), $source)
    }
    $records += [pscustomobject]@{ Name = $file.Name; Source = $source; Installed = '' }
}
Write-Host ''

# --- manifests -------------------------------------------------------------
# obr.exe and obi.exe carry an application manifest that MSBuild does not embed
# the way the deploy script does. Without it they behave differently under some
# Windows settings, and nothing reports the omission.

$needManifest = @('obr.exe', 'obi.exe') | Where-Object { Test-Path (Join-Path $targetBin $_) }
if ($needManifest) {
    $mt = Find-Mt
    if (-not $mt) {
        Write-Host 'mt.exe not found -- manifests NOT embedded.' -ForegroundColor Yellow
        Write-Host 'Install the Windows SDK, or embed them by hand:'
        foreach ($name in $needManifest) {
            Write-Host ("  mt.exe -manifest core\vm\vs\manifest.xml -outputresource:{0};1" -f (Join-Path $targetBin $name))
        }
    } else {
        $manifest = Join-Path $repo 'core\vm\vs\manifest.xml'
        Write-Host 'Embedding manifests:'
        foreach ($name in $needManifest) {
            $exe = Join-Path $targetBin $name
            Write-Host ("  {0}" -f $name)
            if (-not $WhatIf) {
                & $mt -manifest $manifest ("-outputresource:" + $exe + ";1") | Out-Null
                if ($LASTEXITCODE -ne 0) {
                    Write-Host ("  mt.exe failed for {0} (exit {1})" -f $name, $LASTEXITCODE) -ForegroundColor Red
                    exit 1
                }
            }
        }
    }
    Write-Host ''
}

# --- libraries -------------------------------------------------------------
# The .obl set is tracked in core/lib and deploy_windows.cmd only COPIES it, so
# a deploy tree can hold libraries from weeks ago while its binaries are current.
# That is not cosmetic: a stale set cost another session a full suite run reading
# 304/4/10, where eight of the failures were ml_* sources failing to compile
# against .obl files that predated the methods they call. Nothing in that output
# points at the libraries.
#
# Rebuilding binaries without refreshing these is the same class of lie this
# script exists to prevent, so they are refreshed and recorded alongside.

$libRecords = @()
$libSource = Join-Path $repo 'core\lib'
$libTarget = Join-Path $target 'lib'
if (Test-Path $libSource) {
    if (-not (Test-Path $libTarget) -and -not $WhatIf) {
        New-Item -ItemType Directory -Path $libTarget | Out-Null
    }
    $obls = Get-ChildItem -Path $libSource -Filter *.obl -ErrorAction SilentlyContinue
    $changed = 0
    foreach ($obl in $obls) {
        $destination = Join-Path $libTarget $obl.Name
        $source = (Get-FileHash $obl.FullName -Algorithm MD5).Hash.Substring(0, 12)
        $current = ''
        if (Test-Path $destination) {
            $current = (Get-FileHash $destination -Algorithm MD5).Hash.Substring(0, 12)
        }
        if ($current -ne $source) {
            $changed++
            if ($WhatIf) {
                Write-Host ("  {0,-18} would refresh  {1} -> {2}" -f $obl.Name, $(if ($current) { $current } else { '(absent)' }), $source)
            } else {
                Copy-Item $obl.FullName $destination -Force
                Write-Host ("  {0,-18} refreshed      {1} -> {2}" -f $obl.Name, $(if ($current) { $current } else { '(absent)' }), $source)
            }
        }
        $libRecords += [pscustomobject]@{ Name = $obl.Name; Source = $source }
    }
    if ($changed -eq 0) {
        Write-Host ("Libraries: {0} .obl already current." -f $obls.Count)
    } else {
        Write-Host ("Libraries: {0} of {1} .obl refreshed from core/lib." -f $changed, $obls.Count)
    }
} else {
    Write-Host 'core/lib not found -- libraries NOT refreshed.' -ForegroundColor Yellow
}
Write-Host ''


# --- native libraries -------------------------------------------------------
# -Native BUILT these; nothing installed them, so the deploy tree kept whatever
# DLLs it already had. After the 2026.9.7 bump that left a libobjk_diags.dll
# from the previous day carrying the old VER_NUM, which rejected the current
# .obl set -- and only the two tests that compile Objeck at RUN time through
# System.Diagnostics could see it, so 314 of 316 passed and the failure read as
# two flaky tests rather than a stale artifact (#1008).
#
# Output paths differ per project -- Release\win64, vs\Release\x64,
# ARM64\Release -- so each artifact is located under its own project directory
# rather than from a hardcoded list that would drift as projects move.

$nativeRecords = @()
$nativeTarget = Join-Path $target 'lib\native'
$archPattern = if ($Arch -eq 'arm64') { 'ARM64' } else { 'win64|x64' }

$builtNatives = @{}
foreach ($project in $nativeProjects) {
    $projectDir = Join-Path $repo (Split-Path $project.Path -Parent)
    if (-not (Test-Path $projectDir)) { continue }
    Get-ChildItem -Path $projectDir -Recurse -Filter 'libobjk_*.dll' -ErrorAction SilentlyContinue |
        Where-Object { $_.FullName -match $archPattern } |
        ForEach-Object {
            # A project can leave several builds behind; keep the newest.
            if (-not $builtNatives.ContainsKey($_.Name) -or
                $_.LastWriteTime -gt $builtNatives[$_.Name].LastWriteTime) {
                $builtNatives[$_.Name] = $_
            }
        }
}

if ($builtNatives.Count -gt 0) {
    if (-not (Test-Path $nativeTarget)) {
        New-Item -ItemType Directory -Path $nativeTarget | Out-Null
    }
    Write-Host 'Native libraries:'
    $nativeChanged = 0
    foreach ($name in ($builtNatives.Keys | Sort-Object)) {
        $dll = $builtNatives[$name]
        $destination = Join-Path $nativeTarget $name
        $source = (Get-FileHash $dll.FullName -Algorithm MD5).Hash.Substring(0, 12)
        $current = ''
        if (Test-Path $destination) {
            $current = (Get-FileHash $destination -Algorithm MD5).Hash.Substring(0, 12)
        }
        if ($current -ne $source) {
            $nativeChanged++
            if ($WhatIf) {
                Write-Host ("  {0,-24} would install  {1} -> {2}" -f $name, $(if ($current) { $current } else { '(absent)' }), $source)
            } else {
                Copy-Item $dll.FullName $destination -Force
                Write-Host ("  {0,-24} installed      {1} -> {2}" -f $name, $(if ($current) { $current } else { '(absent)' }), $source)
            }
        }
        $nativeRecords += [pscustomobject]@{ Name = $name; Source = $source }
    }
    if ($nativeChanged -eq 0) {
        Write-Host ("  {0} native libraries already current." -f $builtNatives.Count)
    }
    Write-Host ''
}

# Silence is what made this expensive: without -Native the script says nothing
# about libraries it did not build, and a version bump can leave them behind.
if (-not $Native) {
    $versionHeader = Join-Path $repo 'core\shared\version.h'
    if ((Test-Path $versionHeader) -and (Test-Path $nativeTarget)) {
        # The COMMIT time of version.h, not its file mtime. An mtime records when
        # the file was last written on THIS machine, and a clone, a branch switch
        # and a 'cp' restore all reset it to now -- every deployed native then
        # reads as stale. That fired on all eight during #1010's testing, and a
        # warning that cries wolf is the one people stop reading, which is the
        # failure this one exists to prevent.
        #
        # The exception is a version bump in progress: version.h is edited but not
        # yet committed, so its commit time still names the PREVIOUS version and
        # natives built before the bump would stop flagging -- precisely the case
        # this warning is for. While it is dirty the mtime is the real signal.
        $versionTime = $null
        $reference = 'commit time'
        $dirty = $false
        try { $dirty = [bool](& git -C $repo status --porcelain -- $versionHeader 2>$null) } catch { $dirty = $false }

        if (-not $dirty) {
            try {
                $iso = (& git -C $repo log -1 --format=%cI -- $versionHeader 2>$null)
                if ($iso) { $versionTime = [datetimeoffset]::Parse($iso).LocalDateTime }
            }
            catch { $versionTime = $null }
        }
        if (-not $versionTime) {
            # dirty, no git, or version.h never committed: the mtime is all there is
            $versionTime = (Get-Item $versionHeader).LastWriteTime
            $reference = $(if ($dirty) { 'file mtime, version.h is uncommitted' } else { 'file mtime, no commit found' })
        }

        $stale = Get-ChildItem -Path $nativeTarget -Filter 'libobjk_*.dll' -ErrorAction SilentlyContinue |
                 Where-Object { $_.LastWriteTime -lt $versionTime }
        if ($stale) {
            Write-Host ("WARNING: {0} deployed native librar{1} older than core/shared/version.h ({2:yyyy-MM-dd HH:mm}, {3})." -f $stale.Count, $(if ($stale.Count -eq 1) { 'y is' } else { 'ies are' }), $versionTime, $reference) -ForegroundColor Yellow
            foreach ($s in ($stale | Sort-Object Name)) {
                Write-Host ("         {0,-24} {1:yyyy-MM-dd HH:mm}" -f $s.Name, $s.LastWriteTime) -ForegroundColor Yellow
            }
            Write-Host '         A native library built before a version bump carries the old' -ForegroundColor Yellow
            Write-Host '         VER_NUM and will reject the current .obl set. Re-run with -Native.' -ForegroundColor Yellow
            Write-Host ''
        }
    }
}
# --- record ----------------------------------------------------------------

if (-not $WhatIf) {
    # Hash the installed files NOW, after any manifest embedding, so `installed`
    # is what is actually in bin. `source` is what it was built from, and is
    # what the next run compares against.
    foreach ($record in $records) {
        $installedPath = Join-Path $targetBin $record.Name
        if (Test-Path $installedPath) {
            $record.Installed = (Get-FileHash $installedPath -Algorithm MD5).Hash.Substring(0, 12)
        }
    }

    $lines = @()
    $lines += ("# refreshed {0} from commit {1} ({2})" -f (Get-Date -Format 'yyyy-MM-ddTHH:mm:ss'), $commit, $Arch)
    $lines += '# md5 (first 12). source = the build output; installed = the file in bin'
    $lines += '# after mt.exe embedded its manifest. They differ for binaries whose'
    $lines += '# project does not embed one at build time, which is expected.'
    foreach ($record in ($records | Sort-Object Name)) {
        $lines += ("{0,-12} source={1} installed={2}" -f $record.Name, $record.Source, $record.Installed)
    }
    if ($libRecords.Count -gt 0) {
        $lines += ''
        $lines += '# libraries copied from core/lib (tracked; no manifest embedding)'
        foreach ($record in ($libRecords | Sort-Object Name)) {
            $lines += ("{0,-18} source={1}" -f $record.Name, $record.Source)
        }
    }
    if ($nativeRecords.Count -gt 0) {
        $lines += ''
        $lines += '# native libraries built from source (lib/native). A manifest that'
        $lines += '# omitted these could not answer the question it exists for: they are'
        $lines += '# the artifact class most likely to go stale, because they are built'
        $lines += '# separately and only with -Native.'
        foreach ($record in ($nativeRecords | Sort-Object Name)) {
            $lines += ("{0,-24} source={1}" -f $record.Name, $record.Source)
        }
    }
    Set-Content -Path $manifestPath -Value $lines -Encoding utf8
    Write-Host ("Recorded {0} binaries, {1} libraries and {2} native libraries in {3}" -f $records.Count, $libRecords.Count, $nativeRecords.Count, 'DEPLOY_MANIFEST.txt')
    Write-Host 'Compare against it when a test result looks impossible: a deploy binary'
    Write-Host 'replaced by another build reporting the same version string is invisible'
    Write-Host 'otherwise, and invalidates every measurement taken against it.'
}

Write-Host ''
Write-Host 'Done.'
