# ---------------------------------------------------------------------------
# ui_cube.ps1 -- a rotating wireframe cube for deploy_windows.cmd's live UI.
#
# Run it on its own to watch it spin:
#
#   powershell.exe -NoProfile -ExecutionPolicy Bypass -File ui_cube.ps1
#   powershell.exe -NoProfile -ExecutionPolicy Bypass -File ui_cube.ps1 -Stats
#
# Dot-sourced by ui.ps1, it exports four functions and draws nothing by itself:
#
#   New-CubeState      -Width -Height       -> the state object
#   Write-CubeBlock    $cube $side $cols    draw the block, cursor back on top
#   Clear-CubeBlock    $cube $cols          erase it, so a line can be printed
#   Step-CubeState     $cube                advance the two rotation angles
#
# Eight vertices, rotated about X and Y, divided by depth, joined by twelve
# Bresenham edges into one character buffer with a depth buffer behind it: the
# near edges overwrite the far ones instead of crossing them, and the ramp
# ".:-=+*#%@" darkens each cell with distance. The whole frame goes out in a
# single Write-Host -- roughly 700 characters -- and the cursor is put back on
# the block's first row, so the next frame overwrites it in place. Nothing is
# cleared between frames, which is what keeps it from flickering.
#
# Why the console API and not escape codes: ui.ps1 draws with Write-Host and
# [Console] on purpose, because legacy conhost does not turn on VT processing
# and a deploy log full of "\033[" is worse than no animation. SetCursorPosition
# works in both conhost and Windows Terminal.
#
# ASCII-only source, as ui.ps1 is: Windows PowerShell 5.1 reads a BOM-less .ps1
# as ANSI, so a non-ASCII glyph in this file would not survive.
# ---------------------------------------------------------------------------
param(
  [int]    $Width   = 0,      # cube box, in cells; 0 picks one from the window
  [int]    $Height  = 0,
  [double] $Fps     = 12,
  [double] $Seconds = 0,      # 0 = until a key is pressed
  [string] $Color   = 'Cyan',
  [switch] $Stats,            # print per-frame render cost on the way out
  [switch] $NoRun             # define the functions and return
)

# The ramp runs far -> near, so the corner closest to the eye is the densest.
$script:CUBE_RAMP = '.:-=+*#%@'

# Vertex i takes its sign on each axis from a bit of i, so an edge joins i to i
# with one bit set: the three neighbours of every corner, each counted once.
$script:CUBE_EDGES = New-Object 'System.Collections.Generic.List[int[]]'
foreach ($i in 0..7) {
  foreach ($b in 1, 2, 4) {
    if (-not ($i -band $b)) { $script:CUBE_EDGES.Add(@($i, ($i -bor $b))) }
  }
}

# sqrt(3): half a unit cube's longest diagonal, so the furthest a corner can be
# from the centre. It sets both the scale that just fills the box and the two
# ends of the depth ramp.
$script:CUBE_REACH = 1.7320508075688772
$script:CUBE_DIST = 3.6      # eye distance; smaller is a wider-angle, blockier cube

function New-CubeState {
  param([int] $Width = 34, [int] $Height = 15)
  if ($Width -lt 12) { $Width = 12 }
  if ($Height -lt 7) { $Height = 7 }
  $n = $Width * $Height
  $blank = New-Object 'char[]' $n
  for ($i = 0; $i -lt $n; $i++) { $blank[$i] = ' ' }
  $far = New-Object 'double[]' $n
  for ($i = 0; $i -lt $n; $i++) { $far[$i] = [double]::MaxValue }
  [pscustomobject]@{
    W        = $Width
    H        = $Height
    Blank    = $blank              # copied over Buf each frame; cheaper than a loop
    Far      = $far
    Buf      = New-Object 'char[]' $n
    Zbuf     = New-Object 'double[]' $n
    Rows     = New-Object 'string[]' $Height
    Ax       = 0.55                # a starting tilt, so frame one is not face-on
    Ay       = 0.70
    Origin   = -1                  # buffer row the block starts on; -1 = not drawn
    Broken   = $false              # the console refused a cursor move: stop trying
    Frames   = 0
    RenderMs = 0.0
  }
}

# The two angles turn at rates that do not divide into each other, so the cube
# never falls back into the same pose and the animation has no visible period.
function Step-CubeState {
  param($C)
  $C.Ay += 0.1100
  $C.Ax += 0.0472
  if ($C.Ay -gt 6.2831853) { $C.Ay -= 6.2831853 }
  if ($C.Ax -gt 6.2831853) { $C.Ax -= 6.2831853 }
}

# One frame into $C.Rows. No console work here, so it can be timed on its own.
function Get-CubeRows {
  param($C)
  $w = $C.W; $h = $C.H; $n = $w * $h
  $buf = $C.Buf; $zb = $C.Zbuf
  [Array]::Copy($C.Blank, $buf, $n)
  [Array]::Copy($C.Far, $zb, $n)

  $ca = [Math]::Cos($C.Ax); $sa = [Math]::Sin($C.Ax)
  $cb = [Math]::Cos($C.Ay); $sb = [Math]::Sin($C.Ay)
  $dist = $script:CUBE_DIST
  $reach = $script:CUBE_REACH

  # The furthest any corner can project from the centre, over every pose: a
  # corner is a vector of length reach, so maximising its projected offset
  # y/(dist+z) subject to y^2+z^2 = reach^2 gives reach/sqrt(dist^2-reach^2).
  # Scaling by the box over that is the scale at which the widest pose still
  # just fits. (reach/(dist-reach) is the obvious guess and is far too big: it
  # asks for a corner both fully sideways and fully forward at once, which no
  # rotation can do, and leaves the cube filling about half its box.) A terminal
  # cell is roughly twice as tall as it is wide, hence sx = 2 * sy -- without
  # that the cube reads as a slab.
  $limit = $reach / [Math]::Sqrt($dist * $dist - $reach * $reach)
  $sy = [Math]::Min(($h / 2.0 - 0.6) / $limit, ($w / 2.0 - 1.0) / $limit / 2.0)
  $sx = 2.0 * $sy
  $cx = $w / 2.0
  $cy = $h / 2.0

  $px = New-Object 'double[]' 8
  $py = New-Object 'double[]' 8
  $pd = New-Object 'double[]' 8
  for ($i = 0; $i -lt 8; $i++) {
    $x = if ($i -band 1) { 1.0 } else { -1.0 }
    $y = if ($i -band 2) { 1.0 } else { -1.0 }
    $z = if ($i -band 4) { 1.0 } else { -1.0 }
    $x1 = $x * $cb + $z * $sb          # yaw
    $z1 = $z * $cb - $x * $sb
    $y1 = $y * $ca - $z1 * $sa         # pitch
    $z2 = $z1 * $ca + $y * $sa
    $d = $dist + $z2                   # always positive: dist > reach
    $k = 1.0 / $d
    $px[$i] = $cx + $x1 * $k * $sx
    $py[$i] = $cy - $y1 * $k * $sy
    $pd[$i] = $d
  }

  $near = $dist - $reach
  $span = 2.0 * $reach
  $ramp = $script:CUBE_RAMP
  $last = $ramp.Length - 1

  foreach ($e in $script:CUBE_EDGES) {
    $a = $e[0]; $b = $e[1]
    $x0 = [int][Math]::Round($px[$a]); $y0 = [int][Math]::Round($py[$a]); $d0 = $pd[$a]
    $xe = [int][Math]::Round($px[$b]); $ye = [int][Math]::Round($py[$b]); $d1 = $pd[$b]
    $dx = [Math]::Abs($xe - $x0)
    $dy = -[Math]::Abs($ye - $y0)
    $ix = if ($x0 -lt $xe) { 1 } else { -1 }
    $iy = if ($y0 -lt $ye) { 1 } else { -1 }
    $err = $dx + $dy
    $steps = [Math]::Max($dx, -$dy)
    if ($steps -lt 1) { $steps = 1 }
    $s = 0
    while ($true) {
      if ($x0 -ge 0 -and $x0 -lt $w -and $y0 -ge 0 -and $y0 -lt $h) {
        $idx = $y0 * $w + $x0
        $d = $d0 + ($d1 - $d0) * ($s / $steps)
        if ($d -lt $zb[$idx]) {
          $zb[$idx] = $d
          $g = [int]((($near + $span) - $d) / $span * $last)
          if ($g -lt 0) { $g = 0 } elseif ($g -gt $last) { $g = $last }
          $buf[$idx] = $ramp[$g]
        }
      }
      if ($x0 -eq $xe -and $y0 -eq $ye) { break }
      $e2 = 2 * $err
      if ($e2 -ge $dy) { $err += $dy; $x0 += $ix }
      if ($e2 -le $dx) { $err += $dx; $y0 += $iy }
      $s++
    }
  }

  for ($r = 0; $r -lt $h; $r++) { $C.Rows[$r] = [string]::new($buf, $r * $w, $w) }
  return $C.Rows
}

# Draw the block at $C.Origin and leave the cursor on the row just below it --
# the caller's footer row, which is ui.ps1's existing live line. $Side is an
# optional column of text drawn to the right of the cube, one entry per row;
# $Cols is the usable console width (ui.ps1's Width()).
#
# Every row here ends in a newline, including the last, so the cursor lands on
# the footer row. That row is only ever written WITHOUT a trailing newline, by
# the caller, which is what keeps the buffer from scrolling a row per frame
# once the block reaches the bottom of the screen.
function Write-CubeBlock {
  param($C, [string[]] $Side, [int] $Cols = 100, $Color = 'Cyan')
  if ($C.Broken) { return }
  $sw = New-Object System.Diagnostics.Stopwatch
  $sw.Start()
  $rows = Get-CubeRows $C
  $sb = New-Object System.Text.StringBuilder (($Cols + 1) * $C.H)
  for ($r = 0; $r -lt $C.H; $r++) {
    $line = '  ' + $rows[$r]
    if ($null -ne $Side -and $r -lt $Side.Count -and $Side[$r]) { $line += '   ' + $Side[$r] }
    # Pad, never wrap: a line as wide as the window wraps, and the block would
    # walk down the screen a row per frame.
    if ($line.Length -gt $Cols) { $line = $line.Substring(0, $Cols) }
    [void]$sb.Append($line.PadRight($Cols)).Append("`n")
  }
  try {
    if ($C.Origin -ge 0) { [Console]::SetCursorPosition(0, $C.Origin) }
    Write-Host $sb.ToString() -NoNewline -ForegroundColor $Color
    # Learn the origin back from where the cursor ended up rather than trusting
    # the old value: the first frame scrolls the buffer to make room, and so
    # does anything else that prints while the block is on screen.
    $top = [Console]::CursorTop - $C.H
    if ($top -lt 0) { $top = 0 }
    $C.Origin = $top
  } catch {
    # A console that will not take a cursor move cannot host this. Give up on
    # the cube for the rest of the run; the caller keeps its plain output.
    $C.Broken = $true
    $C.Origin = -1
  }
  $sw.Stop()
  $C.Frames++
  $C.RenderMs += $sw.Elapsed.TotalMilliseconds
}

# Erase the block and its footer row and leave the cursor on the block's first
# row, ready for a line that must stay on screen -- a finished stage, a
# warning, a compiler error. $Foot is how many rows below the cube belong to
# the caller (ui.ps1: 1, for the live line).
function Clear-CubeBlock {
  param($C, [int] $Cols = 100, [int] $Foot = 1)
  if ($C.Origin -lt 0) { return }
  $blank = ' ' * $Cols
  $n = $C.H + $Foot
  try {
    [Console]::SetCursorPosition(0, $C.Origin)
    for ($r = 0; $r -lt $n; $r++) {
      [Console]::Write($blank)
      # No newline after the last row: at the bottom of the buffer it would
      # scroll, and take the block's anchor with it.
      if ($r -lt $n - 1) { [Console]::Write("`n") }
    }
    [Console]::SetCursorPosition(0, $C.Origin)
  } catch { $C.Broken = $true }
  $C.Origin = -1
}

# A box that fits the window, or $null when there is no room for one. Below
# this the cube is more in the way than the one progress line it sits over: the
# banner is 11 rows, the live line 1, and a few finished stages have to stay
# visible or the animation is all there is to look at.
function Get-CubeSize {
  param([int] $Cols, [int] $Rows)
  if ($Cols -lt 60 -or $Rows -lt 26) { return $null }
  $h = [Math]::Min(15, $Rows - 11)
  if ($h -lt 9) { return $null }
  $w = [Math]::Min(2 * $h + 4, $Cols - 34)
  if ($w -lt 20) { return $null }
  return @($w, $h)
}

if ($NoRun -or $MyInvocation.InvocationName -eq '.') { return }

# ---------------------------------------------------------------------------
# standalone
# ---------------------------------------------------------------------------
if ([Console]::IsOutputRedirected) {
  # The same decision ui.ps1 makes. Redirected output gets words, not frames.
  Write-Output 'ui_cube.ps1: output is redirected, so nothing is animated.'
  Write-Output 'Run it at a terminal to see the cube.'
  exit 0
}

$cols = [Math]::Max(40, [Console]::WindowWidth - 1)
$rows = [Console]::WindowHeight
if ($Width -le 0 -or $Height -le 0) {
  $size = Get-CubeSize $cols $rows
  if ($null -eq $size) {
    Write-Output "ui_cube.ps1: this window is ${cols}x${rows}; the cube wants at least 60x24."
    Write-Output 'Pass -Width and -Height to force a size.'
    exit 0
  }
  if ($Width -le 0) { $Width = $size[0] }
  if ($Height -le 0) { $Height = $size[1] }
}

$cube = New-CubeState -Width $Width -Height $Height
$delay = [int](1000.0 / [Math]::Max(1.0, $Fps))
$start = [DateTime]::Now
$keys = $false

Write-Host ''
Write-Host '  ui_cube.ps1' -ForegroundColor White -NoNewline
Write-Host "   $Width x $Height box, $Fps fps, press any key to stop" -ForegroundColor DarkGray
Write-Host ''
try {
  try { [Console]::CursorVisible = $false } catch { }
  try { [Console]::TreatControlCAsInput = $true; $keys = $true } catch { }
  while ($true) {
    $t = [DateTime]::Now - $start
    # Stand-ins for what ui.ps1 knows while a stage runs.
    $side = @(
      '',
      'Objeck deploy  windows-x64',
      '',
      ('stage   ' + '{0,2}/16' -f 6),
      'building  vm.vcxproj',
      ('elapsed   {0}:{1:00}' -f [int]$t.TotalMinutes, $t.Seconds),
      '',
      ('frames    {0}' -f $cube.Frames),
      ('render    {0:0.00} ms/frame' -f $(if ($cube.Frames) { $cube.RenderMs / $cube.Frames } else { 0 }))
    )
    Write-CubeBlock $cube $side $cols $Color
    if ($cube.Broken) { break }
    # The footer row: where ui.ps1's live line goes. Written with no trailing
    # newline, which is what stops the block scrolling a row per frame.
    $fps = if ($t.TotalSeconds -gt 0.5) { $cube.Frames / $t.TotalSeconds } else { 0 }
    $foot = '  compiler, vm, debugger, repl   {0}:{1:00}   {2:0.0} fps' -f `
      [int]$t.TotalMinutes, $t.Seconds, $fps
    [Console]::Write("`r")
    Write-Host $foot.PadRight($cols) -NoNewline -ForegroundColor DarkGray
    Step-CubeState $cube
    if ($keys -and [Console]::KeyAvailable) { [void][Console]::ReadKey($true); break }
    if ($Seconds -gt 0 -and $t.TotalSeconds -ge $Seconds) { break }
    Start-Sleep -Milliseconds $delay
  }
} finally {
  Clear-CubeBlock $cube $cols 1
  if ($keys) { try { [Console]::TreatControlCAsInput = $false } catch { } }
  try { [Console]::CursorVisible = $true } catch { }
}

if ($cube.Broken) {
  Write-Host '  this console would not take a cursor move; the cube is off' -ForegroundColor Yellow
}
if ($Stats -and $cube.Frames -gt 0) {
  $el = ([DateTime]::Now - $start).TotalSeconds
  Write-Host ('  {0} frames in {1:0.0}s -- {2:0.0} fps, {3:0.00} ms of render per frame' -f `
    $cube.Frames, $el, ($cube.Frames / [Math]::Max(0.001, $el)), ($cube.RenderMs / $cube.Frames)) -ForegroundColor DarkGray
}
