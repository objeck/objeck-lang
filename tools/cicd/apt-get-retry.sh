#!/bin/bash
#
# apt-get that retries a network failure. tools/cicd/apt_harden.sh installs this as
# /usr/local/bin/apt-get on a CI runner, ahead of /usr/bin/apt-get, so every
# `sudo apt-get update` and `sudo apt-get install` in a workflow retries without being rewritten.
#
# Why it exists: on 2026-11-11 the GitHub-hosted runners could not resolve
# azure.archive.ubuntu.com (and, on arm64, ports.ubuntu.com) for hours. Every job that installs
# packages failed at "Temporary failure resolving", on PRs that had nothing to do with packages,
# and nothing in the pipeline retried or fell back.
#
# What it retries: update, install, upgrade, dist-upgrade, build-dep and reinstall, and only when
# the output looks like a network problem (a failed lookup or fetch, a timeout, a 5xx, a hash
# mismatch from a half-synced mirror). A real error (a missing package, a broken dependency) is
# not retried: it fails at once, as before. `apt-get update` exits 0 even when index files failed
# to download, and the install after it then fails, so a failed fetch in the output counts as a
# failure of the update too.
#
# Environment (for tests): APT_REAL (the real apt-get), APT_RETRY_TRIES (5), APT_RETRY_DELAY (20 s,
# multiplied by the attempt number).

REAL="${APT_REAL:-/usr/bin/apt-get}"
TRIES="${APT_RETRY_TRIES:-5}"
DELAY="${APT_RETRY_DELAY:-20}"

verb=""
for a in "$@"; do
	case "$a" in
		update|install|upgrade|dist-upgrade|build-dep|reinstall) verb="$a"; break ;;
	esac
done

# anything else (remove, clean, autoremove, -s, ...) runs as it always did
[ -n "$verb" ] || exec "$REAL" "$@"

log="$(mktemp)"
trap 'rm -f "$log"' EXIT

TRANSIENT='Temporary failure|Could not resolve|Failed to fetch|Unable to fetch some archives|Connection (timed out|failed|refused|reset)|Hash Sum mismatch|Bad header line|Service Unavailable|Bad Gateway|Gateway Time-out|Err:[0-9]+ .* (503|502|504)'

attempt=0
while :; do
	attempt=$((attempt + 1))
	"$REAL" "$@" 2>&1 | tee "$log"
	rc=${PIPESTATUS[0]}

	failed=0
	[ "$rc" -ne 0 ] && failed=1
	if [ "$verb" = update ] && grep -qE '^(W|E): Failed to fetch|Temporary failure resolving|Could not resolve' "$log"; then
		failed=1
	fi
	[ "$failed" -eq 0 ] && exit "$rc"

	if ! grep -qE "$TRANSIENT" "$log"; then
		echo "apt-get: '$verb' failed for a reason that is not a network problem; not retrying" >&2
		exit "$rc"
	fi
	if [ "$attempt" -ge "$TRIES" ]; then
		echo "apt-get: '$verb' still failing after $attempt attempts; giving up" >&2
		exit "$rc"
	fi

	wait_s=$((DELAY * attempt))
	echo "apt-get: '$verb' hit a network problem (attempt $attempt of $TRIES); retrying in ${wait_s}s" >&2
	sleep "$wait_s"
done
