# Probe loop for the windows-arm64 core_thread_gc_stress flake. Runs the
# original test and a diagnostic copy many times under several VM
# configurations, counts failures and hangs, keeps every failing run's output,
# and writes a summary (also to the GitHub step summary when present).
param(
  [Parameter(Mandatory = $true)][string]$Bin,
  [int]$Runs = 40,
  [int]$TimeoutSec = 180
)

$ErrorActionPreference = 'Continue'
$bin = (Resolve-Path $Bin).Path
$obc = Join-Path $bin 'obc.exe'
$obr = Join-Path $bin 'obr.exe'
$here = $PSScriptRoot
$res = Join-Path $here 'results'
New-Item -ItemType Directory -Force $res | Out-Null
$env:OBJECK_LIB_PATH = (Resolve-Path (Join-Path $bin '..\lib')).Path

# the regression runner's default libraries (run_regression.cmd)
function Compile([string]$src, [string]$dest) {
  & $obc -src $src -lib cipher,collect,xml,json -opt s3 -dest $dest | Out-Host
  if (-not (Test-Path $dest)) { throw "compile failed: $src" }
}

$orig = Join-Path $res 'stress.obe'
$diag = Join-Path $res 'diag.obe'
Compile (Join-Path $here '..\core_thread_gc_stress.obs') $orig
Compile (Join-Path $here 'gc_stress_diag.obs') $diag

# name, program, extra obr arguments, environment, runs
$configs = @(
  @{ name = 'orig-default'; obe = $orig; args = @();                     envs = @{};                              runs = $Runs },
  @{ name = 'diag-default'; obe = $diag; args = @();                     envs = @{};                              runs = $Runs },
  @{ name = 'orig-jit1';    obe = $orig; args = @();                     envs = @{ OBJECK_JIT_THRESHOLD = '1' };  runs = $Runs },
  @{ name = 'orig-gc1m';    obe = $orig; args = @('--gc-threshold=1m');  envs = @{};                              runs = $Runs },
  @{ name = 'orig-jitoff';  obe = $orig; args = @('--jit=off');          envs = @{};                              runs = [math]::Max(1, [int]($Runs / 2)) }
)

$summary = New-Object System.Collections.Generic.List[string]
$summary.Add("| config | runs | pass | fail | hang | failure lines |")
$summary.Add("|---|---|---|---|---|---|")

foreach ($c in $configs) {
  $pass = 0; $fail = 0; $hang = 0
  $lines = New-Object System.Collections.Generic.List[string]
  foreach ($k in $c.envs.Keys) { Set-Item -Path "Env:$k" -Value $c.envs[$k] }
  for ($n = 1; $n -le $c.runs; $n++) {
    $out = Join-Path $res ("{0}_{1:D3}.out" -f $c.name, $n)
    $err = Join-Path $res ("{0}_{1:D3}.err" -f $c.name, $n)
    $argList = @($c.args) + @($c.obe)
    $sw = [Diagnostics.Stopwatch]::StartNew()
    $p = Start-Process -FilePath $obr -ArgumentList $argList -NoNewWindow -PassThru `
      -RedirectStandardOutput $out -RedirectStandardError $err
    # read the handle now: without it ExitCode can come back empty after the wait
    $null = $p.Handle
    if (-not $p.WaitForExit($TimeoutSec * 1000)) {
      try { $p.Kill() } catch {}
      $hang++
      $lines.Add("run $n hung after $TimeoutSec s")
      continue
    }
    $sw.Stop()
    $code = $p.ExitCode
    $text = (Get-Content $out -Raw -ErrorAction SilentlyContinue)
    if ($code -eq 0 -and $text -match 'PASS:') {
      $pass++
      Remove-Item $out, $err -ErrorAction SilentlyContinue
    }
    else {
      $fail++
      $first = ($text -split "`n" | Where-Object { $_ -match 'FAIL|round=' } | Select-Object -First 3) -join ' / '
      $lines.Add(("run {0}: exit 0x{1:X8}, {2:N1} s: {3}" -f $n, $code, $sw.Elapsed.TotalSeconds, $first))
    }
  }
  foreach ($k in $c.envs.Keys) { Remove-Item -Path "Env:$k" -ErrorAction SilentlyContinue }
  $detail = if ($lines.Count -gt 0) { ($lines | Select-Object -First 6) -join '<br>' } else { '' }
  $summary.Add(("| {0} | {1} | {2} | {3} | {4} | {5} |" -f $c.name, $c.runs, $pass, $fail, $hang, $detail))
  Write-Host ("{0}: {1} pass, {2} fail, {3} hang" -f $c.name, $pass, $fail, $hang)
}

$summary | Set-Content (Join-Path $res 'summary.md')
if ($env:GITHUB_STEP_SUMMARY) { $summary | Add-Content $env:GITHUB_STEP_SUMMARY }
$summary | Out-Host
exit 0
