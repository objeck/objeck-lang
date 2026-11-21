# Stores the SafeNet token password for UNATTENDED signing, protected by Windows (DPAPI).
#
#   token_pin.ps1 -Set       prompt once (hidden), save it encrypted to %USERPROFILE%\.objeck\token_pin.dpapi
#   token_pin.ps1 -Status    say whether the file exists and when it was written (never the password)
#   token_pin.ps1 -Remove    delete it; signing goes back to the interactive "Token Logon" prompt
#
# Why a protected file and not a plain text one:
#   The file is encrypted with the Windows account that created it, on this machine (ConvertFrom-SecureString
#   without a key = DPAPI). A copy taken from the disk, a backup or a repository is unreadable anywhere else.
#   A plain text password would sit in every backup and sync client (Dropbox, OneDrive) forever.
#
# What it does NOT protect against: any process running as YOU on THIS machine can decrypt it and sign
# with your certificate while the token is plugged in. That is the trade of unattended signing. Keep the
# token unplugged when you are not releasing if that matters to you.
#
# Nothing here prints, logs or passes the password anywhere. The person at the keyboard types it; no script
# or assistant ever sees it.
param(
    [switch]$Set,
    [string]$Import = '',
    [switch]$DeleteSource,
    [switch]$Remove,
    [switch]$Status,
    [string]$File = (Join-Path $env:USERPROFILE '.objeck\token_pin.dpapi')
)

$ErrorActionPreference = 'Stop'

# -Import <plain text file> [-DeleteSource]: for a password you already put in a text file. The file is read once
# here (first non-empty line, nothing else), stored encrypted exactly like -Set, and with -DeleteSource the plain
# text is overwritten and the file deleted. A plain text password in a SYNCED folder (Dropbox, OneDrive) is copied
# to the cloud and kept in version history, so import it and delete the source straight away; deleting it does not
# remove old synced copies. If it sat in a synced folder, changing the token password afterwards is the safe move.
if ($Import) {
    if (-not (Test-Path -LiteralPath $Import)) { throw "file not found: $Import" }
    $first = (Get-Content -LiteralPath $Import | Where-Object { $_.Trim() } | Select-Object -First 1)
    if (-not $first) { throw 'the file has no password in it: nothing saved' }
    $dir = Split-Path -Parent $File
    if (-not (Test-Path -LiteralPath $dir)) { New-Item -ItemType Directory -Path $dir | Out-Null }
    (ConvertTo-SecureString $first.Trim() -AsPlainText -Force) | ConvertFrom-SecureString | Set-Content -LiteralPath $File -Encoding ASCII
    $first = $null
    & icacls.exe $File /inheritance:r /grant:r "$($env:USERNAME):(R,W)" | Out-Null
    Write-Host "Imported (encrypted for $($env:USERNAME) on $($env:COMPUTERNAME)): $File"
    if ($DeleteSource) {
        $len = (Get-Item -LiteralPath $Import).Length
        [IO.File]::WriteAllBytes((Resolve-Path -LiteralPath $Import).Path, (New-Object byte[] $len))   # overwrite with zeros
        Remove-Item -LiteralPath $Import -Force
        Write-Host "Overwrote and deleted the plain text file: $Import"
        Write-Host 'If that folder syncs to the cloud, older synced copies may still exist: consider changing the token password.'
    } else {
        Write-Host "The plain text file is STILL THERE: $Import  (re-run with -DeleteSource, or delete it yourself)"
    }
    return
}

if ($Set) {
    $dir = Split-Path -Parent $File
    if (-not (Test-Path -LiteralPath $dir)) { New-Item -ItemType Directory -Path $dir | Out-Null }
    $secure = Read-Host -AsSecureString 'SafeNet token password (hidden; not shown, not logged)'
    if ($secure.Length -eq 0) { throw 'empty password: nothing saved' }
    $secure | ConvertFrom-SecureString | Set-Content -LiteralPath $File -Encoding ASCII
    # Only this account may read or write it.
    & icacls.exe $File /inheritance:r /grant:r "$($env:USERNAME):(R,W)" | Out-Null
    Write-Host "Saved (encrypted for $($env:USERNAME) on $($env:COMPUTERNAME)): $File"
    Write-Host 'Check it before a release:  powershell -File tools\cicd\sign_with_saved_pin.ps1 -Dir <folder> -WhatIf'
    return
}

if ($Remove) {
    if (Test-Path -LiteralPath $File) { Remove-Item -LiteralPath $File -Force; Write-Host "Removed $File" }
    else { Write-Host 'Nothing to remove.' }
    return
}

# default: status
if (Test-Path -LiteralPath $File) {
    $item = Get-Item -LiteralPath $File
    Write-Host "Saved password file present: $File (written $($item.LastWriteTime))"
    exit 0
}
Write-Host "No saved password file at $File. Create it with:  token_pin.ps1 -Set"
exit 1
