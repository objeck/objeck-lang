#!/bin/sh
# Add (or remove) an "Objeck" entry in this user's application menu.
#
#     ./install-desktop.sh              install
#     ./install-desktop.sh --uninstall  remove
#
# Per-user by design: it writes only under $XDG_DATA_HOME (~/.local/share),
# needs no root, and cannot collide with a distribution package. A tarball that
# wrote into /usr/share would be a tarball that needs sudo and leaves files
# behind when the directory is deleted.
#
# Entirely optional. env.sh is the useful part; this is for people who would
# rather click.
set -e

self_dir="$(cd "$(dirname "$0")" && pwd)"
home_dir="$(cd "$self_dir/.." 2>/dev/null && pwd)"
[ -f "$self_dir/objeck.desktop.in" ] || { echo "install-desktop.sh: objeck.desktop.in is missing" >&2; exit 1; }

# In the release tree this file sits at the install root, not a subdirectory, so
# prefer the directory holding bin/ and lib/.
if [ -x "$self_dir/bin/objeck-shell" ]; then
  home_dir="$self_dir"
elif [ ! -x "$home_dir/bin/objeck-shell" ]; then
  echo "install-desktop.sh: cannot find bin/objeck-shell -- run this from the install root" >&2
  exit 1
fi

data_home="${XDG_DATA_HOME:-$HOME/.local/share}"
apps_dir="$data_home/applications"
icon_dir="$data_home/icons/hicolor/128x128/apps"
entry="$apps_dir/objeck.desktop"
icon="$icon_dir/objeck.png"

if [ "${1:-}" = "--uninstall" ]; then
  rm -f "$entry" "$icon"
  echo "removed $entry"
  [ -x "$(command -v update-desktop-database 2>/dev/null)" ] && update-desktop-database "$apps_dir" 2>/dev/null || true
  exit 0
fi

mkdir -p "$apps_dir" "$icon_dir"

# The path is substituted rather than interpolated by a shell, so an install
# directory containing a space or an ampersand survives. sed's delimiter is |
# because the replacement is a path full of slashes.
#
# Backslash and double quote are escaped first, because the Exec line quotes the
# path and the Desktop Entry spec treats both specially inside a quoted
# argument. Backslashes before quotes, or the escaping backslashes would
# themselves be escaped. Rare in a path, and silent and total when it happens.
esc_home=$(printf '%s' "$home_dir" | sed -e 's/\\/\\\\/g' -e 's/"/\\"/g')
# And then escaped AGAIN, for sed's replacement text, where backslash and & are
# themselves special -- without this second pass sed strips the backslashes the
# first pass just added, and the entry ends up exactly as unescaped as it was
# before any of this. The | is escaped because it is the delimiter.
sed_home=$(printf '%s' "$esc_home" | sed -e 's/[\\&|]/\\&/g')
sed "s|@OBJECK_HOME@|$sed_home|g" "$self_dir/objeck.desktop.in" > "$entry"
chmod 644 "$entry"
[ -f "$self_dir/objeck.png" ] && cp "$self_dir/objeck.png" "$icon"

# Validate rather than hope: desktop-file-validate is part of
# desktop-file-utils and is usually present. A malformed entry is simply
# ignored by the menu, with no error anywhere -- exactly the silent failure
# this script would otherwise be prone to.
if command -v desktop-file-validate >/dev/null 2>&1; then
  if ! desktop-file-validate "$entry"; then
    echo "install-desktop.sh: the generated entry does not validate (shown above)" >&2
    rm -f "$entry"
    exit 1
  fi
  echo "validated with desktop-file-validate"
fi

command -v update-desktop-database >/dev/null 2>&1 && update-desktop-database "$apps_dir" 2>/dev/null || true

echo "installed $entry"
echo "  Exec=$home_dir/bin/objeck-shell"
echo "  it may take a moment to appear in the menu; some desktops need a re-login"
