# Signs a freshly published release's Windows installers with no one at the keyboard.
#
#   auto_sign.ps1                    look at the latest release once; sign it if it is ready and unsigned
#   auto_sign.ps1 -Version 2026.11.2 same, for one specific release
#   auto_sign.ps1 -DryRun            say what it would do; change nothing
#   auto_sign.ps1 -Install           register a Windows scheduled task that runs it every 30 minutes
#   auto_sign.ps1 -Uninstall         remove that task
#
# Needs, on the release machine: the SafeNet token plugged in, GitHub CLI signed in, and the protected
# password file from  tools\cicd\token_pin.ps1 -Set .  With the token unplugged it does nothing (not an error).
#
# It signs ONLY when all of these hold, and says which one stopped it otherwise:
#   * the release is published (not a draft or prerelease) and has both Windows installers
#   * it is either complete (the Linux .deb packages, built after publish, are there) or at least
#     -MinAgeMinutes old, because signing regenerates SHA256SUMS from whatever is on the release
#   * the tag is on master (GitHub's compare says master is ahead of or equal to the tag)
#   * the installers match the CI-built SHA256SUMS (checked inside sign_with_saved_pin.ps1)
#   * no earlier attempt for this release failed (see below), and it has not already been signed
#
# Failure policy, on purpose: it tries a release at most 3 times and NEVER retries after a signing failure
# (signtool non-zero). A wrong or locked token must be looked at by a person; SafeNet tokens lock after a few
# wrong passwords. To retry after fixing the cause, delete  %USERPROFILE%\.objeck\signing_failed.txt  and/or
# auto_sign_attempts_<version>.txt .
#
# Everything is appended to %USERPROFILE%\.objeck\auto_sign.log (never the password).
param(
    [string]$Repo = 'objeck/objeck-lang',
    [string]$Version = '',
    [int]$MinAgeMinutes = 45,
    [string]$Thumbprint = '6DEAD3A9C58C82DD3CE979DBDFAF64CC964C0A46',
    [switch]$DryRun,
    [switch]$Install,
    [switch]$Uninstall
)

$ErrorActionPreference = 'Stop'
$taskName = 'Objeck auto-sign'
$state = Join-Path $env:USERPROFILE '.objeck'
$logFile = Join-Path $state 'auto_sign.log'
if (-not (Test-Path -LiteralPath $state)) { New-Item -ItemType Directory -Path $state | Out-Null }

function Log([string]$m) {
    $line = "$(Get-Date -Format s)  $m"
    Write-Host $line
    Add-Content -LiteralPath $logFile -Value $line
}

if ($Install) {
    $script = $MyInvocation.MyCommand.Path
    $cmd = "powershell.exe -NoProfile -ExecutionPolicy Bypass -File `"$script`""
    & schtasks.exe /Create /TN $taskName /SC MINUTE /MO 30 /TR $cmd /F | Out-Null
    if ($LASTEXITCODE -ne 0) { throw 'schtasks could not create the task' }
    Write-Host "Registered '$taskName': every 30 minutes, as $($env:USERNAME), only while you are logged on."
    Write-Host "It runs this file in place: $script"
    Write-Host 'Run -Install from a checkout that stays put (the main objeck-lang folder on master), not a temporary worktree.'
    return
}
if ($Uninstall) {
    & schtasks.exe /Delete /TN $taskName /F | Out-Null
    Write-Host "Removed '$taskName' (if it existed)."
    return
}

# one run at a time
$mutex = New-Object System.Threading.Mutex($false, 'Global\ObjeckAutoSign')
if (-not $mutex.WaitOne(0)) { Log 'another auto_sign run is in progress; leaving it alone'; exit 0 }
try {
    $gh = 'C:\Program Files\GitHub CLI\gh.exe'
    if (-not (Test-Path -LiteralPath $gh)) { $gh = 'gh' }

    # ---- which release
    if (-not $Version) {
        # (parentheses + ForEach-Object: Windows PowerShell 5.1 hands a JSON array on as ONE object otherwise)
        $list = (& $gh release list --repo $Repo --limit 5 --json tagName,isDraft,isPrerelease | ConvertFrom-Json) | ForEach-Object { $_ }
        $rel = $list | Where-Object { -not $_.isDraft -and -not $_.isPrerelease } | Select-Object -First 1
        if (-not $rel) { Log 'no published release found'; exit 0 }
        $Version = $rel.tagName.TrimStart('v')
    }
    $tag = "v$Version"

    if (Test-Path -LiteralPath (Join-Path $state "signed_$Version.txt")) { exit 0 }     # quiet: nothing to do
    if (Test-Path -LiteralPath (Join-Path $state 'signing_failed.txt')) { Log "refusing ${tag}: an earlier signing failure is unresolved (signing_failed.txt)"; exit 2 }
    $attemptsFile = Join-Path $state "auto_sign_attempts_$Version.txt"
    $attempts = 0
    if (Test-Path -LiteralPath $attemptsFile) { $attempts = [int](Get-Content -LiteralPath $attemptsFile -Raw) }
    if ($attempts -ge 3) { Log "refusing ${tag}: already tried $attempts times (delete $attemptsFile to retry)"; exit 2 }

    # ---- is it ready
    $prevEap = $ErrorActionPreference; $ErrorActionPreference = 'Continue'      # a native command's stderr must not throw
    $json = & $gh release view $tag --repo $Repo --json assets,publishedAt,isDraft,isPrerelease 2>&1
    $ghExit = $LASTEXITCODE
    $ErrorActionPreference = $prevEap
    if ($ghExit -ne 0) { Log ("could not read release ${tag}: " + (($json | ForEach-Object { $_.ToString() }) -join ' ')); exit 0 }
    $info = $json | ConvertFrom-Json
    if ($info.isDraft -or $info.isPrerelease) { Log "$tag is a draft or prerelease; not signing"; exit 0 }
    $names = @($info.assets | ForEach-Object { $_.name })
    $haveX64 = @($names | Where-Object { $_ -like 'objeck-windows-x64_*.msi' }).Count -gt 0
    $haveArm = @($names | Where-Object { $_ -like 'objeck-windows-arm64_*.msi' }).Count -gt 0
    if (-not ($haveX64 -and $haveArm)) { Log "$tag does not have both Windows installers yet"; exit 0 }
    $haveDeb = @($names | Where-Object { $_ -like '*.deb' }).Count -gt 0
    $ageMin = [int]((Get-Date).ToUniversalTime() - [datetime]$info.publishedAt).TotalMinutes
    if (-not $haveDeb -and $ageMin -lt $MinAgeMinutes) { Log "$tag is $ageMin min old and the .deb packages are not up yet; waiting (min age $MinAgeMinutes)"; exit 0 }

    # ---- is the tag on master
    $cmp = (& $gh api "repos/$Repo/compare/master...$tag" --jq '.status') 2>$null
    if ($cmp -ne 'behind' -and $cmp -ne 'identical') { Log "REFUSING ${tag}: GitHub says the tag is '$cmp' relative to master, not an ancestor of it"; exit 3 }

    # ---- is the machine ready
    if (-not (Get-Item -LiteralPath "Cert:\CurrentUser\My\$Thumbprint" -ErrorAction SilentlyContinue)) { Log "$tag is ready but the signing certificate is not available (token unplugged?); will try again later"; exit 0 }
    if (-not (Test-Path -LiteralPath (Join-Path $state 'token_pin.dpapi'))) { Log "$tag is ready but there is no saved password file; run tools\cicd\token_pin.ps1 -Set"; exit 0 }

    if ($DryRun) { Log "DRY RUN: would sign $tag now (checks above all passed); nothing changed"; exit 0 }

    # ---- sign
    Set-Content -LiteralPath $attemptsFile -Value ($attempts + 1)
    Log "signing $tag (attempt $($attempts + 1) of 3)"
    $work = Join-Path $state 'work'
    if (-not (Test-Path -LiteralPath $work)) { New-Item -ItemType Directory -Path $work | Out-Null }
    Push-Location $work
    try {
        $out = & cmd.exe /c "`"$(Join-Path $PSScriptRoot 'sign_release.cmd')`" $Version" 2>&1
        $exit = $LASTEXITCODE
    } finally { Pop-Location }
    ($out | ForEach-Object { "$_" }) | Select-Object -Last 25 | ForEach-Object { Add-Content -LiteralPath $logFile -Value "    $_" }

    if ($exit -eq 0) {
        Set-Content -LiteralPath (Join-Path $state "signed_$Version.txt") -Value (Get-Date -Format s)
        Remove-Item -LiteralPath $attemptsFile -Force -ErrorAction SilentlyContinue
        Log "SIGNED $tag. Next: confirm with tools\cicd\check_release_signatures.ps1 and the manifest re-sign workflow."
        exit 0
    }
    Log "FAILED signing $tag (exit $exit); see the lines above. It will not retry a signing failure on its own."
    exit 1
} finally {
    $mutex.ReleaseMutex()
}
