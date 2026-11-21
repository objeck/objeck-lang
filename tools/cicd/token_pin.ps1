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
    [switch]$Remove,
    [switch]$Status,
    [string]$File = (Join-Path $env:USERPROFILE '.objeck\token_pin.dpapi')
)

$ErrorActionPreference = 'Stop'

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
