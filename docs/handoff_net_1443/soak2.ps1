param([int]$Times = 30, [string]$Bin, [string]$Name = "soak2", [string]$Tests = "query_url_cache_processes", [string]$Out = $env:TEMP)
# CI's differential harness on the URL-cache test with a given bin folder; each
# iteration gets its own OBJECK_SOCK_TRACE folder, kept with the work folder only
# when the iteration diverges or fails. -Bin needs a lib folder beside it (obr
# finds ../lib); work folders go under -Out.
$s = $Out
$tree = (Resolve-Path "$PSScriptRoot\..\..").Path
$env:PATH = "$tree\core\release\deploy-x64\lib\native;$env:PATH"
$fail = 0
for ($i = 1; $i -le $Times; $i++) {
  $work = "$s\$Name\$i"
  New-Item -ItemType Directory -Force "$work\trace" | Out-Null
  $env:OBJECK_SOCK_TRACE = "$work\trace"
  $log = python "$tree\programs\regression\run_differential.py" $Bin ($Tests -split ",") --lib-dir "$tree\core\lib" --timeout 900 --work "$work\w" 2>&1
  $bad = $log | Select-String -CaseSensitive "^(DIFF|FAIL) "
  if ($bad) {
    $fail++
    "$(Get-Date -Format HH:mm:ss) iteration ${i}: " + (($log | Select-String "after 2\d\.\d+ s|rror" | Select-Object -First 3) -join " | ")
    $log | Out-File "$work\run.log"
  } else {
    "$(Get-Date -Format HH:mm:ss) iteration ${i}: ok"
    Remove-Item -Recurse -Force $work -ErrorAction SilentlyContinue
  }
}
"${Name}: $fail of $Times iterations had a divergence or failure"
