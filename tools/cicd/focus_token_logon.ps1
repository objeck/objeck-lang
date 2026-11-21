# Brings the SafeNet "Token Logon" PIN window to the front while sign_release.cmd runs.
#
# Why this exists: on v2026.11.1 the release signer reported "enter your password when prompted"
# but the prompt was a signtool window titled "Token Logon" that opened behind everything, or on
# another desktop, and the maintainer reported "saw no signing dialog" until it was found and raised
# by hand. This does that automatically.
#
# It ONLY raises the window. It never types, reads, stores or forwards the PIN: the person at the
# keyboard enters it, exactly as before.
#
# usage: powershell -NoProfile -ExecutionPolicy Bypass -File focus_token_logon.ps1 [-TimeoutSec 900]
# Exits when signtool has been seen and is gone again, or after the timeout. Prints nothing secret.
param([int]$TimeoutSec = 900)

Add-Type -Namespace Win -Name Native -MemberDefinition @'
[System.Runtime.InteropServices.DllImport("user32.dll")] public static extern bool ShowWindow(System.IntPtr h, int n);
[System.Runtime.InteropServices.DllImport("user32.dll")] public static extern bool SetForegroundWindow(System.IntPtr h);
[System.Runtime.InteropServices.DllImport("user32.dll")] public static extern bool BringWindowToTop(System.IntPtr h);
'@

$deadline = (Get-Date).AddSeconds($TimeoutSec)
$seen = $false
$raised = @{}
while ((Get-Date) -lt $deadline) {
    $procs = @(Get-Process -Name signtool -ErrorAction SilentlyContinue)
    if ($procs.Count -gt 0) {
        $seen = $true
        foreach ($p in $procs) {
            $p.Refresh()
            $h = $p.MainWindowHandle
            # The PIN prompt is the window titled "Token Logon"; any other signtool window is left alone.
            if ($h -ne [System.IntPtr]::Zero -and $p.MainWindowTitle -like '*Token Logon*') {
                $key = "$($p.Id):$h"
                if (-not $raised.ContainsKey($key)) {
                    [void][Win.Native]::ShowWindow($h, 9)      # SW_RESTORE
                    [void][Win.Native]::BringWindowToTop($h)
                    [void][Win.Native]::SetForegroundWindow($h)
                    [Console]::Beep(880, 200)
                    Write-Host "SafeNet 'Token Logon' window raised: enter the token password there."
                    $raised[$key] = $true
                }
            }
        }
    }
    elseif ($seen) {
        break      # signtool ran and has finished
    }
    Start-Sleep -Milliseconds 500
}
