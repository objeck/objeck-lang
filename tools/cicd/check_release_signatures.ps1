<#
.SYNOPSIS
  Checks whether a published release's Windows installers are signed, and makes it
  impossible to miss when they are not.

.DESCRIPTION
  Signing cannot happen in CI: the key is on a physical SafeNet eToken, so it is a
  local step (tools\cicd\sign_release.cmd) run after the release publishes. That is
  easy to forget -- every Windows installer from v2026.4.0 through v2026.8.2 shipped
  unsigned because nobody noticed for four months, and the release notes claimed
  otherwise.

  This downloads the published MSIs, reports the REAL signature status, and on any
  unsigned installer beeps, raises a desktop notification, optionally pushes to ntfy,
  and prints the exact command to fix it. Exits 1 so it can gate a script.

.PARAMETER Version
  Release version without the leading v, e.g. 2026.8.2

.PARAMETER Quiet
  Report only; no beep, notification or push.

.EXAMPLE
  .\check_release_signatures.ps1 2026.8.2

.NOTES
  ntfy is optional. Set NTFY_TOPIC (and optionally NTFY_SERVER, default https://ntfy.sh)
  to receive a push; unset, that step is skipped silently.
#>
[CmdletBinding()]
param(
  [Parameter(Mandatory = $true, Position = 0)]
  [string]$Version,
  [switch]$Quiet
)

$ErrorActionPreference = 'Stop'

function Send-Alert {
  param([string]$Title, [string]$Message)

  if ($Quiet) { return }

  # 1. Audible -- three rising tones, hard to miss while doing something else.
  try {
    [console]::beep(880, 200); Start-Sleep -Milliseconds 80
    [console]::beep(1046, 200); Start-Sleep -Milliseconds 80
    [console]::beep(1318, 350)
  } catch { }

  # 2. Desktop notification via the shell's balloon tip. Uses only built-in types,
  #    so it needs no module install (BurntToast et al.).
  try {
    Add-Type -AssemblyName System.Windows.Forms
    $icon = New-Object System.Windows.Forms.NotifyIcon
    $icon.Icon = [System.Drawing.SystemIcons]::Warning
    $icon.BalloonTipTitle = $Title
    $icon.BalloonTipText = $Message
    $icon.Visible = $true
    $icon.ShowBalloonTip(20000)
    Start-Sleep -Seconds 6
    $icon.Dispose()
  } catch { }

  # 3. Push, only when a topic is configured.
  $topic = $env:NTFY_TOPIC
  if ($topic) {
    $server = if ($env:NTFY_SERVER) { $env:NTFY_SERVER.TrimEnd('/') } else { 'https://ntfy.sh' }
    try {
      Invoke-RestMethod -Uri "$server/$topic" -Method Post -Body $Message -Headers @{
        Title    = $Title
        Priority = 'high'
        Tags     = 'warning,lock'
      } | Out-Null
      Write-Host "  ntfy: pushed to $server/$topic"
    } catch {
      Write-Host "  ntfy: push failed - $($_.Exception.Message)"
    }
  }
}

$staging = Join-Path $env:TEMP "objeck-sigcheck-$Version"
if (Test-Path $staging) { Remove-Item $staging -Recurse -Force }
New-Item -ItemType Directory -Path $staging | Out-Null

Write-Host "Checking signatures for v$Version ..."
& gh release download "v$Version" --pattern "*.msi" --dir $staging 2>&1 | Out-Null
if ($LASTEXITCODE -ne 0) {
  Write-Host "ERROR: could not download MSIs for v$Version (does the release exist?)"
  exit 2
}

$msis = Get-ChildItem "$staging\*.msi" -ErrorAction SilentlyContinue
if (-not $msis) {
  Write-Host "ERROR: no MSI assets found on release v$Version"
  exit 2
}

$unsigned = @()
foreach ($m in $msis) {
  $sig = Get-AuthenticodeSignature $m.FullName
  $signer = if ($sig.SignerCertificate) { $sig.SignerCertificate.Subject.Split(',')[0] } else { '(none)' }
  $stamped = if ($sig.TimeStamperCertificate) { 'timestamped' } else { 'NOT timestamped' }

  if ($sig.Status -eq 'Valid') {
    Write-Host "  OK       $($m.Name)  -> $signer, $stamped"
    # An untimestamped signature dies with the certificate, so flag it even when valid.
    if (-not $sig.TimeStamperCertificate) {
      Write-Host "           WARNING: no timestamp - this signature expires with the certificate"
    }
  }
  else {
    Write-Host "  UNSIGNED $($m.Name)  -> status=$($sig.Status)"
    $unsigned += $m.Name
  }
}

Remove-Item $staging -Recurse -Force -ErrorAction SilentlyContinue

if ($unsigned.Count -eq 0) {
  Write-Host ""
  Write-Host "All Windows installers for v$Version are signed."
  exit 0
}

$cmd = "tools\cicd\sign_release.cmd $Version"
Write-Host ""
Write-Host "============================================================"
Write-Host " $($unsigned.Count) UNSIGNED installer(s) on release v$Version"
Write-Host "============================================================"
$unsigned | ForEach-Object { Write-Host "   - $_" }
Write-Host ""
Write-Host " Plug in the SafeNet eToken and run:"
Write-Host "     $cmd"
Write-Host ""
Write-Host " Signing rewrites the MSIs, so regenerate SHA256SUMS afterwards."
Write-Host "============================================================"

Send-Alert -Title "Objeck v$Version needs signing" `
           -Message "$($unsigned.Count) unsigned Windows installer(s). Plug in the eToken and run $cmd"

exit 1
