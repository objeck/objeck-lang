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
    Also build the eleven native library solutions. Slow, and they change
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

Write-Host 'Installing:'
$records = @()
foreach ($file in $staged) {
    $destination = Join-Path $targetBin $file.Name
    $before = ''
    if (Test-Path $destination) {
        $before = (Get-FileHash $destination -Algorithm MD5).Hash.Substring(0, 12)
    }
    $after = (Get-FileHash $file.FullName -Algorithm MD5).Hash.Substring(0, 12)

    if ($before -eq $after) {
        Write-Host ("  {0,-12} unchanged  {1}" -f $file.Name, $after)
    } elseif ($WhatIf) {
        Write-Host ("  {0,-12} would go   {1} -> {2}" -f $file.Name, $(if ($before) { $before } else { '(absent)' }), $after)
    } else {
        Copy-Item $file.FullName $destination -Force
        Write-Host ("  {0,-12} replaced   {1} -> {2}" -f $file.Name, $(if ($before) { $before } else { '(absent)' }), $after)
    }
    $records += [pscustomobject]@{ Name = $file.Name; Hash = $after }
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

# --- record ----------------------------------------------------------------

if (-not $WhatIf) {
    $manifestPath = Join-Path $target 'DEPLOY_MANIFEST.txt'
    $lines = @()
    $lines += ("# refreshed {0} from commit {1} ({2})" -f (Get-Date -Format 'yyyy-MM-ddTHH:mm:ss'), $commit, $Arch)
    $lines += '# md5 (first 12) of each binary installed by refresh_deploy.ps1'
    foreach ($record in ($records | Sort-Object Name)) {
        $lines += ("{0,-14} {1}" -f $record.Name, $record.Hash)
    }
    Set-Content -Path $manifestPath -Value $lines -Encoding utf8
    Write-Host ("Recorded {0} binaries in {1}" -f $records.Count, 'DEPLOY_MANIFEST.txt')
    Write-Host 'Compare against it when a test result looks impossible: a deploy binary'
    Write-Host 'replaced by another build reporting the same version string is invisible'
    Write-Host 'otherwise, and invalidates every measurement taken against it.'
}

Write-Host ''
Write-Host 'Done.'
