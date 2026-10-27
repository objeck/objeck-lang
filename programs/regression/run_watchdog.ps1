# The per-test time limit of run_regression.cmd. One instance runs beside the whole
# suite; the POSIX runner uses timeout(1) per test, and cmd.exe has nothing of the kind.
#
#   powershell -NoProfile -ExecutionPolicy Bypass -File run_watchdog.ps1 SECONDS VM_EXE KILLED_FILE RUNNING_FILE
#
# Every two seconds it looks for a run of VM_EXE that the runner (this process's parent,
# the cmd.exe that interprets run_regression.cmd) started more than SECONDS ago, writes
# the test's name (the .obe on its command line) to KILLED_FILE, and kills its process
# tree. The runner then reports that test as timed out. Only the runner's own children
# are considered, so another suite or a program a user runs is never touched.
#
# It exits when RUNNING_FILE is gone (the runner deletes it at the end), when the runner
# is gone, or after ten minutes without a test running (an interrupted runner).
#
# This replaced a wrapper that started PowerShell around every test: that cost about
# 0.35 s a test, over four minutes on a suite of 750 tests, so the limit was off by
# default and a hung test could stall a run for hours.
param(
    [Parameter(Mandatory = $true, Position = 0)][int]$Seconds,
    [Parameter(Mandatory = $true, Position = 1)][string]$Vm,
    [Parameter(Mandatory = $true, Position = 2)][string]$KilledFile,
    [Parameter(Mandatory = $true, Position = 3)][string]$RunningFile
)

$runner = (Get-CimInstance Win32_Process -Filter "ProcessId = $PID").ParentProcessId
$vmPath = [System.IO.Path]::GetFullPath($Vm).ToLowerInvariant()
$taskkill = Join-Path $env:SystemRoot 'System32\taskkill.exe'
$idleSince = Get-Date

while ($true) {
    Start-Sleep -Seconds 2
    if (-not (Test-Path -LiteralPath $RunningFile)) { exit 0 }
    if (-not (Get-Process -Id $runner -ErrorAction SilentlyContinue)) { exit 0 }

    $now = Get-Date
    $running = @(Get-Process -Name 'obr' -ErrorAction SilentlyContinue)
    if ($running.Count -gt 0) { $idleSince = $now }
    elseif (($now - $idleSince).TotalSeconds -gt 600) { exit 0 }

    foreach ($proc in $running) {
        $started = $null
        try { $started = $proc.StartTime } catch { continue }
        if (($now - $started).TotalSeconds -lt $Seconds) { continue }

        $info = Get-CimInstance Win32_Process -Filter "ProcessId = $($proc.Id)"
        if (-not $info -or $info.ParentProcessId -ne $runner) { continue }
        if (-not $info.ExecutablePath -or $info.ExecutablePath.ToLowerInvariant() -ne $vmPath) { continue }

        $name = ''
        if ($info.CommandLine -match '"?([^"\\/\s]+)\.obe"?') { $name = $Matches[1] }
        # the name goes down before the kill, so the runner finds it when the run returns
        Add-Content -LiteralPath $KilledFile -Value $name
        & $taskkill /T /F /PID $proc.Id *> $null
    }
}
