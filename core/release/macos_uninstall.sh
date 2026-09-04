#!/bin/bash
#
# Objeck uninstaller for macOS.
#
# The .pkg shipped no uninstaller at all, which mattered more than it sounds:
# the installer writes to four places outside its own directory, and one of them
# is an export line appended to SYSTEM shell files. Deleting
# /usr/local/objeck-lang by hand -- the obvious thing to do -- left that line
# behind pointing at a directory that no longer exists, in /etc/zshenv, for
# every user on the machine.
#
# This removes exactly what the postinstall created:
#   /usr/local/objeck-lang            the install itself
#   /etc/paths.d/objeck               PATH entry
#   /Library/LaunchDaemons/org.objeck.env.plist   OBJECK_LIB_PATH for GUI apps
#   the '# >>> objeck >>>' block in /etc/zshenv and /etc/profile
#
# Usage: sudo ./uninstall.sh [--dry-run]

set -u

INSTALL_DIR="/usr/local/objeck-lang"
PATHS_D="/etc/paths.d/objeck"
PLIST="/Library/LaunchDaemons/org.objeck.env.plist"
RC_FILES=("/etc/zshenv" "/etc/profile")
BEGIN='# >>> objeck >>>'
END='# <<< objeck <<<'

DRY_RUN=0
[ "${1:-}" = "--dry-run" ] && DRY_RUN=1

if [ "$DRY_RUN" -eq 0 ] && [ "$(id -u)" -ne 0 ]; then
  echo "This uninstaller must run as root:  sudo $0" >&2
  exit 1
fi

say() { if [ "$DRY_RUN" -eq 1 ]; then echo "would $*"; else echo "$*"; fi; }

# ---- the launchd job, unloaded before its plist is removed ----
if [ -f "$PLIST" ]; then
  say "remove $PLIST"
  if [ "$DRY_RUN" -eq 0 ]; then
    launchctl unload "$PLIST" 2>/dev/null || true
    # The daemon ran `launchctl setenv`; unloading does not unset it, so the
    # variable would survive in the GUI session until the next reboot.
    launchctl unsetenv OBJECK_LIB_PATH 2>/dev/null || true
    rm -f "$PLIST"
  fi
else
  echo "not present: $PLIST"
fi

# ---- PATH entry ----
if [ -f "$PATHS_D" ]; then
  say "remove $PATHS_D"
  [ "$DRY_RUN" -eq 0 ] && rm -f "$PATHS_D"
else
  echo "not present: $PATHS_D"
fi

# ---- the delimited block in system shell files ----
#
# Matched by marker, not by content: a grep for 'OBJECK_LIB_PATH' would also
# match a line the user wrote themselves, and deleting that would be worse than
# leaving ours behind. Installs from before the markers existed are reported
# rather than guessed at.
for rc in "${RC_FILES[@]}"; do
  [ -f "$rc" ] || continue
  if grep -qF "$BEGIN" "$rc" 2>/dev/null; then
    say "remove the objeck block from $rc"
    if [ "$DRY_RUN" -eq 0 ]; then
      tmp="$(mktemp)"
      if awk -v b="$BEGIN" -v e="$END" '
        index($0, b) { skip = 1; next }
        index($0, e) { skip = 0; next }
        !skip        { print }
      ' "$rc" > "$tmp"; then
        # Gate on awk's exit status, NOT on whether the result is empty. An
        # empty result is the COMMON case, not a failure: on stock macOS
        # /etc/zshenv does not exist, the installer creates it, and it then
        # holds nothing but our block -- so "refuse to empty the file" would
        # leave the block behind precisely where it always needs removing.
        # Written through cat so the file keeps its own mode and owner rather
        # than inheriting mktemp's 0600 root.
        cat "$tmp" > "$rc"
      else
        echo "  refused: could not rewrite $rc; leaving it untouched" >&2
      fi
      rm -f "$tmp"
    fi
  elif grep -q "OBJECK_LIB_PATH" "$rc" 2>/dev/null; then
    echo "NOTE: $rc sets OBJECK_LIB_PATH but has no objeck marker block."
    echo "      It predates this uninstaller (or was edited by hand)."
    echo "      Remove the line yourself if you want it gone:"
    grep -n "OBJECK_LIB_PATH" "$rc" | sed 's/^/        /'
  fi
done

# ---- the install itself, last: everything above references it ----
if [ -d "$INSTALL_DIR" ]; then
  say "remove $INSTALL_DIR"
  [ "$DRY_RUN" -eq 0 ] && rm -rf "$INSTALL_DIR"
else
  echo "not present: $INSTALL_DIR"
fi

# ---- forget the package receipt, so a reinstall is clean ----
if [ "$DRY_RUN" -eq 0 ]; then
  pkgutil --forget org.objeck.lang >/dev/null 2>&1 || true
else
  echo "would forget the org.objeck.lang receipt"
fi

echo
if [ "$DRY_RUN" -eq 1 ]; then
  echo "Dry run: nothing was changed."
else
  echo "Objeck removed. Open a new terminal for the PATH change to take effect."
fi
