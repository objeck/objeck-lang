# ---------------------------------------------------------------------------
# ui.sh -- presentation for deploy_posix.sh.
#
# Sourced, not executed, and POSIX sh only: deploy_posix.sh is #!/bin/sh, which
# is dash on the Ubuntu runners (no arrays, no [[ ]], no local, no pipefail).
#
#   ui_banner "linux-x64" "2026.9.1"   once, after UI_TOTAL is set
#   ui_step "label"                    at each stage boundary
#   cmd args... || ui_fail             report a failed step and carry on
#   ui_ok "deploy complete"            names any failed stages instead
#
# Three modes, chosen when this file is sourced:
#   live     stdout is a terminal (the default by hand). Everything the script
#            and its tools print goes to $UI_LOG. The terminal gets the banner,
#            a line per finished stage, and one live line that a background
#            ticker redraws ten times a second -- spinner, bar, stage timer and
#            the log's latest line -- so it keeps moving while make runs. Only
#            this file writes to the terminal (through fd 9), so nothing tears
#            the live line.
#   plain    stdout is not a terminal -- every CI leg. Output streams exactly as
#            it did before this file existed, plus "[ n/N] stage" lines. No
#            escape codes.
#   verbose  VERBOSE=1 or -v at a terminal: streaming output, coloured stage lines.
# ---------------------------------------------------------------------------

UI_MODE=plain
C_RESET=; C_BOLD=; C_DIM=; C_RED=; C_GREEN=; C_YELLOW=; C_BLUE=; C_CYAN=
if [ -t 1 ] && [ "${TERM:-dumb}" != "dumb" ]; then
	UI_MODE=live
	if [ "${VERBOSE:-0}" = "1" ]; then UI_MODE=verbose; fi
	if [ -z "${NO_COLOR:-}" ]; then
		_e=$(printf '\033')
		C_RESET="${_e}[0m"; C_BOLD="${_e}[1m"; C_DIM="${_e}[90m"
		C_RED="${_e}[91m"; C_GREEN="${_e}[92m"; C_YELLOW="${_e}[93m"
		C_BLUE="${_e}[94m"; C_CYAN="${_e}[96m"
	fi
fi

# Block and braille glyphs where the terminal is UTF-8, ASCII everywhere else.
UI_FILL='#'; UI_EMPTY='.'; UI_OK='+'; UI_BAD='x'; UI_SPIN='| / - \'
if [ "$UI_MODE" != "plain" ]; then
	case "${LC_ALL:-${LC_CTYPE:-${LANG:-}}}" in
		*UTF-8*|*utf-8*|*UTF8*|*utf8*)
			UI_FILL='█'; UI_EMPTY='░'; UI_OK='✓'; UI_BAD='✗'
			UI_SPIN='⠋ ⠙ ⠹ ⠸ ⠼ ⠴ ⠦ ⠧ ⠇ ⠏' ;;
	esac
fi

# The Objeck mark turns at the head of the live region and the check appears
# inside it when a stage finishes (ui_mark.sh, shared with ui_mark.ps1 on
# Windows). ui_mark_init leaves UI_MARK_OK at 0 for a pipe, for CI and for a
# non-UTF-8 locale, and everything below then runs as it did before the mark
# existed. It is sourced before the redirect further down, so the "is this a
# terminal" test still sees the real one.
UI_MARK_OK=0
UI_MARK_ROWS=1
_ui_dir=$(dirname "$0")
if [ -r "$_ui_dir/ui_mark.sh" ]; then
	. "$_ui_dir/ui_mark.sh"
	ui_mark_init
fi
[ "$UI_MARK_OK" = "1" ] || UI_MARK_ROWS=1

UI_PID=$$
UI_STEP=0
UI_TOTAL=1
UI_LABEL=setup
UI_DONE=0
UI_FAILED=0
UI_FAILED_LABELS=
UI_STAGE_FAILED=0
UI_STAGE_LINE=0
UI_LIVE_PID=
UI_START=$(date +%s)
UI_STAGE_START=$UI_START
UI_LOG="${TMPDIR:-/tmp}/objeck-deploy.log"

if [ "$UI_MODE" = "live" ]; then
	: > "$UI_LOG"
	exec 9>&1 >>"$UI_LOG" 2>&1
	printf '\033[?25l' >&9
	trap '_ui_rc=$?; ui_on_exit' EXIT
	trap 'exit 130' INT
	trap 'exit 143' TERM
fi

ui_out() {
	if [ "$UI_MODE" = "live" ]; then printf "$@" >&9; else printf "$@"; fi
}

ui_dur() {  # $1 = start, epoch seconds
	_d=$(( $(date +%s) - $1 ))
	printf '%dm %02ds' $((_d / 60)) $((_d % 60))
}

ui_bar() {  # $1 = filled cells, $2 = width
	_bar=''
	_i=0
	while [ "$_i" -lt "$1" ]; do _bar="$_bar$UI_FILL"; _i=$((_i + 1)); done
	while [ "$_i" -lt "$2" ]; do _bar="$_bar$UI_EMPTY"; _i=$((_i + 1)); done
	printf '%s' "$_bar"
}

# The banner art. A function, not a heredoc inside $(...): bash 3.2 (macOS's
# /bin/bash) counts the art's unbalanced parentheses and ends the substitution
# early, so the file did not parse there.
ui_art() {
	cat <<'EOF'
  ___   _        _              _
 / _ \ | |__    (_)  ___   ___ | | __
| | | || '_ \   | | / _ \ / __|| |/ /
| |_| || |_) |  | ||  __/| (__ |   <
 \___/ |_.__/  _/ | \___| \___||_|\_\
              |__/
EOF
}

ui_banner() {  # $1 = target, $2 = version
	if [ "$UI_MODE" = "plain" ]; then
		echo "=== Objeck $2 deploy: $1, $UI_TOTAL stages ==="
		return 0
	fi
	ui_out '\n%s%s%s\n\n' "$C_CYAN" "$(ui_art)" "$C_RESET"
	ui_out '  %sObjeck %s%s  %s%s, %d stages%s\n' "$C_BOLD" "$2" "$C_RESET" "$C_DIM" "$1" "$UI_TOTAL" "$C_RESET"
	if [ "$UI_MODE" = "live" ]; then
		ui_out '  %stool output: %s%s\n' "$C_DIM" "$UI_LOG" "$C_RESET"
		ui_out '  %spass -v to stream it instead%s\n' "$C_DIM" "$C_RESET"
	fi
	ui_out '\n'
}

ui_step() {
	ui_live_stop
	ui_stage_done
	UI_STEP=$((UI_STEP + 1))
	UI_LABEL="$1"
	UI_STAGE_START=$(date +%s)
	UI_STAGE_FAILED=0
	if [ "$UI_MODE" = "live" ]; then UI_STAGE_LINE=$(wc -l < "$UI_LOG"); fi
	case "$UI_MODE" in
		plain)
			printf '\n[%2d/%d] %s  (%s)\n' "$UI_STEP" "$UI_TOTAL" "$1" "$(ui_dur "$UI_START")" ;;
		verbose)
			printf '\n  %s==>%s %s[%2d/%d] %s%s  %s%s%s\n' "$C_BLUE" "$C_RESET" "$C_BOLD" \
				"$UI_STEP" "$UI_TOTAL" "$1" "$C_RESET" "$C_DIM" "$(ui_dur "$UI_START")" "$C_RESET" ;;
		live)
			ui_live_start ;;
	esac
}

# The line for the stage that just finished, with its own duration.
ui_stage_done() {
	[ "$UI_MODE" = "live" ] || return 0
	[ "$UI_STEP" -gt 0 ] || return 0
	[ "$UI_STAGE_FAILED" = "0" ] || return 0
	ui_out '  %s%s%s %-32s %s%s%s\n' "$C_GREEN" "$UI_OK" "$C_RESET" "$UI_LABEL" \
		"$C_DIM" "$(ui_dur "$UI_STAGE_START")" "$C_RESET"
}

ui_live_start() {
	_cols=$(stty size </dev/tty 2>/dev/null | cut -d' ' -f2)
	case "$_cols" in ''|*[!0-9]*|0) _cols=100 ;; esac
	_done=$((UI_STEP - 1))
	_lbar=$(ui_bar $((_done * 20 / UI_TOTAL)) 20)
	_lpct=$((_done * 100 / UI_TOTAL))
	# A ticker is started once per stage, so "the first frames of this ticker" is
	# the same thing as "a stage just finished" -- as long as one did, which is
	# why the first stage is excluded. Nothing has to cross into the subshell.
	_beat=0
	[ "$UI_STEP" -gt 1 ] && _beat=7
	if [ "$UI_MARK_OK" = "1" ]; then
		_mbar=$(ui_mark_bar "$_done" "$UI_TOTAL" 20)
		_aw=$((_cols - UI_MARK_CELLS - 5))
	else
		# "  F BAR PCT  NN/NN  LABEL  M:SS  ": 2+1+1+20+5+2+5+2+label+2+5+2
		_aw=$((_cols - 47 - ${#UI_LABEL}))
	fi
	(
		trap 'exit 0' TERM
		set -- $UI_SPIN
		_n=$#
		_t=0
		while kill -0 "$UI_PID" 2>/dev/null; do
			_i=$((_t % _n + 1))
			eval "_f=\${$_i}"
			_el=$(( $(date +%s) - UI_STAGE_START ))
			_act=
			if [ "$_aw" -gt 12 ]; then
				_act=$(tail -n 5 "$UI_LOG" 2>/dev/null | awk -v w="$_aw" '
					{ gsub(/\r/, ""); gsub(/\t/, " ") }
					NF { l = $0 }
					END { sub(/^ +/, "", l); if (length(l) > w) l = substr(l, 1, w - 3) "..."; print l }')
			fi
			if [ "$UI_MARK_OK" = "1" ]; then
				_b=0
				[ "$_t" -lt "$_beat" ] && _b=1
				# Three rows: stage and clock, then the bar and counter level with
				# the widest part of the ring, then the build's latest output. Each
				# row is cleared before it is written and the cursor is walked back
				# to the top of the block, so it repaints in place.
				# The clock sits next to the label, not out at the window edge: on a wide
				# terminal that put three feet of nothing between them.
				printf '\033[2K  %s   %s%-24s%s %s%d:%02d%s%*s\n' \
					"$(ui_mark_row "$_t" "$_b" 0)" "$C_BOLD" "$UI_LABEL" "$C_RESET" \
					"$C_DIM" $((_el / 60)) $((_el % 60)) "$C_RESET" $((_aw - 31)) "" >&9
				printf '\033[2K  %s   %s  %s%2d/%d%s\n' \
					"$(ui_mark_row "$_t" "$_b" 1)" "$_mbar" "$C_DIM" "$UI_STEP" "$UI_TOTAL" "$C_RESET" >&9
				printf '\033[2K  %s   %s%s%s\r\033[2A' \
					"$(ui_mark_row "$_t" "$_b" 2)" "$C_DIM" "$_act" "$C_RESET" >&9
			else
				printf '\r\033[2K  %s%s%s %s%s%s %3d%%  %s%2d/%d%s  %s%s%s  %s%d:%02d%s  %s%s%s' \
					"$C_CYAN" "$_f" "$C_RESET" "$C_BLUE" "$_lbar" "$C_RESET" "$_lpct" \
					"$C_DIM" "$UI_STEP" "$UI_TOTAL" "$C_RESET" "$C_BOLD" "$UI_LABEL" "$C_RESET" \
					"$C_DIM" $((_el / 60)) $((_el % 60)) "$C_RESET" "$C_DIM" "$_act" "$C_RESET" >&9
			fi
			_t=$((_t + 1))
			sleep 0.1
		done
	) &
	UI_LIVE_PID=$!
}

ui_live_stop() {
	[ -n "$UI_LIVE_PID" ] || return 0
	kill "$UI_LIVE_PID" 2>/dev/null
	wait "$UI_LIVE_PID" 2>/dev/null
	UI_LIVE_PID=
	# Wipe the whole region and leave the cursor at the top of it, so the line
	# that follows -- a finished stage, a warning, a build error -- lands there
	# and stays. This is why a failure gets the screen to itself.
	if [ "$UI_MARK_OK" = "1" ]; then
		printf '\r\033[2K\n\033[2K\n\033[2K\r\033[2A' >&9
	else
		printf '\r\033[2K' >&9
	fi
}

ui_warn() {
	ui_live_stop
	ui_out '    %s! %s%s\n' "$C_YELLOW" "$1" "$C_RESET"
	if [ "$UI_MODE" = "live" ] && [ "$UI_STEP" -gt 0 ]; then ui_live_start; fi
	return 0
}

# ui_fail -- used as "cmd || ui_fail". $? on entry is cmd's exit status.
#
# Reports and CONTINUES, because the script it wraps never stopped on a failed
# build: the native-library check after the builds decides whether the tree is
# shippable, and exits 1 if it is not. Stopping here would change that
# contract, not just its presentation. ui_ok names every failed stage instead.
ui_fail() {
	_rc=$?
	UI_FAILED=$((UI_FAILED + 1))
	UI_FAILED_LABELS="${UI_FAILED_LABELS:+$UI_FAILED_LABELS, }$UI_LABEL"
	if [ "$UI_MODE" = "live" ]; then
		ui_live_stop
		if [ "$UI_STAGE_FAILED" = "0" ]; then
			ui_out '  %s%s %-32s%s %s%s, exit %s%s\n' "$C_RED" "$UI_BAD" "$UI_LABEL" "$C_RESET" \
				"$C_DIM" "$(ui_dur "$UI_STAGE_START")" "$_rc" "$C_RESET"
		fi
		UI_STAGE_FAILED=1
		tail -n +$((UI_STAGE_LINE + 1)) "$UI_LOG" | tail -n 15 | sed 's/^/      /' >&9
		ui_live_start
	else
		printf '\n  %sX  FAILED at stage %d/%d: %s  (exit %s, after %s)%s\n' "$C_RED" \
			"$UI_STEP" "$UI_TOTAL" "$UI_LABEL" "$_rc" "$(ui_dur "$UI_START")" "$C_RESET"
	fi
	return 0
}

ui_ok() {
	ui_live_stop
	ui_stage_done
	UI_DONE=1
	_total=$(ui_dur "$UI_START")
	if [ "$UI_MODE" = "plain" ]; then
		if [ "$UI_FAILED" -gt 0 ]; then
			printf '\n=== finished with %d failed stage(s): %s (%d stages in %s) ===\n' \
				"$UI_FAILED" "$UI_FAILED_LABELS" "$UI_STEP" "$_total"
		else
			printf '\n=== %s: %d stages in %s ===\n' "$1" "$UI_STEP" "$_total"
		fi
		return 0
	fi
	if [ "$UI_FAILED" -gt 0 ]; then
		ui_out '\n  %s! finished with %d failed stage(s): %s%s  %s%d stages in %s%s\n' \
			"$C_YELLOW" "$UI_FAILED" "$UI_FAILED_LABELS" "$C_RESET" "$C_DIM" "$UI_STEP" "$_total" "$C_RESET"
	else
		if [ "$UI_MARK_OK" = "1" ]; then
			ui_out '  %s\n' "$(ui_mark_bar "$UI_TOTAL" "$UI_TOTAL" 20)"
		else
			ui_out '  %s%s%s 100%%\n' "$C_GREEN" "$(ui_bar 20 20)" "$C_RESET"
		fi
		ui_out '\n'
		# The mark, settled, on the line that says the build worked.
		_mk=
		[ "$UI_MARK_OK" = "1" ] && _mk="$(ui_mark_row 0 1 1)  "
		ui_out '  %s%s%s %s%s  %s%d stages in %s%s\n' \
			"$_mk" "$C_GREEN" "$UI_OK" "$1" "$C_RESET" "$C_DIM" "$UI_STEP" "$_total" "$C_RESET"
	fi
	if [ "$UI_MODE" = "live" ]; then
		ui_out '  %stool output: %s%s\n' "$C_DIM" "$UI_LOG" "$C_RESET"
	fi
	return 0
}

# live mode only: stop the ticker, explain an early exit, give the cursor back.
ui_on_exit() {
	ui_live_stop
	if [ "$UI_DONE" != "1" ]; then
		if [ "$_ui_rc" = "130" ]; then _why="interrupted"; else _why="stopped with exit $_ui_rc"; fi
		ui_out '  %s%s %-32s %s%s\n' "$C_RED" "$UI_BAD" "$UI_LABEL" "$_why" "$C_RESET"
		if [ "$_ui_rc" != "130" ]; then
			tail -n +$((UI_STAGE_LINE + 1)) "$UI_LOG" | tail -n 25 | sed 's/^/      /' >&9
		fi
		ui_out '  %sfull log: %s%s\n' "$C_DIM" "$UI_LOG" "$C_RESET"
	fi
	printf '\033[?25h' >&9
}
