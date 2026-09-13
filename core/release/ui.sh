# ---------------------------------------------------------------------------
# ui.sh -- presentation for deploy_posix.sh: a banner, one progress line per
# stage, quiet build output, and the log tail when a stage fails.
#
# Sourced, not executed, and POSIX sh only: deploy_posix.sh is #!/bin/sh, which
# is dash on the Ubuntu runners (no arrays, no [[ ]], no local, no pipefail).
#
#   ui_banner "linux-x64" "2026.9.1"   once, after UI_TOTAL is set
#   ui_step "label"                    at each stage boundary
#   ui_q cmd args...                   run a tool, output hidden unless verbose
#   ui_q cmd args... || ui_fail        and report it if it fails
#   ui_ok "deploy complete"            names any failed stages instead
#
# Quiet is for people at a terminal. When stdout is not a terminal -- every CI
# leg -- or with VERBOSE=1 or -v, ui_q runs the command untouched and its output
# streams exactly as it did before this file existed, so a CI failure is read
# from the same log it always was. Colour and the banner are terminal-only for
# the same reason.
# ---------------------------------------------------------------------------

UI_TTY=0
C_RESET=; C_BOLD=; C_DIM=; C_RED=; C_GREEN=; C_YELLOW=; C_BLUE=; C_CYAN=
if [ -t 1 ]; then
	UI_TTY=1
	if [ -z "${NO_COLOR:-}" ] && [ "${TERM:-dumb}" != "dumb" ]; then
		_e=$(printf '\033')
		C_RESET="${_e}[0m"; C_BOLD="${_e}[1m"; C_DIM="${_e}[90m"
		C_RED="${_e}[91m"; C_GREEN="${_e}[92m"; C_YELLOW="${_e}[93m"
		C_BLUE="${_e}[94m"; C_CYAN="${_e}[96m"
	fi
fi

UI_VERBOSE=0
if [ "${VERBOSE:-0}" = "1" ] || [ "$UI_TTY" != "1" ]; then
	UI_VERBOSE=1
fi

# Block glyphs where the terminal is UTF-8, ASCII everywhere else.
UI_FILL='#'; UI_EMPTY='.'; UI_L='['; UI_R=']'
if [ "$UI_TTY" = "1" ]; then
	case "${LC_ALL:-${LC_CTYPE:-${LANG:-}}}" in
		*UTF-8*|*utf-8*|*UTF8*|*utf8*) UI_FILL='█'; UI_EMPTY='░'; UI_L=''; UI_R='' ;;
	esac
fi

UI_STEP=0
UI_TOTAL=1
UI_LABEL=setup
UI_FAILED=0
UI_FAILED_LABELS=
UI_START=$(date +%s)
UI_LOG="${TMPDIR:-/tmp}/objeck-deploy.log"
if [ "$UI_VERBOSE" != "1" ]; then
	: > "$UI_LOG"
fi

ui_elapsed() {
	_el=$(( $(date +%s) - UI_START ))
	printf '%dm %02ds' $((_el / 60)) $((_el % 60))
}

ui_bar() {  # $1 = filled cells of 28
	_bar=''
	_i=0
	while [ "$_i" -lt "$1" ]; do _bar="$_bar$UI_FILL"; _i=$((_i + 1)); done
	while [ "$_i" -lt 28 ]; do _bar="$_bar$UI_EMPTY"; _i=$((_i + 1)); done
	printf '%s%s%s' "$UI_L" "$_bar" "$UI_R"
}

ui_banner() {  # $1 = target, $2 = version
	if [ "$UI_TTY" != "1" ]; then
		echo "=== Objeck $2 deploy: $1, $UI_TOTAL stages ==="
		return 0
	fi
	printf '\n%s' "$C_CYAN"
	cat <<'EOF'
  ___   _        _              _
 / _ \ | |__    (_)  ___   ___ | | __
| | | || '_ \   | | / _ \ / __|| |/ /
| |_| || |_) |  | ||  __/| (__ |   <
 \___/ |_.__/  _/ | \___| \___||_|\_\
              |__/
EOF
	printf '%s\n' "$C_RESET"
	printf '  %sObjeck %s%s  %s%s, %d stages%s\n' \
		"$C_BOLD" "$2" "$C_RESET" "$C_DIM" "$1" "$UI_TOTAL" "$C_RESET"
	if [ "$UI_VERBOSE" != "1" ]; then
		printf '  %stool output: %s%s\n' "$C_DIM" "$UI_LOG" "$C_RESET"
		printf '  %spass -v or set VERBOSE=1 to stream it%s\n' "$C_DIM" "$C_RESET"
	fi
	echo
}

# ui_step "label" -- the bar shows stages already finished, so the first
# stage reads 0% and 100% is printed only by ui_ok.
ui_step() {
	UI_STEP=$((UI_STEP + 1))
	UI_LABEL="$1"
	if [ "$UI_TTY" != "1" ]; then
		printf '\n[%2d/%d] %s  (%s)\n' "$UI_STEP" "$UI_TOTAL" "$1" "$(ui_elapsed)"
		return 0
	fi
	printf '  %s%s%s %3d%%  %s%2d/%d%s  %s%-30s%s %s%s%s\n' \
		"$C_BLUE" "$(ui_bar $(( (UI_STEP - 1) * 28 / UI_TOTAL )))" "$C_RESET" \
		$(( (UI_STEP - 1) * 100 / UI_TOTAL )) \
		"$C_DIM" "$UI_STEP" "$UI_TOTAL" "$C_RESET" \
		"$C_BOLD" "$1" "$C_RESET" \
		"$C_DIM" "$(ui_elapsed)" "$C_RESET"
}

ui_q() {
	if [ "$UI_VERBOSE" = "1" ]; then
		"$@"
	else
		"$@" >>"$UI_LOG" 2>&1
	fi
}

ui_warn() {
	printf '  %s!  %s%s\n' "$C_YELLOW" "$1" "$C_RESET"
}

# ui_fail -- used as "ui_q cmd || ui_fail". $? on entry is cmd's exit status.
#
# Reports and CONTINUES, because the script it wraps always continued. That is
# load-bearing: on green CI the linux-arm64 onnx build fails (ld: cannot find
# -lonnxruntime) and the leg still passes, so stopping here would turn a
# tolerated failure into a red build. ui_ok names every failed stage instead.
ui_fail() {
	_rc=$?
	UI_FAILED=$((UI_FAILED + 1))
	UI_FAILED_LABELS="${UI_FAILED_LABELS:+$UI_FAILED_LABELS, }$UI_LABEL"
	printf '\n  %sX  FAILED at stage %d/%d: %s%s  %s(exit %s, after %s)%s\n' \
		"$C_RED" "$UI_STEP" "$UI_TOTAL" "$UI_LABEL" "$C_RESET" \
		"$C_DIM" "$_rc" "$(ui_elapsed)" "$C_RESET"
	if [ "$UI_VERBOSE" != "1" ] && [ -s "$UI_LOG" ]; then
		printf '  %slast 40 lines of %s:%s\n' "$C_DIM" "$UI_LOG" "$C_RESET"
		tail -n 40 "$UI_LOG" | sed 's/^/    /'
		printf '  %sre-run with -v or VERBOSE=1 to stream every build%s\n\n' "$C_DIM" "$C_RESET"
	fi
	return 0
}

ui_ok() {
	if [ "$UI_FAILED" -gt 0 ]; then
		if [ "$UI_TTY" != "1" ]; then
			printf '\n=== finished with %d failed stage(s): %s (%d stages in %s) ===\n' \
				"$UI_FAILED" "$UI_FAILED_LABELS" "$UI_STEP" "$(ui_elapsed)"
			return 0
		fi
		printf '\n  %s!  finished with %d failed stage(s): %s%s  %s%d stages in %s%s\n' \
			"$C_YELLOW" "$UI_FAILED" "$UI_FAILED_LABELS" "$C_RESET" "$C_DIM" "$UI_STEP" "$(ui_elapsed)" "$C_RESET"
	elif [ "$UI_TTY" != "1" ]; then
		printf '\n=== %s: %d stages in %s ===\n' "$1" "$UI_STEP" "$(ui_elapsed)"
		return 0
	else
		printf '  %s%s%s 100%%\n\n' "$C_GREEN" "$(ui_bar 28)" "$C_RESET"
		printf '  %s*  %s%s  %s%d stages in %s%s\n' \
			"$C_GREEN" "$1" "$C_RESET" "$C_DIM" "$UI_STEP" "$(ui_elapsed)" "$C_RESET"
	fi
	if [ "$UI_VERBOSE" != "1" ]; then
		printf '  %stool output: %s%s\n' "$C_DIM" "$UI_LOG" "$C_RESET"
	fi
}
