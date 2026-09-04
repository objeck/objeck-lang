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

# Ship the uninstaller inside the install tree. The .pkg previously shipped none,
# so removing Objeck meant deleting /usr/local/objeck-lang by hand and leaving the
# PATH entry, the LaunchDaemon and an export line in /etc/zshenv behind.
cp "$(dirname "$0")/macos_uninstall.sh" "$STAGING_DIR$INSTALL_PREFIX/uninstall.sh"
chmod 755 "$STAGING_DIR$INSTALL_PREFIX/uninstall.sh"

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

# OBJECK_LIB_PATH for terminal sessions.
#
# This edit is load-bearing, not belt-and-braces: /etc/paths.d sets PATH only,
# and without OBJECK_LIB_PATH the toolchain falls back to "../lib/" relative to
# the CURRENT WORKING DIRECTORY (see GetLibraryPath in core/shared/sys.h), so
# obc would work only when run from the install's own bin/.
#
# It previously appended only to files that already existed:
#     SHELLS=("/etc/zshenv" "/etc/profile")
#     if [ -f "$SHELL_RC" ]; then ... fi
# Stock macOS ships /etc/zshrc and /etc/zprofile but NOT /etc/zshenv, and zsh --
# the default shell since Catalina -- never reads /etc/profile. So on a default
# install the variable was written where zsh does not look and nowhere it does.
# Create the zsh file rather than skipping it.
#
# The block is delimited so the uninstaller can remove exactly what was added.
# The old single appended line had no marker and nothing ever removed it, which
# left a dangling export pointing at a deleted directory in a SYSTEM file.
OBJECK_BEGIN='# >>> objeck >>>'
OBJECK_END='# <<< objeck <<<'

write_env_block() {
  rc="$1"
  [ -e "$rc" ] || : > "$rc"
  if grep -qF "$OBJECK_BEGIN" "$rc" 2>/dev/null; then
    return 0                      # already managed; upgrade leaves it alone
  fi
  {
    echo "$OBJECK_BEGIN"
    echo "# Added by the Objeck installer. Removed by $INSTALL_DIR/uninstall.sh"
    echo "export OBJECK_LIB_PATH=$INSTALL_DIR/lib"
    echo "$OBJECK_END"
  } >> "$rc"
}

write_env_block /etc/zshenv       # zsh: read by every zsh, login or not
write_env_block /etc/profile      # sh/bash

echo "Objeck installed to $INSTALL_DIR"
echo "Open a new terminal for PATH changes to take effect."
echo "To remove Objeck: sudo $INSTALL_DIR/uninstall.sh"
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
  NOTARIZE_SH="$(cd "$(dirname "$0")/../.." && pwd)/tools/cicd/notarize.sh"
  if [ -x "$NOTARIZE_SH" ]; then
    APPLE_ID="$APPLE_ID" APPLE_TEAM_ID="$APPLE_TEAM_ID" APPLE_APP_PASSWORD="$APPLE_APP_PASSWORD" \
      "$NOTARIZE_SH" "$OUTPUT_DIR/$PKG_NAME" || exit 1
  else
    echo "Error: $NOTARIZE_SH not found" >&2
    exit 1
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
