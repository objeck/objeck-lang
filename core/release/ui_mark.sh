# ---------------------------------------------------------------------------
# ui_mark.sh -- the Objeck mark as a build spinner.
#
# The POSIX half of ui_mark.ps1; that file says what the mark is and why. Same
# frames, same order, same blue, because both read one generated table:
#
#   powershell -File ui_mark.ps1 -Generate -MarkRing
#
# Sourced, not executed, and POSIX sh only (dash on the CI runners: no arrays,
# no [[ ]], no local). It prints nothing of its own and owns no cursor. ui.sh's
# live ticker composes and writes the whole block; this hands it the pieces.
#
#   ui_mark_init                    decide once whether the mark can be shown
#   ui_mark_row <tick> <beat> <row> one screen row of the mark
#   ui_mark_bar <done> <total> <n>  a progress bar in the same alphabet
#
# There is no state to advance: a frame is a function of the tick the caller is
# already counting, and a non-zero <beat> asks for the settled frame instead.
# That is what lets ui.sh call it from inside its background ticker subshell,
# which cannot write a variable back to anybody.
# ---------------------------------------------------------------------------

UI_MARK_CELLS=7
UI_MARK_ROWS=3
UI_MARK_R0='⠀⡠⠒⠒⠒⢄⠀ ⠀⡠⠒⠒⠒⠤⡀ ⠀⡔⠒⠒⠢⢄⠀ ⠀⡔⠒⠉⠑⢢⠀ ⠀⡠⠔⠊⠑⢢⠀ ⠀⡠⠒⠉⠒⢄⠀ ⠀⢠⠒⠑⠢⡀⠀ ⠀⢀⠤⠒⠲⡀⠀ ⠀⢀⠔⠒⢢⠀⠀ ⠀⠀⡔⠒⢆⠀⠀ ⠀⠀⡰⠒⢄⠀⠀ ⠀⠀⢰⠲⡀⠀⠀ ⠀⠀⢀⠶⡀⠀⠀ ⠀⠀⠀⣾⠀⠀⠀ ⠀⠀⠀⡇⠀⠀⠀ ⠀⠀⠀⣼⠀⠀⠀ ⠀⠀⠀⡖⡄⠀⠀ ⠀⠀⢰⠒⡄⠀⠀ ⠀⠀⡰⠒⢄⠀⠀ ⠀⠀⡰⠒⢆⠀⠀ ⠀⢀⠔⠒⢢⠀⠀ ⠀⢀⠤⠒⠒⡄⠀ ⠀⢠⠒⠑⠢⡀⠀ ⠀⢠⠒⠑⠢⢄⠀ ⠀⡠⠒⠉⠒⢤⠀ ⠀⡰⠊⠉⠒⢤⠀ ⠀⡔⠒⠒⠢⢄⠀ ⠀⡠⠒⠒⠢⢄⡀'
UI_MARK_R1='⢸⠀⠀⠀⠀⠀⡇ ⢸⠀⠀⠀⠀⠀⡇ ⠘⡄⠀⠀⠀⠘⡄ ⠸⡀⠀⠀⠀⠈⡆ ⠰⡁⠀⠀⠀⢈⠆ ⠰⡁⠀⠀⠀⢈⠆ ⠀⢇⠀⠀⠀⢱⠀ ⠀⢎⠀⠀⠀⡱⠀ ⠀⢸⠀⠀⠀⡇⠀ ⠀⠀⡇⠀⢸⠀⠀ ⠀⠀⡇⠀⢸⠀⠀ ⠀⠀⢸⠀⡇⠀⠀ ⠀⠀⢸⠀⡇⠀⠀ ⠀⠀⠀⣿⠀⠀⠀ ⠀⠀⠀⡇⠀⠀⠀ ⠀⠀⠀⣿⠀⠀⠀ ⠀⠀⢸⠀⡇⠀⠀ ⠀⠀⢸⠀⡇⠀⠀ ⠀⠀⡇⠀⢸⠀⠀ ⠀⠀⡇⠀⢸⠀⠀ ⠀⢸⠀⠀⠀⡇⠀ ⠀⡎⠀⠀⠀⡸⠀ ⠀⢎⠀⠀⠀⡱⠀ ⠠⡃⠀⠀⠀⢨⠂ ⢰⠁⠀⠀⠀⢀⠇ ⢰⠁⠀⠀⠀⢀⠇ ⠸⡀⠀⠀⠀⠈⡆ ⢸⠀⠀⠀⠀⠀⡇'
UI_MARK_R2='⠀⠑⠤⠤⠤⠊⠀ ⠈⠒⠤⠤⠤⠊⠀ ⠀⠑⠢⠤⠤⠜⠀ ⠀⠣⢄⣀⠤⠜⠀ ⠀⠣⢄⡠⠔⠊⠀ ⠀⠑⠤⣀⠤⠊⠀ ⠀⠈⠢⢄⠤⠃⠀ ⠀⠈⠦⠤⠒⠁⠀ ⠀⠀⠣⠤⠔⠁⠀ ⠀⠀⠱⠤⠜⠀⠀ ⠀⠀⠑⠤⠎⠀⠀ ⠀⠀⠈⠦⠇⠀⠀ ⠀⠀⠈⠶⠁⠀⠀ ⠀⠀⠀⡿⠀⠀⠀ ⠀⠀⠀⡇⠀⠀⠀ ⠀⠀⠀⡟⠀⠀⠀ ⠀⠀⠘⠼⠀⠀⠀ ⠀⠀⠘⠤⠇⠀⠀ ⠀⠀⠑⠤⠎⠀⠀ ⠀⠀⠱⠤⠎⠀⠀ ⠀⠀⠣⠤⠔⠁⠀ ⠀⠘⠤⠤⠒⠁⠀ ⠀⠈⠢⢄⠤⠃⠀ ⠀⠑⠢⢄⠤⠃⠀ ⠀⠓⠤⣀⠤⠊⠀ ⠀⠓⠤⣀⡠⠎⠀ ⠀⠑⠢⠤⠤⠜⠀ ⠈⠑⠢⠤⠤⠊⠀'
UI_MARK_D0='⠀⡠⠒⠒⠒⣤⠀'
UI_MARK_D1='⢸⢠⡀⢀⡴⠃⡇'
UI_MARK_D2='⠀⠑⠽⠾⠤⠊⠀'

# The bar is drawn in the mark's alphabet so the block reads as one thing: a
# solid cell for a finished stage, a half cell for the one in progress, and
# the bottom two dots as a track under the rest.
UI_MARK_FULL='⣿'
UI_MARK_HALF='⣇'
UI_MARK_TRACK='⣀'

UI_MARK_OK=0
UI_MARK_ON=
UI_MARK_DIM=
UI_MARK_OFF=

# Braille and 24-bit colour, or nothing at all: there is no half-resolution
# braille, so where the locale or the terminal cannot take it the caller keeps
# the plain spinner it had before. #0998E3 is the logo's blue, sampled from
# docs/web/style/objeck-logo.png -- the whole mark is that one colour.
ui_mark_init() {
	UI_MARK_OK=0
	UI_MARK_ON=
	UI_MARK_DIM=
	UI_MARK_OFF=
	[ -t 1 ] || return 0
	[ -z "${CI:-}${GITHUB_ACTIONS:-}" ] || return 0
	case "${LC_ALL:-${LC_CTYPE:-${LANG:-}}}" in
		*UTF-8*|*utf-8*|*UTF8*|*utf8*) ;;
		*) return 0 ;;
	esac
	UI_MARK_OK=1
	if [ -z "${NO_COLOR:-}" ]; then
		UI_MARK_ON=$(printf '\033[38;2;9;152;227m')
		UI_MARK_DIM=$(printf '\033[38;2;72;84;96m')
		UI_MARK_OFF=$(printf '\033[0m')
	fi
	return 0
}

ui_mark_row() {
	[ "$UI_MARK_OK" = "1" ] || return 0
	_mt=${1:-0}
	_mb=${2:-0}
	_mr=${3:-0}
	if [ "$_mb" != "0" ]; then
		eval "_mf=\$UI_MARK_D$_mr"
		printf '%s%s%s' "$UI_MARK_ON" "$_mf" "$UI_MARK_OFF"
		return 0
	fi
	# "set --" over the space-separated table, which is the only indexing dash
	# has; it clobbers the positional parameters, hence the copies above.
	eval "set -- \$UI_MARK_R$_mr"
	eval "_mf=\${$((_mt % $# + 1))}"
	printf '%s%s%s' "$UI_MARK_ON" "$_mf" "$UI_MARK_OFF"
}

# Half-cell granularity, so a fifteen-stage deploy moves the bar twice a stage.
ui_mark_bar() {
	[ "$UI_MARK_OK" = "1" ] || return 0
	_bd=${1:-0}
	_bt=${2:-1}
	_bn=${3:-20}
	[ "$_bt" -ge 1 ] || _bt=1
	[ "$_bd" -le "$_bt" ] || _bd=$_bt
	[ "$_bd" -ge 0 ] || _bd=0
	_bh=$((_bd * 2 * _bn / _bt))
	_bf=$((_bh / 2))
	printf '%s' "$UI_MARK_ON"
	_i=0
	while [ $_i -lt $_bf ]; do printf '%s' "$UI_MARK_FULL"; _i=$((_i + 1)); done
	if [ $((_bh % 2)) -eq 1 ]; then printf '%s' "$UI_MARK_HALF"; _i=$((_i + 1)); fi
	printf '%s' "$UI_MARK_DIM"
	while [ $_i -lt $_bn ]; do printf '%s' "$UI_MARK_TRACK"; _i=$((_i + 1)); done
	printf '%s' "$UI_MARK_OFF"
}
