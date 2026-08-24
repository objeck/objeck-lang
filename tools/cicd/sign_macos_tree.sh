#!/usr/bin/env bash
#
# sign_macos_tree.sh <deploy-tree> [identity]
#
# Sign every Mach-O in a macOS deploy tree with a Developer ID Application
# identity, hardened runtime and a secure timestamp, so the .pkg built from it
# can actually be notarized.
#
# WHY THIS EXISTS
# ---------------
# Notarization has been failing on every macOS release, and CI reported success.
# From the v2026.8.3 run (submission 64f2f789-6c03-41f3-aa01-319afadf6730):
#
#     Current status: Invalid......Processing complete
#       status: Invalid
#     Stapling notarization ticket...
#     CloudKit query ... failed due to "Record not found".
#     The staple and validate action failed! Error 65.
#     Warning: Staple failed (Apple may need more time) - .pkg is still signed and notarized
#     Notarization complete.
#
# Three things conspired. `notarytool submit --wait` exits 0 even when the
# verdict is Invalid, so `set -e` never fired. `stapler` then failed for the
# only possible reason -- there is no ticket for a rejected submission -- and
# its `||` branch blamed Apple for needing "more time". The script then printed
# "Notarization complete." So the release shipped a signed but UNNOTARIZED .pkg
# while the log said otherwise.
#
# The underlying cause is that nothing ever signed the binaries. The workflow
# imports a Developer ID Application certificate into the CI keychain and never
# calls codesign with it; deploy_macos_arm64.sh looks for a "Mac Development"
# identity, does not find one, and falls back to ad-hoc:
#
#     note: Using codesigning identity override: -
#
# Apple will not notarize a package whose executables are ad-hoc signed, lack
# hardened runtime, or lack a secure timestamp. This script fixes all three.
#
# It is deliberately separate from the Xcode builds: obu is built by make, the
# SDL2 dylibs are vendored and re-signed after install_name_tool rewrites them,
# and AppLauncher is built by swiftc. A sweep over the finished tree is the only
# place that catches every one of them.
#
# Exit codes:
#   0  everything signed and verified (or no identity available and SKIP_OK set)
#   1  signing or verification failed
#   3  bad usage
set -uo pipefail

TREE="${1:-}"
[ -n "$TREE" ] || { echo "usage: sign_macos_tree.sh <deploy-tree> [identity]" >&2; exit 3; }
[ -d "$TREE" ] || { echo "no such tree: $TREE" >&2; exit 3; }

IDENTITY="${2:-}"
if [ -z "$IDENTITY" ]; then
	IDENTITY=$(security find-identity -v -p codesigning 2>/dev/null \
		| grep "Developer ID Application" | head -1 | sed 's/.*"\(.*\)".*/\1/')
fi

if [ -z "$IDENTITY" ]; then
	echo "No 'Developer ID Application' identity found."
	echo "The tree will keep whatever signatures it already has, and the .pkg"
	echo "built from it CANNOT be notarized."
	# A local developer build has no Developer ID and should not be a hard error.
	[ "${SKIP_OK:-0}" = "1" ] && exit 0
	exit 1
fi

echo "Signing $TREE"
echo "  identity: $IDENTITY"

# The VM JITs, so it needs allow-jit; it also dlopens the native libraries,
# which are signed by us but are separate Mach-Os, so it needs
# disable-library-validation. These mirror core/vm/xcode/VM-A64.entitlements.
#
# What must NOT appear here is com.apple.security.get-task-allow. Xcode injects
# that automatically for DEVELOPMENT-signed builds, and the notary service
# rejects any binary carrying it -- it is a debugging entitlement and has no
# place in a distributed build.
ENTITLEMENTS=$(mktemp -t objeck_entitlements).plist
cat > "$ENTITLEMENTS" <<'PLIST'
<?xml version="1.0" encoding="UTF-8"?>
<!DOCTYPE plist PUBLIC "-//Apple//DTD PLIST 1.0//EN" "http://www.apple.com/DTDs/PropertyList-1.0.dtd">
<plist version="1.0">
<dict>
	<key>com.apple.security.cs.allow-jit</key>
	<true/>
	<key>com.apple.security.cs.disable-library-validation</key>
	<true/>
</dict>
</plist>
PLIST
trap 'rm -f "$ENTITLEMENTS"' EXIT

FAILED=0

sign_one() {
	local target="$1"; shift
	if codesign --force --sign "$IDENTITY" --options runtime --timestamp "$@" "$target" 2>&1 \
		| sed 's/^/    /'; then :; fi
	if ! codesign --verify --strict "$target" 2>/dev/null; then
		echo "    FAILED to verify: $target"
		FAILED=1
		return 1
	fi
	return 0
}

# Inside out. A bundle's nested code must be signed before the bundle itself, and
# a dylib before whatever loads it, or the outer signature seals a stale hash.
echo "  libraries..."
find "$TREE/lib" -type f -name "*.dylib" 2>/dev/null | while read -r f; do
	sign_one "$f" >/dev/null 2>&1 && echo "    ok  ${f#$TREE/}" || echo "    ERR ${f#$TREE/}"
done

# obn is a launcher stub that lives under lib/native/misc
if [ -f "$TREE/lib/native/misc/obn" ]; then
	sign_one "$TREE/lib/native/misc/obn" --entitlements "$ENTITLEMENTS" >/dev/null 2>&1 \
		&& echo "    ok  lib/native/misc/obn" || echo "    ERR lib/native/misc/obn"
fi

echo "  executables..."
for exe in "$TREE"/bin/*; do
	[ -f "$exe" ] || continue
	file "$exe" 2>/dev/null | grep -q "Mach-O" || continue
	sign_one "$exe" --entitlements "$ENTITLEMENTS" >/dev/null 2>&1 \
		&& echo "    ok  bin/$(basename "$exe")" || echo "    ERR bin/$(basename "$exe")"
done

if [ -d "$TREE/app/Objeck.app" ]; then
	echo "  app bundle..."
	find "$TREE/app/Objeck.app/Contents/MacOS" -type f 2>/dev/null | while read -r f; do
		sign_one "$f" >/dev/null 2>&1
	done
	sign_one "$TREE/app/Objeck.app" >/dev/null 2>&1 \
		&& echo "    ok  app/Objeck.app" || echo "    ERR app/Objeck.app"
fi

# ---------------------------------------------------------------- verification
# "It ran codesign" is not the same as "the tree can be notarized". Assert the
# three properties the notary service actually checks.
echo "  verifying..."
ADHOC=0; NOTIME=0; NORUNTIME=0; TOTAL=0
while read -r f; do
	TOTAL=$((TOTAL + 1))
	info=$(codesign -dv --verbose=4 "$f" 2>&1)
	printf '%s' "$info" | grep -q "Signature=adhoc" && { echo "    ADHOC    ${f#$TREE/}"; ADHOC=$((ADHOC + 1)); }
	printf '%s' "$info" | grep -q "Timestamp=" || { echo "    NO-TS    ${f#$TREE/}"; NOTIME=$((NOTIME + 1)); }
	printf '%s' "$info" | grep -q "flags=.*runtime" || { echo "    NO-HR    ${f#$TREE/}"; NORUNTIME=$((NORUNTIME + 1)); }
	# get-task-allow is an automatic notarization rejection
	if codesign -d --entitlements - "$f" 2>/dev/null | grep -q "get-task-allow"; then
		echo "    DEBUG-ENT ${f#$TREE/} carries get-task-allow"
		FAILED=1
	fi
done < <(find "$TREE/bin" "$TREE/lib" -type f \( -name "*.dylib" -o -perm -u+x \) 2>/dev/null \
	| while read -r c; do file "$c" 2>/dev/null | grep -q "Mach-O" && echo "$c"; done)

echo ""
echo "  $TOTAL Mach-O files: adhoc=$ADHOC no-timestamp=$NOTIME no-hardened-runtime=$NORUNTIME"

if [ "$ADHOC" -gt 0 ] || [ "$NOTIME" -gt 0 ] || [ "$NORUNTIME" -gt 0 ] || [ "$FAILED" -ne 0 ]; then
	echo ""
	echo "Tree is NOT ready for notarization." >&2
	exit 1
fi

echo "Tree is signed with Developer ID, hardened runtime and secure timestamps."
exit 0
