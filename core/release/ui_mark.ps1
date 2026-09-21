# ---------------------------------------------------------------------------
# ui_mark.ps1 -- the Objeck mark as a build spinner.
#
# The mark is the octagon from docs/web/style/objeck-logo.png with the check
# inside it. Here it turns about a vertical axis at the head of a build's live
# line, seven character cells wide and three tall. Its silhouette swings from
# a full octagon to an upright bar and back, which is the clearest motion
# available in fourteen dots by twelve, and the question it exists to answer
# is "is this build still alive" -- so no two frames in a row are the same
# picture, and a stopped build looks stopped within a tenth of a second.
#
# A stage finishing puts the check inside the ring for two thirds of a second:
# the logo briefly completing itself, and the only thing a milestone does. A
# finished build leaves it there.
#
# Dot-source it and use six functions. It owns no cursor and prints nothing --
# it hands back strings and the caller puts them in its own line, which is
# what lets ui.ps1 and ui.sh share it without either tearing the other.
#
#   $m = New-Mark                   $null when the terminal cannot show it
#   Step-Mark $m                    advance one frame
#   Set-MarkMilestone $m            a stage finished: hold the check for a beat
#   Set-MarkDone $m                 the build finished: hold it for good
#   Get-Mark $m                     $m.Rows strings, one per screen row
#   Get-MarkBar $m $done $total $n  a progress bar in the same alphabet
#
# ui_mark.sh is the same component for POSIX sh. Both read the same generated
# table, so a build looks like Objeck wherever it runs.
#
# THE FRAMES ARE A TABLE, not geometry: twenty-eight small pictures are easier
# to review than the code that drew them, and a table cannot drift between two
# platforms. The code that drew them is still here, below -Generate, so nobody
# has to take the table on faith:
#
#   powershell -File ui_mark.ps1 -Generate -MarkRing
#
# ASCII-only source, as ui.ps1 is: Windows PowerShell 5.1 reads a BOM-less
# .ps1 as ANSI, so each braille cell is stored as the low byte of its U+28xx.
# ---------------------------------------------------------------------------
# Only -Generate concerns a caller; the rest shape the table it prints. The
# names are deliberately long because dot-sourcing drops every one of them
# into the caller's scope, and ui.ps1 is full of short ones.
param(
  [switch] $Generate,
  [int]    $MarkCells = 7,
  [int]    $MarkRows = 3,
  [int]    $MarkCount = 28,
  [double] $MarkRadius = 0.92, # of the half-canvas, so the ring keeps a margin
  [int]    $MarkCreep = 3,     # eighths of a turn the ring also creeps in its own plane
  [double] $MarkCheck = 1.2,   # size of the check in the settled frame
  [switch] $MarkRing,          # draw the ring behind the check too (the shipped look)
  [switch] $MarkShow           # print the frames instead of the tables
)

# The blue of the mark, sampled from objeck-logo.png: #0998E3, and the whole
# logo is that one colour.
$script:MARK_RGB = '9;152;227'

# 28 frames, 7 cells x 3 rows, creep 3/8, check 1.2
$script:MARK_CELLS = 7
$script:MARK_ROWS = 3
$script:MARK_SPIN_HEX = @(
  '00601212128400b800000000004700112424240a00', '00601212122440b800000000004708122424240a00', '005412122284001844000000184400112224241c00',
  '0054120911a20038400000000846002384c0241c00', '0060140a11a2003041000000880600238460140a00', '0060120912840030410000008806001124c0240a00',
  '00a012112240000087000000b10000082284240300', '00802412324000008e000000710000082624120100', '00801412a2000000b8000000470000002324140100',
  '0000541286000000004700b80000000031241c0000', '0000701284000000004700b80000000011240e0000', '0000b0324000000000b80047000000000826070000',
  '000080364000000000b80047000000000836010000', '000000fe000000000000ff0000000000007f000000', '000000470000000000004700000000000047000000',
  '000000fc000000000000ff0000000000005f000000', '000000564400000000b8004700000000183c000000', '0000b0124400000000b80047000000001824070000',
  '0000701284000000004700b80000000011240e0000', '0000701286000000004700b80000000031240e0000', '00801412a2000000b8000000470000002324140100',
  '00802412124400004e000000780000182424120100', '00a01211224000008e000000710000082284240300', '00a012112284002043000000a80200112284240300',
  '0060120912a400b0010000008007001324c0240a00', '00700a0912a400b0010000008007001324c0600e00', '005412122284003840000000084600112224241c00',
  '00601212228440b800000000004708112224240a00'
)
# Face-on with the check inside.
$script:MARK_DONE_HEX = '0060121212e400b8a0408074034700113d3e240a00'

# The bar is drawn in the same alphabet as the mark, so the line reads as one
# thing: a solid braille cell for a finished stage, a half cell for the one in
# progress, and the bottom two dots as a track under the rest.
$script:MARK_BAR_FULL = [string][char]0x28FF
$script:MARK_BAR_HALF = [string][char]0x28C7
$script:MARK_BAR_TRACK = [string][char]0x28C0

function script:Expand-Mark([string] $Hex) {
  $n = [int]($Hex.Length / 2)
  $c = New-Object 'char[]' $n
  for ($i = 0; $i -lt $n; $i++) { $c[$i] = [char](0x2800 + [Convert]::ToInt32($Hex.Substring($i * 2, 2), 16)) }
  $rows = New-Object 'string[]' $script:MARK_ROWS
  for ($r = 0; $r -lt $script:MARK_ROWS; $r++) { $rows[$r] = [string]::new($c, $r * $script:MARK_CELLS, $script:MARK_CELLS) }
  , $rows
}

# Braille and 24-bit colour, or nothing at all. Windows Terminal and VS Code
# both carry the glyphs and both parse the escape; legacy conhost carries
# neither, and a redirected stream or a CI leg must never see either. There is
# no half-resolution braille, so where this returns $null the caller keeps the
# plain spinner it had before.
function New-Mark {
  if ([Console]::IsOutputRedirected) { return $null }
  if ($env:CI -or $env:GITHUB_ACTIONS) { return $null }
  if (-not ($env:WT_SESSION -or $env:WT_PROFILE_ID -or $env:TERM_PROGRAM -eq 'vscode')) { return $null }
  $esc = [string][char]27
  $col = -not $env:NO_COLOR
  [pscustomobject]@{
    Tick = 0
    Beat = 0                        # frames of check still owed
    Done = $false
    Rows = $script:MARK_ROWS
    Cols = $script:MARK_CELLS
    Spin = @($script:MARK_SPIN_HEX | ForEach-Object { Expand-Mark $_ })
    Fin  = (Expand-Mark $script:MARK_DONE_HEX)
    On   = $(if ($col) { $esc + '[38;2;' + $script:MARK_RGB + 'm' } else { '' })
    Dim  = $(if ($col) { $esc + '[38;2;72;84;96m' } else { '' })
    Off  = $(if ($col) { $esc + '[0m' } else { '' })
  }
}

function Step-Mark($M) {
  if ($null -eq $M -or $M.Done) { return }
  $M.Tick++
  if ($M.Beat -gt 0) { $M.Beat-- }
}

# Seven frames at the ten a second the deploys tick at: long enough to read,
# short enough that the next one still feels like an event.
function Set-MarkMilestone($M) { if ($null -ne $M) { $M.Beat = 7 } }

function Set-MarkDone($M) { if ($null -ne $M) { $M.Done = $true } }

function Get-Mark($M) {
  if ($null -eq $M) { return @() }
  $g = if ($M.Done -or $M.Beat -gt 0) { $M.Fin } else { $M.Spin[$M.Tick % $M.Spin.Count] }
  $out = New-Object 'string[]' $M.Rows
  for ($r = 0; $r -lt $M.Rows; $r++) { $out[$r] = $M.On + $g[$r] + $M.Off }
  , $out
}

# Half-cell granularity, so a fifteen-stage deploy still moves the bar twice
# per stage instead of once.
function Get-MarkBar($M, [int] $Done, [int] $Total, [int] $Cells = 20) {
  if ($null -eq $M) { return '' }
  if ($Total -lt 1) { $Total = 1 }
  $halves = [int][Math]::Floor([Math]::Max(0, [Math]::Min($Done, $Total)) * 2.0 * $Cells / $Total)
  $full = [int][Math]::Floor($halves / 2)
  $sb = New-Object System.Text.StringBuilder ($Cells + 24)
  [void]$sb.Append($M.On)
  if ($full -gt 0) { [void]$sb.Append($script:MARK_BAR_FULL, $full) }
  if ($halves % 2 -eq 1) { [void]$sb.Append($script:MARK_BAR_HALF) }
  $used = $full + ($halves % 2)
  if ($used -lt $Cells) { [void]$sb.Append($M.Dim).Append($script:MARK_BAR_TRACK, $Cells - $used) }
  [void]$sb.Append($M.Off)
  $sb.ToString()
}

if (-not $Generate) { return }

# ---------------------------------------------------------------------------
# the generator
# ---------------------------------------------------------------------------
# An octagon of eight unit vertices, flat side up like the logo's, drawn as a
# closed ring of Bresenham lines into a grid of braille dots. It turns about
# the vertical axis, which is what makes the silhouette swing, and at the same
# time creeps round within its own plane, which is what keeps something moving
# in the frames where it is nearly face-on and the width has stopped changing.
# Over the whole table the in-plane creep comes to exactly one eighth of a
# turn, so the last frame runs back into the first.
$TAU = 6.283185307179586
$DW = 2 * $MarkCells; $DH = 4 * $MarkRows
$CX = ($DW - 1) / 2.0; $CY = ($DH - 1) / 2.0
# U+2800 dot bits: two columns of three, then a fourth row in the top two bits.
$BIT = New-Object 'int[]' 8
for ($r = 0; $r -lt 4; $r++) {
  for ($c = 0; $c -lt 2; $c++) { $BIT[$r * 2 + $c] = 1 -shl $(if ($r -lt 3) { $c * 3 + $r } else { 6 + $c }) }
}

function Plot($Grid, [double] $ax, [double] $ay, [double] $bx, [double] $by) {
  $x0 = [int][Math]::Round($ax); $y0 = [int][Math]::Round($ay)
  $x1 = [int][Math]::Round($bx); $y1 = [int][Math]::Round($by)
  $dx = [Math]::Abs($x1 - $x0); $dy = -[Math]::Abs($y1 - $y0)
  $sx = $(if ($x0 -lt $x1) { 1 } else { -1 }); $sy = $(if ($y0 -lt $y1) { 1 } else { -1 })
  $err = $dx + $dy
  while ($true) {
    if ($x0 -ge 0 -and $x0 -lt $DW -and $y0 -ge 0 -and $y0 -lt $DH) {
      $j = ($y0 -shr 2) * $MarkCells + ($x0 -shr 1)
      $Grid[$j] = $Grid[$j] -bor $BIT[(($y0 -band 3) * 2) + ($x0 -band 1)]
    }
    if ($x0 -eq $x1 -and $y0 -eq $y1) { break }
    $e2 = 2 * $err
    if ($e2 -ge $dy) { $err += $dy; $x0 += $sx }
    if ($e2 -le $dx) { $err += $dx; $y0 += $sy }
  }
}

# Turn about Y by $swing, after turning within the plane by $spin, then drop
# the depth: an orthographic view is all the projection this size can show.
function Shape([double] $swing, [double] $spin, [bool] $check) {
  $grid = New-Object 'int[]' ($MarkCells * $MarkRows)
  $cs = [Math]::Cos($spin); $sn = [Math]::Sin($spin)
  $cw = [Math]::Cos($swing) * $MarkRadius
  $ch = $MarkRadius
  $px = New-Object 'double[]' 8; $py = New-Object 'double[]' 8
  for ($i = 0; $i -lt 8; $i++) {
    $a = $TAU / 16.0 + $i * $TAU / 8.0
    $ux = [Math]::Cos($a); $uy = [Math]::Sin($a)
    $rx = $ux * $cs - $uy * $sn
    $ry = $ux * $sn + $uy * $cs
    $px[$i] = $CX + $rx * $cw * $CX
    $py[$i] = $CY - $ry * $ch * $CY
  }
  if (-not $check -or $MarkRing) {
    for ($i = 0; $i -lt 8; $i++) { Plot $grid $px[$i] $py[$i] $px[($i + 1) % 8] $py[($i + 1) % 8] }
  }
  if ($check) {
    # The logo's tick, in the same plane as the ring so it turns with it.
    # Parenthesised on purpose: PowerShell's comma binds tighter than "*", so
    # "a * $t, b * $t" multiplies by the array rather than by the scalar and
    # fails at run time instead of at parse time.
    $k = @(@((-0.42 * $MarkCheck), (-0.10 * $MarkCheck)), @((-0.08 * $MarkCheck), (-0.50 * $MarkCheck)), @((0.58 * $MarkCheck), (0.50 * $MarkCheck)))
    $qx = New-Object 'double[]' 3; $qy = New-Object 'double[]' 3
    for ($i = 0; $i -lt 3; $i++) {
      $rx = $k[$i][0] * $cs - $k[$i][1] * $sn
      $ry = $k[$i][0] * $sn + $k[$i][1] * $cs
      $qx[$i] = $CX + $rx * $cw * $CX
      $qy[$i] = $CY - $ry * $ch * $CY
    }
    # Twice, a dot apart. A one-dot stroke inside a one-dot ring is a scratch;
    # the tick has to be the heaviest thing in the frame or the eye misses it.
    foreach ($off in 0.0, 1.0) {
      Plot $grid $qx[0] ($qy[0] + $off) $qx[1] ($qy[1] + $off)
      Plot $grid $qx[1] ($qy[1] + $off) $qx[2] ($qy[2] + $off)
    }
  }
  ($grid | ForEach-Object { '{0:x2}' -f $_ }) -join ''
}

$hex = @()
for ($f = 0; $f -lt $MarkCount; $f++) {
  # Half a turn is the whole visual cycle for the swing: the ring's width is
  # |cos(angle)|, and a flat-topped octagon mirrored left to right is the same
  # octagon. The creep is the ring turning in its own plane at the same time,
  # by a whole number of eighths over that half turn so the table still closes
  # on itself. Without it the frames near face-on are identical pictures --
  # the width barely changes there -- and a spinner that repeats itself four
  # frames running is a spinner that looks stopped.
  $hex += Shape ($TAU * 0.5 * $f / $MarkCount) ($TAU / 8.0 * $MarkCreep * $f / $MarkCount) $false
}
$fin = Shape 0.0 0.0 $true

if ($MarkShow) {
  foreach ($h in $hex + @($fin)) {
    $s = ''
    for ($i = 0; $i -lt $h.Length; $i += 2) { $s += [char](0x2800 + [Convert]::ToInt32($h.Substring($i, 2), 16)) }
    Write-Output $s
  }
  return
}

function script:ToGlyphs([string] $Hex) {
  $g = ''
  for ($i = 0; $i -lt $Hex.Length; $i += 2) { $g += [char](0x2800 + [Convert]::ToInt32($Hex.Substring($i, 2), 16)) }
  $g
}

Write-Output ("# {0} frames, {1} cells x {2} rows, creep {3}/8, check {4}" -f $MarkCount, $MarkCells, $MarkRows, $MarkCreep, $MarkCheck)
Write-Output ("`$script:MARK_CELLS = {0}" -f $MarkCells)
Write-Output ("`$script:MARK_ROWS  = {0}" -f $MarkRows)
Write-Output '$script:MARK_SPIN_HEX = @('
for ($i = 0; $i -lt $hex.Count; $i += 3) {
  $end = [Math]::Min($i + 2, $hex.Count - 1)
  Write-Output ('  ' + (($hex[$i..$end] | ForEach-Object { "'$_'" }) -join ', ') + $(if ($end -lt $hex.Count - 1) { ',' } else { '' }))
}
Write-Output ')'
Write-Output ("`$script:MARK_DONE_HEX = '{0}'" -f $fin)
Write-Output ''
Write-Output '# --- ui_mark.sh: one table per character row, so dash can index it ---'
Write-Output ("UI_MARK_CELLS={0}" -f $MarkCells)
Write-Output ("UI_MARK_ROWS={0}" -f $MarkRows)
for ($r = 0; $r -lt $MarkRows; $r++) {
  $row = $hex | ForEach-Object { (ToGlyphs $_).Substring($r * $MarkCells, $MarkCells) }
  Write-Output ("UI_MARK_R{0}='{1}'" -f $r, ($row -join ' '))
}
$fg = ToGlyphs $fin
for ($r = 0; $r -lt $MarkRows; $r++) {
  Write-Output ("UI_MARK_D{0}='{1}'" -f $r, $fg.Substring($r * $MarkCells, $MarkCells))
}
