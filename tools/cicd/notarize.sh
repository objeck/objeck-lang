#!/usr/bin/env bash
#
# notarize.sh <archive>
#
# Submit an archive to Apple's notary service, believe the verdict it returns,
# and staple a ticket when the format supports one.
#
# Requires APPLE_ID, APPLE_TEAM_ID and APPLE_APP_PASSWORD in the environment.
#
# WHY THE VERDICT IS PARSED RATHER THAN TRUSTED TO $?
# ---------------------------------------------------
# `notarytool submit --wait` exits 0 even when the verdict is Invalid. Every
# macOS release before this shipped unnotarized because nothing checked: the
# v2026.8.3 run recorded "status: Invalid", stapler then failed with "Record not
# found" -- there is no ticket for a rejected submission -- and the caller's ||
# branch reported that as Apple needing "more time", after which it printed
# "Notarization complete."
#
# STAPLING
# --------
# A ticket can be stapled to a .pkg, .dmg or .app bundle. It CANNOT be stapled
# to a .zip: the zip is only a transport, and Apple notarizes the Mach-O files
# inside it by their cdhash. Those binaries are then recognised on any machine,
# with Gatekeeper checking the notary service online the first time. So for a
# .zip a failed staple is not a failure, it is the expected outcome.
#
# Exit codes:
#   0  notarized (stapled where that is possible)
#   1  rejected, or credentials missing
#   3  bad usage
set -uo pipefail

ARCHIVE="${1:-}"
[ -n "$ARCHIVE" ] || { echo "usage: notarize.sh <archive>" >&2; exit 3; }
[ -f "$ARCHIVE" ] || { echo "no such file: $ARCHIVE" >&2; exit 3; }

if [ -z "${APPLE_ID:-}" ] || [ -z "${APPLE_TEAM_ID:-}" ] || [ -z "${APPLE_APP_PASSWORD:-}" ]; then
	echo "Notarization credentials not set (APPLE_ID / APPLE_TEAM_ID / APPLE_APP_PASSWORD)."
	echo "Skipping notarization of $(basename "$ARCHIVE")."
	[ "${ALLOW_UNNOTARIZED:-0}" = "1" ] && exit 0
	exit 1
fi

echo "Submitting $(basename "$ARCHIVE") for notarization..."
OUT=$(xcrun notarytool submit "$ARCHIVE" \
	--apple-id "$APPLE_ID" \
	--team-id "$APPLE_TEAM_ID" \
	--password "$APPLE_APP_PASSWORD" \
	--wait 2>&1) || true
echo "$OUT"

STATUS=$(printf '%s\n' "$OUT" | awk -F': *' '/^ *status:/ {print $2; exit}')
SUB_ID=$(printf '%s\n' "$OUT" | awk -F': *' '/^ *id:/ {print $2; exit}')

if [ "$STATUS" != "Accepted" ]; then
	echo ""
	echo "ERROR: notarization returned '${STATUS:-no status}' for submission ${SUB_ID:-unknown}." >&2
	if [ -n "$SUB_ID" ]; then
		echo "Rejection detail:" >&2
		xcrun notarytool log "$SUB_ID" \
			--apple-id "$APPLE_ID" \
			--team-id "$APPLE_TEAM_ID" \
			--password "$APPLE_APP_PASSWORD" 2>&1 | head -60 >&2 || true
	fi
	echo "" >&2
	echo "$(basename "$ARCHIVE") is NOT notarized; macOS will refuse or warn on it." >&2
	echo "Set ALLOW_UNNOTARIZED=1 to continue anyway." >&2
	[ "${ALLOW_UNNOTARIZED:-0}" = "1" ] || exit 1
	exit 0
fi

echo "Notarization accepted (submission $SUB_ID)."

case "$ARCHIVE" in
	*.zip)
		# Nothing to staple to -- see the note above. The binaries inside are
		# notarized by cdhash, which is what makes a downloaded, quarantined
		# archive runnable at all.
		echo "A .zip carries no staple; its contents are notarized by hash."
		;;
	*)
		echo "Stapling notarization ticket..."
		if xcrun stapler staple "$ARCHIVE"; then
			echo "Notarized and stapled."
		else
			# Survivable after a genuine Accept: Gatekeeper falls back to an
			# online check instead of an offline one.
			echo "Warning: notarized, but stapling failed - Gatekeeper will verify online."
		fi
		;;
esac
exit 0
