# Probe loop for the windows-arm64 core_thread_gc_stress flake. Runs the
# original test and a diagnostic copy many times under several VM
# configurations, counts failures and hangs, keeps every failing run's output,
# and writes a summary (also to the GitHub step summary when present).
#
# -Profile diag concentrates the budget on the ONE configuration that can
# answer anything. The original split spends most of its runs on `orig`, which
# prints a total and nothing else; `diag` reports which worker, which round,
# how many nodes its list had and the first bad value. Across every probe run
# to date the diag build has failed exactly once -- and that one was a silent
# 0xC0000005 that died before printing, so the instrumentation has never
# actually fired. See investigation.md.
#
# -Catcher runs each program under av_catch.py, which puts a faulting address,
# a module+RVA and (if a PDB is around) a symbol name on that silent crash. It
# inherits stdout/stderr, so the diag report still lands in the .out file.
param(
  [Parameter(Mandatory = $true)][string]$Bin,
  [int]$Runs = 40,
  [int]$TimeoutSec = 180,
  [ValidateSet('survey', 'diag')][string]$Mode = 'survey',
  [string]$Catcher,
  [string]$Python = 'python'
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
if ($Mode -eq 'diag') {
  # Phase 1. Everything on the build that can report, with a small `orig` cell
  # kept only so a failure can be told apart from "the diag build does not
  # reproduce it". The jit1/gc1m/jitoff cells are deliberately absent: at the
  # measured ~0.8% a 40-run cell expects 0.7 failures, so 0 against 1 says
  # nothing, and splitting the budget five ways is what kept every cell
  # underpowered. Narrowing by configuration is Phase 4, with 500+ per cell.
  $configs = @(
    @{ name = 'diag-default'; obe = $diag; args = @(); envs = @{}; runs = $Runs },
    @{ name = 'orig-default'; obe = $orig; args = @(); envs = @{}; runs = [math]::Max(1, [int]($Runs / 4)) }
  )
}
else {
  $configs = @(
    @{ name = 'orig-default'; obe = $orig; args = @();                     envs = @{};                              runs = $Runs },
    @{ name = 'diag-default'; obe = $diag; args = @();                     envs = @{};                              runs = $Runs },
    @{ name = 'orig-jit1';    obe = $orig; args = @();                     envs = @{ OBJECK_JIT_THRESHOLD = '1' };  runs = $Runs },
    @{ name = 'orig-gc1m';    obe = $orig; args = @('--gc-threshold=1m');  envs = @{};                              runs = $Runs },
    @{ name = 'orig-jitoff';  obe = $orig; args = @('--jit=off');          envs = @{};                              runs = [math]::Max(1, [int]($Runs / 2)) }
  )
}

# The catcher is opt-in and must prove itself before the loop, not on the one
# run that finally crashes. A probe that silently ran without it would report
# the same bare exit code the issue already has.
$useCatcher = $false
if ($Catcher) {
  if (-not (Test-Path $Catcher)) { throw "catcher not found: $Catcher" }
  $Catcher = (Resolve-Path $Catcher).Path
  # Prove it CATCHES, not merely that it starts. A catcher that runs the program
  # and reports nothing is indistinguishable from a run that did not crash --
  # which is the exact ambiguity this whole probe exists to remove. Deliberately
  # fault a throwaway process and require the report. Uses python rather than
  # obr so the check does not depend on obr's argument parsing.
  $probe = & $Python -I $Catcher --timeout 30 -- $Python -c "import ctypes; ctypes.memset(0,0,4)" 2>&1
  if (-not (($probe -join "`n") -match '\[av\] exception 0xC0000005')) {
    throw "catcher did not report a deliberate access violation; it would not report a real one:`n$($probe -join "`n")"
  }
  $useCatcher = $true
  Write-Host "catcher: $Catcher (verified on a deliberate access violation)"
}

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
    if ($useCatcher) {
      $exe = $Python
      $argList = @('-I', $Catcher, '--timeout', $TimeoutSec, '--', $obr) + $argList
    }
    else {
      $exe = $obr
    }
    $sw = [Diagnostics.Stopwatch]::StartNew()
    $p = Start-Process -FilePath $exe -ArgumentList $argList -NoNewWindow -PassThru `
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
      # stdout carries the diag report; stderr carries the catcher's faulting
      # address. The silent crash has NO stdout at all, which is exactly the
      # case the issue could never say anything about, so read both.
      $etext = (Get-Content $err -Raw -ErrorAction SilentlyContinue)
      $parts = @($text -split "`n" | Where-Object { $_ -match 'FAIL|round=' } | Select-Object -First 3)
      $parts += @($etext -split "`n" | Where-Object { $_ -match '^\[av\]' } | Select-Object -First 4)
      $first = ($parts | ForEach-Object { $_.Trim() }) -join ' / '
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
