# ---------------------------------------------------------------------------
# ui.ps1 -- live progress for deploy_windows.cmd when it is run by hand.
#
# cmd cannot redraw anything while devenv or msbuild blocks it, so the deploy
# re-runs itself under this script (ui.cmd, "relaunch" mode):
#
#   deploy_windows.cmd x64
#     -> powershell -File ui.ps1 x64
#          -> cmd /c deploy_windows.cmd x64 > %TEMP%\objeck-deploy-x64.log 2>&1
#             with UI_CHILD=1, so ui.cmd writes "##ui" marker lines into the log
#
# This process is the only writer to the console. It follows the log and
# redraws one live line ten times a second -- spinner, bar, stage timer and the
# build's latest output line -- prints a line with its duration for each
# finished stage, surfaces the script's own ERROR:/Warning: lines as they
# appear, and on failure shows the compiler errors and the end of the log. Its
# exit code is the deploy's. Ctrl+C stops the whole build tree.
#
# ASCII-only source on purpose: Windows PowerShell 5.1 reads a .ps1 without a
# BOM as ANSI, so every non-ASCII glyph is built from its code point.
# UI_DEPLOY_SCRIPT overrides the script that is run, for testing this file.
# ---------------------------------------------------------------------------
param([Parameter(ValueFromRemainingArguments = $true)][string[]] $DeployArgs)

$ErrorActionPreference = 'Stop'

$here = Split-Path -Parent $MyInvocation.MyCommand.Path
if ($null -eq $DeployArgs) { $DeployArgs = @() }
$arch = if ($DeployArgs.Count -gt 0) { $DeployArgs[0] } else { 'x64' }
$deploy = if ($env:UI_DEPLOY_SCRIPT) { $env:UI_DEPLOY_SCRIPT } else { Join-Path $here 'deploy_windows.cmd' }
$live = -not [Console]::IsOutputRedirected

function Glyph([int] $CodePoint) { [string][char]$CodePoint }
if ($env:WT_SESSION -or $env:TERM_PROGRAM -eq 'vscode') {
  # Windows Terminal's and VS Code's fonts carry these; conhost's Consolas does not.
  $OK = Glyph 0x2713; $BAD = Glyph 0x2717; $ELL = Glyph 0x2026
  $SPIN = @(0x280B, 0x2819, 0x2839, 0x2838, 0x283C, 0x2834, 0x2826, 0x2827, 0x2807, 0x280F) | ForEach-Object { Glyph $_ }
  # The bar is one solid line for every cell, coloured by state. A full block
  # plus a light shade (U+2588/U+2591) does not survive terminal fonts: VS Code
  # draws the shade as a dither pattern, which read as solid in blue (6% looked
  # full) and as nothing at all in gray.
  $FILL = Glyph 0x2501; $EMPTY = Glyph 0x2501
} else {
  $OK = Glyph 0x221A; $BAD = 'x'; $ELL = '...'
  $SPIN = @('|', '/', '-', '\')
  $FILL = '#'; $EMPTY = '-'
}

$ART = @(
  '  ___   _        _              _',
  ' / _ \ | |__    (_)  ___   ___ | | __',
  '| | | || ''_ \   | | / _ \ / __|| |/ /',
  '| |_| || |_) |  | ||  __/| (__ |   <',
  ' \___/ |_.__/  _/ | \___| \___||_|\_\',
  '              |__/'
)

$version = 'dev'
$versionH = Join-Path $here '..\shared\version.h'
if (Test-Path -LiteralPath $versionH) {
  $m = Select-String -LiteralPath $versionH -Pattern 'VERSION_STRING\s+L"([^"]+)"' | Select-Object -First 1
  if ($m) { $version = $m.Matches[0].Groups[1].Value }
}

# A lingering process can still hold last run's log open; never fail on that.
$log = Join-Path $env:TEMP "objeck-deploy-$arch.log"
try { if (Test-Path -LiteralPath $log) { Remove-Item -LiteralPath $log -Force } }
catch { $log = Join-Path $env:TEMP "objeck-deploy-$arch-$PID.log" }

$S = @{
  Total = 0; Step = 0; Done = 0; Label = 'starting'; Activity = ''; Ok = $null; Tick = 0; LastKey = $null
  RunStart = [DateTime]::Now; StageStart = [DateTime]::Now
  Errors = New-Object 'System.Collections.Generic.List[string]'
  Tail = New-Object 'System.Collections.Generic.Queue[string]'
}

function W([string] $Text, $Color) {
  if ($null -ne $Color) { Write-Host $Text -NoNewline -ForegroundColor $Color } else { Write-Host $Text -NoNewline }
}
function NL { Write-Host '' }
function Width { if ($live) { [Math]::Max(40, [Console]::WindowWidth - 1) } else { 100 } }
function Clear-Live { if ($live) { [Console]::Write("`r" + (' ' * (Width)) + "`r"); $S.LastKey = $null } }
function Dur([TimeSpan] $t) { '{0}m {1:00}s' -f [int][Math]::Floor($t.TotalMinutes), $t.Seconds }

function Stage-Line([string] $Glyph, $Color, [string] $Label, [string] $Right, $RightColor = 'DarkGray') {
  Clear-Live
  W '  '; W $Glyph $Color; W ' '; W ($Label.PadRight(32)); W $Right $RightColor; NL
}

function Finish-Stage {
  if ($S.Step -gt 0) {
    Stage-Line $OK 'Green' $S.Label (Dur ([DateTime]::Now - $S.StageStart))
    $S.Done++
  }
}

function Note([string] $Glyph, $Color, [string] $Text) {
  Clear-Live
  W '    '; W "$Glyph $Text" $Color; NL
}

function Draw-Live {
  if (-not $live) { return }
  $w = Width
  $frame = $SPIN[$S.Tick % $SPIN.Count]
  $S.Tick++
  $t = [DateTime]::Now - $S.StageStart
  # Most frames only move the spinner. Repaint that one cell unless something
  # else on the line has changed: Write-Host is the costly part of a frame.
  $key = '{0}|{1}|{2}|{3}|{4}|{5}' -f $w, $S.Step, $S.Total, [int][Math]::Floor($t.TotalSeconds), $S.Label, $S.Activity
  if ($key -eq $S.LastKey) {
    [Console]::Write("`r  ")
    W $frame 'Cyan'
    return
  }
  $S.LastKey = $key
  $total = [Math]::Max(1, $S.Total)
  $done = [Math]::Max(0, $S.Step - 1)
  $cells = 20
  $f = [Math]::Min($cells, [int][Math]::Floor($done * $cells / $total))
  # No percentage. The only progress this script can see is a stage boundary, so
  # a number sat still through a long stage (the solution rebuild takes minutes)
  # and then jumped. The bar counts finished stages, the counter names the
  # running one, and the stage clock is what shows the build is alive.
  $n = if ($S.Total -gt 0) { '{0,2}/{1}' -f $S.Step, $S.Total } else { '' }
  $clock = '{0}:{1:00}' -f [int][Math]::Floor($t.TotalMinutes), $t.Seconds

  # "  F BAR  N  LABEL  CLOCK  ACTIVITY"
  $fixed = 2 + 1 + 1 + $cells + 2 + $n.Length + 2 + 2 + $clock.Length
  $label = $S.Label
  if ($label.Length -gt ($w - $fixed)) { $label = $label.Substring(0, [Math]::Max(0, $w - $fixed)) }
  $used = $fixed + $label.Length
  $act = ''
  $room = $w - $used - 2
  if ($room -gt 12 -and $S.Activity) {
    $a = $S.Activity
    if ($a.Length -gt $room) { $a = $a.Substring(0, $room - $ELL.Length) + $ELL }
    $act = '  ' + $a
  }

  [Console]::Write("`r")
  W '  '; W $frame 'Cyan'; W ' '; W ($FILL * $f) 'Blue'; W ($EMPTY * ($cells - $f)) 'DarkGray'; W '  '; W $n 'Gray'; W '  '
  W $label 'White'; W "  $clock" 'DarkGray'; W $act 'DarkGray'
  $len = $used + $act.Length
  if ($len -lt $w) { W (' ' * ($w - $len)) }
}

function Handle-Line([string] $Line) {
  if ($Line -match '^##ui (\w+) ?(.*)$') {
    $kind = $Matches[1]
    $rest = $Matches[2].Trim()
    switch ($kind) {
      'init' { $S.Total = [int]$rest }
      'step' {
        Finish-Stage
        $parts = $rest -split ' ', 2
        $S.Step = [int]$parts[0]
        $S.Label = if ($parts.Count -gt 1) { $parts[1] } else { '' }
        $S.StageStart = [DateTime]::Now
        $S.Activity = ''
        $S.Errors.Clear()
        $S.Tail.Clear()
      }
      'warn' { Note '!' 'Yellow' $rest }
      'ok' { Finish-Stage; $S.Step = 0; $S.Ok = $rest }
    }
    return
  }

  $text = $Line.Trim()
  if (-not $text) { return }
  if ($text -match '^[()]+$') { return }   # the edges of an echoed if (...) block

  # With echo on, cmd writes each command after a "C:\...\core\release>" prompt.
  # Keep the command, which says what ran, without the prompt. An echoed "echo"
  # or "rem" only repeats the line that follows it, so drop those -- otherwise an
  # echoed error line is counted as a second error.
  if ($text -match '^[A-Za-z]:\\[^>]*>(.*)$') {
    $command = $Matches[1].Trim() -replace '\s+', ' '
    if (-not $command -or $command -match '^(echo|rem)\b') { return }
    Remember ('> ' + $command)
    $S.Activity = $command
    return
  }

  Remember $text
  # The live line says what is building. MSVC prefixes each line with its
  # project number ("4>") and interleaves chatter that says nothing about
  # progress; the log keeps all of it, and the error scan below reads $text.
  $act = ($text -replace '^\d+>\s*', '') -replace '\s+', ' '
  if ($act -match '^-+ (?:Rebuild All|Build) started: Project: ([^,]+)') { $act = 'building ' + $Matches[1] }
  elseif ($act -match '^\S+\.vcxproj -> (.+)$') { $act = 'built ' + [IO.Path]::GetFileName($Matches[1]) }
  if ($act -notmatch '^(Previous IPDB not found|Generating code|Finished generating code|All \d+ functions were compiled|\d+ of \d+ functions|Creating library |-+$)') {
    $S.Activity = $act
  }

  if ($text -cmatch '^ERROR:') { Note $BAD 'Red' $text }
  elseif ($text -cmatch '^Warning:') { Note '!' 'Yellow' $text.Substring(8).Trim() }
  elseif ($text -match ':\s*(fatal\s+)?error\s+[A-Za-z]+\d+\s*:' -and $S.Errors.Count -lt 20 -and -not $S.Errors.Contains($text)) {
    $S.Errors.Add($text)
  }
}

function Remember([string] $Text) {
  $S.Tail.Enqueue($Text)
  while ($S.Tail.Count -gt 25) { [void]$S.Tail.Dequeue() }
}

# Stops the deploy and everything it started (devenv, msbuild, cl).
function Stop-Tree($Process) {
  $kill = New-Object System.Diagnostics.ProcessStartInfo 'taskkill.exe', "/PID $($Process.Id) /T /F"
  $kill.UseShellExecute = $false
  $kill.RedirectStandardOutput = $true
  $kill.RedirectStandardError = $true
  ([System.Diagnostics.Process]::Start($kill)).WaitForExit()
}

function CtrlC-Pressed {
  if (-not $script:keys) { return $false }
  try {
    while ([Console]::KeyAvailable) {
      $k = [Console]::ReadKey($true)
      if ($k.Key -eq [ConsoleKey]::C -and ($k.Modifiers -band [ConsoleModifiers]::Control)) { return $true }
    }
  } catch { }
  return $false
}

# ---- banner -----------------------------------------------------------------
NL
foreach ($row in $ART) { W $row 'Cyan'; NL }
NL
W '  '; W "Objeck $version" 'White'; W "  windows-$arch" 'DarkGray'; NL
W "  tool output: $log" 'DarkGray'; NL
W '  pass -v to stream it instead' 'DarkGray'; NL
NL

# ---- run the deploy, following its log ---------------------------------------
$env:UI_CHILD = '1'
$quoted = ($DeployArgs | ForEach-Object { if ($_ -match '[\s&()^]') { '"' + $_ + '"' } else { $_ } }) -join ' '
$psi = New-Object System.Diagnostics.ProcessStartInfo
$psi.FileName = $env:ComSpec
$psi.Arguments = '/d /s /c ""' + $deploy + '" ' + $quoted + ' > "' + $log + '" 2>&1"'
$psi.UseShellExecute = $false
$psi.WorkingDirectory = (Get-Location).ProviderPath

$keys = $false
$interrupted = $false
$code = 1
$proc = $null
try {
  if ($live) {
    try { [Console]::CursorVisible = $false } catch { }
    # Ctrl+C arrives as a key, not a signal, so the build tree can be stopped
    # cleanly instead of every process on the console being interrupted at once.
    try { [Console]::TreatControlCAsInput = $true; $keys = $true } catch { }
  }

  $proc = [System.Diagnostics.Process]::Start($psi)
  $oem = [Text.Encoding]::GetEncoding([Globalization.CultureInfo]::CurrentCulture.TextInfo.OEMCodePage)
  $decoder = $oem.GetDecoder()
  $bytes = New-Object byte[] 65536
  $chars = New-Object char[] 65536
  $pending = ''
  $fs = $null

  while ($true) {
    $exited = $proc.HasExited   # read before the drain, so the last bytes are not missed
    if ($null -eq $fs -and (Test-Path -LiteralPath $log)) {
      try { $fs = [IO.File]::Open($log, [IO.FileMode]::Open, [IO.FileAccess]::Read, [IO.FileShare]::ReadWrite -bor [IO.FileShare]::Delete) } catch { }
    }
    if ($null -ne $fs) {
      while (($got = $fs.Read($bytes, 0, $bytes.Length)) -gt 0) {
        $count = $decoder.GetChars($bytes, 0, $got, $chars, 0)
        $pending += [string]::new($chars, 0, $count)
        $lines = $pending.Split("`n")
        $pending = $lines[$lines.Count - 1]
        for ($i = 0; $i -lt $lines.Count - 1; $i++) { Handle-Line ($lines[$i].TrimEnd("`r")) }
      }
    }
    if ($exited) { break }
    if (CtrlC-Pressed) { $interrupted = $true; break }
    Draw-Live
    Start-Sleep -Milliseconds 100
  }
  if ($pending) { Handle-Line ($pending.TrimEnd("`r")) }

  if ($interrupted) {
    Stop-Tree $proc
    $code = 130
  } else {
    $proc.WaitForExit()
    $code = $proc.ExitCode
  }
  if ($null -ne $fs) { $fs.Dispose() }
} finally {
  # Whatever ended this loop -- Ctrl+C, or an error in this script -- the build
  # must not carry on unwatched, still holding the log open.
  if ($null -ne $proc -and -not $proc.HasExited) { Stop-Tree $proc }
  Clear-Live
  if ($keys) { try { [Console]::TreatControlCAsInput = $false } catch { } }
  if ($live) { try { [Console]::CursorVisible = $true } catch { } }
}

# ---- report -------------------------------------------------------------------
$elapsed = Dur ([DateTime]::Now - $S.RunStart)
$total = if ($S.Total -gt 0) { $S.Total } else { $S.Step }
if ($interrupted) {
  Stage-Line $BAD 'Red' $S.Label 'interrupted' 'Red'
  NL; W "  stopped by Ctrl+C after $elapsed; the build was terminated" 'Red'; NL
} elseif ($code -eq 0 -and $null -ne $S.Ok) {
  W '  '; W ($FILL * 20) 'Green'; W "  $($S.Done)/$total" 'Gray'; NL; NL
  W '  '; W "$OK $($S.Ok)" 'Green'; W "  $($S.Done) stages in $elapsed" 'DarkGray'; NL
  W "  tool output: $log" 'DarkGray'; NL
} else {
  if ($S.Step -gt 0) { Stage-Line $BAD 'Red' $S.Label (Dur ([DateTime]::Now - $S.StageStart)) 'Red' }
  NL
  if ($code -ne 0) { W "  FAILED at stage $($S.Step)/$total with exit code $code, after $elapsed" 'Red' }
  else { W "  stopped before finishing, at stage $($S.Step)/$total, after $elapsed" 'Yellow' }
  NL
  if ($S.Errors.Count -gt 0) {
    NL; W '  compiler errors:' 'Red'; NL
    foreach ($e in $S.Errors) { W "    $e"; NL }
  }
  NL; W '  last lines of the log:' 'DarkGray'; NL
  $tail = @($S.Tail.ToArray())
  for ($i = [Math]::Max(0, $tail.Count - 25); $i -lt $tail.Count; $i++) { W ('    ' + $tail[$i]); NL }
  W "  full log: $log   (-v streams everything instead)" 'DarkGray'; NL
}
exit $code
