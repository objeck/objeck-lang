#!/bin/sh
# ---------------------------------------------------------------------------
# ui_cube.sh -- a rotating wireframe cube for deploy_posix.sh's live ticker.
#
# Run it on its own to watch it spin:
#
#   sh core/release/ui_cube.sh              # until Ctrl-C
#   sh core/release/ui_cube.sh -s 10        # ten seconds, then stop
#   sh core/release/ui_cube.sh -w 40 -h 17 -f 15
#
# Sourced with UI_CUBE_LIB=1 it defines cube_size, cube_term, cube_start and
# cube_stop and runs nothing, so ui.sh can drive it in place of the one-line
# ticker in ui_live_start. See the note at the foot of this file for where
# that hook goes and what it would cost.
#
# POSIX sh has no floating point, so the renderer is awk: one long-lived awk
# process draws every frame, fed one line per tick by the shell. That shape is
# deliberate -- it is how the cube costs less than the ticker it would replace.
# ui.sh's ticker forks date, tail, awk and sleep once per frame, four processes
# ten times a second; this reuses those same four for the tick and does the
# drawing inside a process that is already running. Two writers on one terminal
# would tear the block, so awk is the only one: the shell hands it text, awk
# prints all of it.
#
# Eight vertices, rotated about X and Y, divided by depth, joined by twelve
# Bresenham edges into a character buffer with a depth buffer behind it, so the
# near edges overwrite the far ones instead of crossing them, and the ramp
# ".:-=+*#%@" darkens each cell with distance. Every frame ends by putting the
# cursor back on the block's first row, so the next frame overwrites it in
# place -- no clear, no flicker -- and whoever wants to print a line that lasts
# erases from there down with ESC [ J.
#
# Input protocol, one line per frame:  EPOCH_SECONDS <TAB> ACTIVITY_TEXT
# Everything that only changes at a stage boundary (label, step, total) is
# passed with -v, because the ticker is restarted at every boundary anyway.
# ---------------------------------------------------------------------------

CUBE_W=0
CUBE_H=0
CUBE_FPS=12
CUBE_SECS=0
CUBE_LABEL="virtual machine (obr)"
CUBE_PID=

# ---------------------------------------------------------------------------
# The renderer.
#
# Bit tests are written as arithmetic: and() is a gawk extension and macOS
# ships the one-true awk, which does not have it. Locals are declared as
# trailing parameters, which is the only way awk gives a function locals.
# ---------------------------------------------------------------------------
CUBE_AWK='
function project(   i, x, y, z, x1, y1, z1, z2, d, k, ca, sa, cb, sb) {
	ca = cos(AX); sa = sin(AX); cb = cos(AY); sb = sin(AY)
	for (i = 0; i < 8; i++) {
		x = (i % 2) ? 1 : -1
		y = (int(i / 2) % 2) ? 1 : -1
		z = (int(i / 4) % 2) ? 1 : -1
		x1 = x * cb + z * sb            # yaw
		z1 = z * cb - x * sb
		y1 = y * ca - z1 * sa           # pitch
		z2 = z1 * ca + y * sa
		d = DIST + z2                   # always positive: DIST > REACH
		k = 1 / d
		PX[i] = CX + x1 * k * SX
		PY[i] = CY - y1 * k * SY
		PD[i] = d
	}
}

# Bresenham between two projected corners, interpolating depth along the way.
# A cell is written only when this edge is nearer than what is already there,
# which is what makes the front of the cube look solid instead of a tangle.
function edge(a, b,   x0, y0, xe, ye, d0, d1, dx, dy, ix, iy, err, e2, steps, s, d, g, idx) {
	x0 = int(PX[a] + 0.5); y0 = int(PY[a] + 0.5); d0 = PD[a]
	xe = int(PX[b] + 0.5); ye = int(PY[b] + 0.5); d1 = PD[b]
	dx = xe - x0; if (dx < 0) dx = -dx
	dy = ye - y0; if (dy < 0) dy = -dy
	dy = -dy
	ix = (x0 < xe) ? 1 : -1
	iy = (y0 < ye) ? 1 : -1
	err = dx + dy
	steps = (dx > -dy) ? dx : -dy
	if (steps < 1) steps = 1
	s = 0
	while (1) {
		if (x0 >= 0 && x0 < W && y0 >= 0 && y0 < H) {
			idx = y0 * W + x0
			d = d0 + (d1 - d0) * (s / steps)
			if (d < ZB[idx]) {
				ZB[idx] = d
				g = int(((NEAR + SPAN) - d) / SPAN * (RAMPN - 1)) + 1
				if (g < 1) g = 1; else if (g > RAMPN) g = RAMPN
				BUF[idx] = substr(RAMP, g, 1)
			}
		}
		if (x0 == xe && y0 == ye) break
		e2 = 2 * err
		if (e2 >= dy) { err += dy; x0 += ix }
		if (e2 <= dx) { err += dx; y0 += iy }
		s++
	}
}

# Pad to COLS, or cut to it. A line as wide as the terminal wraps, and the
# block would walk down the screen a row per frame.
function fit(s,   n) {
	n = length(s)
	if (n > COLS) return substr(s, 1, COLS)
	return s substr(BLANKS, 1, COLS - n)
}

function frame(el, act,   i, e, r, c, line, out, bar, n) {
	for (i = 0; i < W * H; i++) { BUF[i] = " "; ZB[i] = 1e30 }
	project()
	for (e = 1; e <= NE; e++) edge(EA[e], EB[e])

	out = ""
	for (r = 0; r < H; r++) {
		line = "  "
		for (c = 0; c < W; c++) line = line BUF[r * W + c]
		if (r in SIDE) line = line "   " SIDE[r]
		out = out fit(line) "\n"
	}
	# The footer: one row under the cube, and the only row written without a
	# trailing newline. That is what stops the screen scrolling a row a frame
	# once the block reaches the bottom.
	n = int((STEP - 1) * 20 / TOTAL)
	bar = substr(FILLS, 1, n) substr(DOTS, 1, 20 - n)
	line = sprintf("  %s %2d/%d  %s  %d:%02d  %s", bar, STEP, TOTAL, LABEL, int(el / 60), el % 60, act)
	out = out fit(line)
	# Leave the cursor on the first row of the block. The next frame overwrites
	# it in place, and anything that must print a lasting line erases from
	# there down.
	printf "%s\r\033[%dA", out, H
	fflush()
}

BEGIN {
	FS = "\t"
	RAMP = ".:-=+*#%@"                    # far to near
	RAMPN = length(RAMP)
	REACH = 1.7320508075688772            # sqrt(3): half the cube diagonal
	DIST = 3.6                            # eye distance
	NEAR = DIST - REACH
	SPAN = 2 * REACH

	# The furthest a corner can project from the centre over every pose: a
	# corner is a vector of length REACH, and maximising y/(DIST+z) subject to
	# y^2+z^2 = REACH^2 gives REACH/sqrt(DIST^2-REACH^2). Scaling the box by
	# that is the scale at which the widest pose still just fits. (REACH/(DIST
	# -REACH) is the obvious guess and is far too big: it asks for a corner
	# both fully sideways and fully forward at once, which no rotation can do,
	# and leaves the cube filling about half its box.) A terminal cell is about
	# twice as tall as it is wide, hence SX = 2 * SY -- without that the cube
	# reads as a slab.
	LIMIT = REACH / sqrt(DIST * DIST - REACH * REACH)
	SY = (H / 2 - 0.6) / LIMIT
	if ((W / 2 - 1.0) / LIMIT / 2 < SY) SY = (W / 2 - 1.0) / LIMIT / 2
	SX = 2 * SY
	CX = W / 2
	CY = H / 2

	# Vertex i takes its sign on each axis from a bit of i, so an edge joins i
	# to i with one bit set: the three neighbours of every corner, once each.
	NE = 0
	for (i = 0; i < 8; i++) {
		if (i % 2 == 0)          { NE++; EA[NE] = i; EB[NE] = i + 1 }
		if (int(i / 2) % 2 == 0) { NE++; EA[NE] = i; EB[NE] = i + 2 }
		if (int(i / 4) % 2 == 0) { NE++; EA[NE] = i; EB[NE] = i + 4 }
	}

	BLANKS = ""; FILLS = ""; DOTS = ""
	while (length(BLANKS) < COLS) BLANKS = BLANKS "                    "
	while (length(FILLS) < 20) { FILLS = FILLS "####"; DOTS = DOTS "...." }

	AX = 0.55                             # a starting tilt, not face-on
	AY = 0.70
	FRAMES = 0
	SIDE[1] = "Objeck deploy  " TARGET
	SIDE[3] = "stage"
	SIDE[4] = LABEL
}

{
	el = $1 + 0 - T0
	if (el < 0) el = 0
	SIDE[3] = sprintf("stage   %2d/%d", STEP, TOTAL)
	SIDE[6] = sprintf("elapsed   %d:%02d", int(el / 60), el % 60)
	SIDE[8] = sprintf("frames    %d", FRAMES)
	frame(el, $2)
	FRAMES++
	# Rates that do not divide into each other, so the cube never falls back
	# into the same pose and the spin has no visible period.
	AY += 0.1100
	AX += 0.0472
	if (AY > 6.2831853) AY -= 6.2831853
	if (AX > 6.2831853) AX -= 6.2831853
}

END {
	# The tick loop closed, so erase the block and leave the cursor on its
	# first row: whatever prints next -- a finished stage, an error -- starts
	# there, on a clean screen.
	printf "\033[J"
	fflush()
}
'

# ---------------------------------------------------------------------------
# library
# ---------------------------------------------------------------------------

# Echo "W H" for a terminal with room, or fail. Below this the cube is more in
# the way than the one progress line it sits over: the banner is 9 rows, the
# footer 1, and a few finished stages have to stay visible.
cube_size() {  # $1 = columns, $2 = rows
	[ "${1:-0}" -ge 60 ] && [ "${2:-0}" -ge 26 ] || return 1
	_h=$(( $2 - 11 ))
	[ "$_h" -gt 15 ] && _h=15
	[ "$_h" -ge 9 ] || return 1
	_w=$(( 2 * _h + 4 ))
	_max=$(( $1 - 34 ))
	[ "$_w" -gt "$_max" ] && _w=$_max
	[ "$_w" -ge 20 ] || return 1
	echo "$_w $_h"
}

# Echo "COLS ROWS". stty on /dev/tty is what ui.sh already uses; tput is the
# fallback for a terminal that does not answer, and 100x30 for one that answers
# with nonsense.
cube_term() {
	_s=$(stty size </dev/tty 2>/dev/null)
	_r=${_s%% *}
	_c=${_s##* }
	case "$_c" in ''|*[!0-9]*) _c=$(tput cols 2>/dev/null) ;; esac
	case "$_r" in ''|*[!0-9]*) _r=$(tput lines 2>/dev/null) ;; esac
	case "$_c" in ''|*[!0-9]*|0) _c=100 ;; esac
	case "$_r" in ''|*[!0-9]*|0) _r=30 ;; esac
	echo "$_c $_r"
}

# Start the cube in the background and set CUBE_PID. The caller redirects the
# whole pipeline to whichever fd owns the terminal -- fd 9 under ui.sh.
#
#   cube_start LABEL STEP TOTAL TARGET COLS W H T0 LOG
#
# The tick loop is the same work ui.sh's ticker already does once a frame; it
# just hands the result to awk instead of printing it.
cube_start() {
	_delay=$(awk -v f="$CUBE_FPS" 'BEGIN { printf "%.2f", 1 / f }')
	_aw=$(( $5 - 50 ))
	_log=$9
	(
		trap 'exit 0' TERM
		while :; do
			_act=
			if [ "$_aw" -gt 12 ] && [ -n "$_log" ]; then
				_act=$(tail -n 5 "$_log" 2>/dev/null | awk -v w="$_aw" '
					{ gsub(/\r/, ""); gsub(/\t/, " ") }
					NF { l = $0 }
					END { sub(/^ +/, "", l); if (length(l) > w) l = substr(l, 1, w - 3) "..."; print l }')
			fi
			printf '%s\t%s\n' "$(date +%s)" "$_act"
			sleep "$_delay"
		done
	) | awk -v W="$6" -v H="$7" -v COLS="$5" -v T0="$8" \
		-v LABEL="$1" -v STEP="$2" -v TOTAL="$3" -v TARGET="$4" "$CUBE_AWK" &
	CUBE_PID=$!
}

# Stop it and erase the block, leaving the cursor where the block began.
# Killing the tick loop closes awk's stdin, so its END rule does the erasing;
# the printf is the belt to that braces, for an awk killed with it.
cube_stop() {
	[ -n "$CUBE_PID" ] || return 0
	kill "$CUBE_PID" 2>/dev/null
	wait "$CUBE_PID" 2>/dev/null
	CUBE_PID=
	printf '\033[J'
}

if [ "${UI_CUBE_LIB:-0}" = "1" ]; then
	return 0 2>/dev/null || exit 0
fi

# ---------------------------------------------------------------------------
# standalone
# ---------------------------------------------------------------------------
while getopts 'w:h:f:s:l:' _o 2>/dev/null; do
	case "$_o" in
		w) CUBE_W=$OPTARG ;;
		h) CUBE_H=$OPTARG ;;
		f) CUBE_FPS=$OPTARG ;;
		s) CUBE_SECS=$OPTARG ;;
		l) CUBE_LABEL=$OPTARG ;;
		*) echo "usage: ui_cube.sh [-w cols] [-h rows] [-f fps] [-s seconds] [-l label]" >&2; exit 2 ;;
	esac
done

# The same gate ui.sh uses. Redirected output, a dumb terminal, any CI leg:
# words, not frames. A log full of escape codes is worse than no animation.
if [ ! -t 1 ] || [ "${TERM:-dumb}" = "dumb" ]; then
	echo "ui_cube.sh: stdout is not a terminal, so nothing is animated."
	echo "Run it at a terminal to see the cube."
	exit 0
fi

set -- $(cube_term)
_cols=$1
_rows=$2
if [ "$CUBE_W" -le 0 ] || [ "$CUBE_H" -le 0 ]; then
	_fit=$(cube_size "$_cols" "$_rows") || {
		echo "ui_cube.sh: this terminal is ${_cols}x${_rows}; the cube wants at least 60x26."
		echo "Pass -w and -h to force a size."
		exit 0
	}
	set -- $_fit
	[ "$CUBE_W" -le 0 ] && CUBE_W=$1
	[ "$CUBE_H" -le 0 ] && CUBE_H=$2
fi

_usable=$(( _cols - 1 ))
_delay=$(awk -v f="$CUBE_FPS" 'BEGIN { printf "%.2f", 1 / f }')
_t0=$(date +%s)
_deadline=0
[ "$CUBE_SECS" -gt 0 ] && _deadline=$(( _t0 + CUBE_SECS ))

printf '\n  ui_cube.sh   %sx%s box, %s fps, Ctrl-C to stop\n\n' "$CUBE_W" "$CUBE_H" "$CUBE_FPS"
printf '\033[?25l'
trap 'printf "\033[J\033[?25h\n"; exit 130' INT
trap 'printf "\033[J\033[?25h\n"; exit 143' TERM

(
	_i=0
	while :; do
		_now=$(date +%s)
		[ "$_deadline" -gt 0 ] && [ "$_now" -ge "$_deadline" ] && break
		# Stand-in for the build's latest log line.
		case $(( _i / 12 % 3 )) in
			0) _act="cl : vm.vcxproj -> obr.exe" ;;
			1) _act="building  libobjk_sdl" ;;
			*) _act="generating code" ;;
		esac
		printf '%s\t%s\n' "$_now" "$_act"
		_i=$(( _i + 1 ))
		sleep "$_delay"
	done
) | awk -v W="$CUBE_W" -v H="$CUBE_H" -v COLS="$_usable" -v T0="$_t0" \
	-v LABEL="$CUBE_LABEL" -v STEP=3 -v TOTAL=19 -v TARGET="linux-x64" "$CUBE_AWK"
_rc=$?

printf '\033[J\033[?25h\n'
exit $_rc

# ---------------------------------------------------------------------------
# Where this would hook into ui.sh, if it ever should:
#
#   ui_live_start   run cube_start instead of the inline ticker subshell, when
#                   UI_CUBE is on and cube_size accepts the terminal
#   ui_live_stop    cube_stop instead of "printf '\r\033[2K'"
#
# Both already exist as the only two places ui.sh touches the live region, so
# the wiring is small. What is not small: the footer row above duplicates the
# bar, counter, label, clock and activity that ui_live_start composes today,
# because one terminal cannot have two writers without tearing and the cube's
# awk has to be the one. That is presentation logic living in two files, in a
# file that also serves every CI leg -- which is why this is a prototype and
# ui.sh is untouched. See the report accompanying this change.
# ---------------------------------------------------------------------------
