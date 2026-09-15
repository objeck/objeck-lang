# Runs a command with stdout and stderr merged into a file, killing its whole
# process tree after a time limit. run_regression.cmd uses it for TEST_TIMEOUT
# (the POSIX runner uses timeout(1)); cmd.exe has no equivalent of its own.
#
#   powershell -NoProfile -ExecutionPolicy Bypass -File run_with_timeout.ps1 SECONDS OUTPUT_FILE COMMAND [ARGS...]
#
# Exit status: the command's own, or 124 when it was killed at the limit
# (the same code timeout(1) uses).
param(
    [Parameter(Mandatory = $true, Position = 0)][int]$Seconds,
    [Parameter(Mandatory = $true, Position = 1)][string]$Output,
    [Parameter(ValueFromRemainingArguments = $true)][string[]]$Command
)

if (-not $Command -or $Command.Count -eq 0) {
    [Console]::Error.WriteLine("run_with_timeout.ps1: no command")
    exit 2
}

function Quote([string]$s) {
    if ($s -eq '' -or $s -match '[\s"&|<>^()]') { return '"' + ($s -replace '"', '\"') + '"' }
    return $s
}

$line = ($Command | ForEach-Object { Quote $_ }) -join ' '
# cmd /s /c "..." strips exactly the outer quotes, leaving the inner ones intact.
$psi = New-Object System.Diagnostics.ProcessStartInfo
$psi.FileName = Join-Path $env:SystemRoot 'System32\cmd.exe'
$psi.Arguments = '/d /s /c "' + $line + ' > "' + $Output + '" 2>&1"'
$psi.UseShellExecute = $false
$proc = [System.Diagnostics.Process]::Start($psi)
$null = $proc.Handle   # keeps the exit code readable after exit

if (-not $proc.WaitForExit($Seconds * 1000)) {
    & (Join-Path $env:SystemRoot 'System32\taskkill.exe') /T /F /PID $proc.Id *> $null
    $null = $proc.WaitForExit(10000)
    Add-Content -Path $Output -Value ("[run_with_timeout] killed after {0}s" -f $Seconds)
    exit 124
}
exit $proc.ExitCode
