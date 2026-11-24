#!/bin/sh
# POSIX twin of gen_rc.ps1: render a Windows resource template (objeck.in) into
# its objeck.rc, stamping the version from core/shared/version.h.
#
# The MSYS2 makefiles (core/*/make/Makefile.msys2-*) run windres on the .rc, and
# the .rc files are no longer committed (they are gitignored build outputs; the
# MSBuild projects render them with rc_version.targets + gen_rc.ps1). MSYS2 has
# no reason to need PowerShell, so the makefiles call this script instead. The two
# generators must produce identical bytes; tools/cicd/check_version_templates.py
# checks that.
#
# usage: gen_rc.sh <template> <output> <version.h>
#   @VERSION@ 2026.11.3   @YEAR_END@ 2026   @VERSION_WINDOWS@ 2026,11,3
# Leaves the output untouched when it is already identical (no needless windres run).
set -e
[ $# -eq 3 ] || { echo "usage: gen_rc.sh <template> <output> <version.h>" >&2; exit 2; }
tmpl=$1; out=$2; hdr=$3
ver=$(sed -n 's/^#define[[:space:]][[:space:]]*VERSION_STRING[[:space:]][[:space:]]*L"\([0-9][0-9]*\.[0-9][0-9]*\.[0-9][0-9]*\)".*/\1/p' "$hdr" | head -n 1 | tr -d '\r')
[ -n "$ver" ] || { echo "gen_rc.sh: no VERSION_STRING of the form L\"Y.M.R\" in $hdr" >&2; exit 1; }
year=${ver%%.*}
verwin=$(echo "$ver" | tr . ,)
tmp="$out.tmp.$$"
sed -e 's/\r$//' -e "s/@VERSION_WINDOWS@/$verwin/g" -e "s/@VERSION@/$ver/g" -e "s/@YEAR_END@/$year/g" "$tmpl" > "$tmp"
# gen_rc.ps1 always ends the file with a newline; the templates may not.
[ -z "$(tail -c 1 "$tmp")" ] || echo >> "$tmp"
if grep -q '@[A-Z][A-Z_]*@' "$tmp"; then
  echo "gen_rc.sh: $tmpl still has a placeholder after substitution" >&2
  rm -f "$tmp"; exit 1
fi
if [ -f "$out" ] && cmp -s "$tmp" "$out"; then rm -f "$tmp"; exit 0; fi
mv "$tmp" "$out"
echo "gen_rc.sh: wrote $out (version $ver)"
