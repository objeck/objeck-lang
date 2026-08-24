#!/bin/bash
#
# Creates a macOS .pkg installer for Objeck
#
# Usage: ./create_macos_pkg.sh <version> [sign_identity] [notarize]
#   version       - e.g. "2026.2.1"
#   sign_identity - Developer ID Installer cert name (optional)
#   notarize      - pass "notarize" to submit for notarization (optional)
#
# Requires: pkgbuild, productbuild, productsign (Xcode CLI tools)
#

set -e

VERSION="${1:?Usage: $0 <version> [sign_identity] [notarize]}"
SIGN_IDENTITY="$2"
NOTARIZE="$3"

INSTALL_PREFIX="/usr/local/objeck-lang"
PKG_ID="org.objeck.lang"
PKG_NAME="objeck-macos-arm64_${VERSION}.pkg"

DEPLOY_DIR="$(pwd)/deploy"
STAGING_DIR="$(mktemp -d)/objeck-pkg-root"
SCRIPTS_DIR="$(mktemp -d)/objeck-pkg-scripts"
DIST_DIR="$(mktemp -d)/objeck-pkg-dist"

mkdir -p "$STAGING_DIR" "$SCRIPTS_DIR" "$DIST_DIR"

if [ ! -d "$DEPLOY_DIR/bin" ]; then
  echo "Error: deploy/bin not found. Run deploy_macos_arm64.sh first."
  exit 1
fi

echo "=== Creating macOS .pkg installer ==="
echo "Version: $VERSION"
echo "Install path: $INSTALL_PREFIX"

# ============================================
# Stage files into the install hierarchy
# ============================================

# Sign the tree BEFORE staging. Apple will not notarize a package whose
# executables are ad-hoc signed, lack hardened runtime, or lack a secure
# timestamp -- which is why every release so far came back Invalid while the log
# claimed success. Nothing else in the pipeline signs these binaries: the
# workflow imports a Developer ID Application certificate and never calls
# codesign with it, and deploy_macos_arm64.sh falls back to ad-hoc because the
# Xcode projects ask for a "Mac Development" identity the CI keychain has no
# reason to hold.
SIGN_TREE="$(cd "$(dirname "$0")/../.." && pwd)/tools/cicd/sign_macos_tree.sh"
if [ -x "$SIGN_TREE" ]; then
  if [ -n "$SIGN_IDENTITY" ]; then
    # A real signed release: an unsignable tree must stop the build, not sail on
    # to produce a package that cannot be notarized.
    "$SIGN_TREE" "$DEPLOY_DIR" || {
      echo "Error: could not sign the deploy tree; the .pkg could not be notarized." >&2
      exit 1
    }
  else
    # Local unsigned build -- no Developer ID anywhere, and that is fine.
    SKIP_OK=1 "$SIGN_TREE" "$DEPLOY_DIR" || true
  fi
else
  echo "Warning: $SIGN_TREE not found - binaries will not be signed"
fi

echo "Staging files..."
mkdir -p "$STAGING_DIR$INSTALL_PREFIX"
cp -R "$DEPLOY_DIR"/* "$STAGING_DIR$INSTALL_PREFIX/"

# ============================================
# Create postinstall script (PATH setup)
# ============================================

echo "Creating install scripts..."
mkdir -p "$SCRIPTS_DIR"
cat > "$SCRIPTS_DIR/postinstall" << 'POSTINSTALL'
#!/bin/bash

INSTALL_DIR="/usr/local/objeck-lang"

# Create /etc/paths.d entry for PATH
echo "$INSTALL_DIR/bin" > /etc/paths.d/objeck

# Create launchd environment variable for OBJECK_LIB_PATH
cat > /Library/LaunchDaemons/org.objeck.env.plist << PLIST
<?xml version="1.0" encoding="UTF-8"?>
<!DOCTYPE plist PUBLIC "-//Apple//DTD PLIST 1.0//EN" "http://www.apple.com/DTDs/PropertyList-1.0.dtd">
<plist version="1.0">
<dict>
  <key>Label</key>
  <string>org.objeck.env</string>
  <key>ProgramArguments</key>
  <array>
    <string>/bin/launchctl</string>
    <string>setenv</string>
    <string>OBJECK_LIB_PATH</string>
    <string>/usr/local/objeck-lang/lib</string>
  </array>
  <key>RunAtLoad</key>
  <true/>
</dict>
</plist>
PLIST

# Also set for current session via shell profile
PROFILE_LINE='export OBJECK_LIB_PATH=/usr/local/objeck-lang/lib'
SHELLS=("/etc/zshenv" "/etc/profile")
for SHELL_RC in "${SHELLS[@]}"; do
  if [ -f "$SHELL_RC" ]; then
    if ! grep -q "OBJECK_LIB_PATH" "$SHELL_RC" 2>/dev/null; then
      echo "$PROFILE_LINE" >> "$SHELL_RC"
    fi
  fi
done

echo "Objeck installed to $INSTALL_DIR"
echo "Open a new terminal for PATH changes to take effect."
exit 0
POSTINSTALL
chmod +x "$SCRIPTS_DIR/postinstall"

# ============================================
# Create Distribution XML
# ============================================

echo "Creating distribution..."
cat > "$DIST_DIR/distribution.xml" << DISTXML
<?xml version="1.0" encoding="utf-8"?>
<installer-gui-script minSpecVersion="2">
    <title>Objeck Programming Language</title>
    <organization>org.objeck</organization>
    <domains enable_localSystem="true"/>
    <options customize="never" require-scripts="true" rootVolumeOnly="true"/>
    <welcome file="welcome.html" mime-type="text/html"/>
    <license file="license.txt"/>
    <choices-outline>
        <line choice="default">
            <line choice="$PKG_ID"/>
        </line>
    </choices-outline>
    <choice id="default"/>
    <choice id="$PKG_ID" visible="false">
        <pkg-ref id="$PKG_ID"/>
    </choice>
    <pkg-ref id="$PKG_ID" version="$VERSION" onConclusion="none">objeck-component.pkg</pkg-ref>
</installer-gui-script>
DISTXML

# Create welcome HTML
cat > "$DIST_DIR/welcome.html" << WELCOME
<!DOCTYPE html>
<html>
<head><style>body { font-family: -apple-system, Helvetica Neue, sans-serif; padding: 20px; }</style></head>
<body>
<h2>Objeck Programming Language</h2>
<p>Version $VERSION</p>
<p>This installer will install Objeck to <code>/usr/local/objeck-lang</code> and configure your PATH.</p>
<p>After installation, open a new terminal and type:</p>
<pre>obc -src hello.obs
obr hello</pre>
<p>Visit <a href="https://www.objeck.org">objeck.org</a> for documentation and examples.</p>
</body>
</html>
WELCOME

# Copy license
if [ -f "$(pwd)/deploy/LICENSE" ]; then
  cp "$(pwd)/deploy/LICENSE" "$DIST_DIR/license.txt"
else
  cp "$(pwd)/../../LICENSE" "$DIST_DIR/license.txt"
fi

# ============================================
# Build .pkg
# ============================================

COMPONENT_PKG="$DIST_DIR/objeck-component.pkg"
UNSIGNED_PKG="$DIST_DIR/objeck-unsigned.pkg"

echo "Building component package..."
pkgbuild \
  --root "$STAGING_DIR" \
  --identifier "$PKG_ID" \
  --version "$VERSION" \
  --scripts "$SCRIPTS_DIR" \
  --install-location "/" \
  "$COMPONENT_PKG"

echo "Building distribution package..."
productbuild \
  --distribution "$DIST_DIR/distribution.xml" \
  --resources "$DIST_DIR" \
  --package-path "$DIST_DIR" \
  "$UNSIGNED_PKG"

# ============================================
# Sign .pkg (requires Developer ID Installer)
# ============================================

OUTPUT_DIR="${OUTPUT_DIR:-$(pwd)}"

if [ -n "$SIGN_IDENTITY" ]; then
  echo "Signing package with: $SIGN_IDENTITY"
  productsign --sign "$SIGN_IDENTITY" "$UNSIGNED_PKG" "$OUTPUT_DIR/$PKG_NAME"
  echo "Signed: $OUTPUT_DIR/$PKG_NAME"
else
  echo "No signing identity provided - creating unsigned package"
  cp "$UNSIGNED_PKG" "$OUTPUT_DIR/$PKG_NAME"
fi

# ============================================
# Notarize (requires Apple ID + app password)
# ============================================

if [ "$NOTARIZE" = "notarize" ] && [ -n "$APPLE_ID" ] && [ -n "$APPLE_TEAM_ID" ] && [ -n "$APPLE_APP_PASSWORD" ]; then
  echo "Submitting for notarization..."
  # `notarytool submit --wait` exits 0 even when the verdict is Invalid, so the
  # exit code cannot be trusted and `set -e` never fires on a rejection. Read the
  # verdict itself. v2026.8.3 shipped unnotarized precisely because this was not
  # checked: status was Invalid, stapler then failed with "Record not found"
  # (there is no ticket for a rejected submission), the || branch blamed Apple
  # for needing more time, and the script printed "Notarization complete."
  NOTARY_OUT=$(xcrun notarytool submit "$OUTPUT_DIR/$PKG_NAME" \
    --apple-id "$APPLE_ID" \
    --team-id "$APPLE_TEAM_ID" \
    --password "$APPLE_APP_PASSWORD" \
    --wait 2>&1) || true
  echo "$NOTARY_OUT"

  NOTARY_STATUS=$(printf '%s\n' "$NOTARY_OUT" | awk -F': *' '/^ *status:/ {print $2; exit}')
  NOTARY_ID=$(printf '%s\n' "$NOTARY_OUT" | awk -F': *' '/^ *id:/ {print $2; exit}')

  if [ "$NOTARY_STATUS" != "Accepted" ]; then
    echo ""
    echo "ERROR: notarization returned '${NOTARY_STATUS:-no status}' for submission ${NOTARY_ID:-unknown}." >&2
    if [ -n "$NOTARY_ID" ]; then
      echo "Rejection detail:" >&2
      xcrun notarytool log "$NOTARY_ID" \
        --apple-id "$APPLE_ID" \
        --team-id "$APPLE_TEAM_ID" \
        --password "$APPLE_APP_PASSWORD" 2>&1 | head -60 >&2 || true
    fi
    echo "" >&2
    echo "The .pkg is signed but NOT notarized. macOS will warn users who open it." >&2
    echo "Set ALLOW_UNNOTARIZED=1 to ship anyway (it will still be unnotarized)." >&2
    [ "${ALLOW_UNNOTARIZED:-0}" = "1" ] || exit 1
  else
    echo "Notarization accepted (submission $NOTARY_ID)."
    echo "Stapling notarization ticket..."
    # A staple failure after a genuine Accept is usually ticket propagation and
    # is survivable -- the package is notarized either way, Gatekeeper just has
    # to check online instead of offline.
    if xcrun stapler staple "$OUTPUT_DIR/$PKG_NAME"; then
      echo "Notarized and stapled."
    else
      echo "Warning: notarized, but stapling failed - Gatekeeper will verify online."
    fi
  fi
elif [ "$NOTARIZE" = "notarize" ]; then
  echo "Warning: Notarization requested but APPLE_ID/APPLE_TEAM_ID/APPLE_APP_PASSWORD not set - skipping"
fi

# ============================================
# Verify
# ============================================

echo ""
echo "=== Package created ==="
ls -lh "$OUTPUT_DIR/$PKG_NAME"
pkgutil --check-signature "$OUTPUT_DIR/$PKG_NAME" 2>/dev/null || true
echo "Done."
