#!/usr/bin/env bash
#
# check_portable_archive.sh [--sha256-out FILE] <objeck-linux-ARCH_VERSION.tgz> <x64|arm64>
#
# The portable-release gate for the file that SHIPS. `check_docs_deps.py --portable` inspects a
# tree, and nothing tied the tree it inspected to the archive a user downloads: a gate that passed a
# build directory proved nothing about the tarball. This unpacks the archive into a FRESH tree,
# runs `check_docs_deps.py --portable` on that tree (core binaries are ELF files for the target, the
# sonames, symbol-version floors and self-contained libraries are the manifest's), and prints the
# archive's sha256 so the distro smoke (test_portable_linux.sh) and the release record the SAME
# bytes. The hash is taken before unpacking and again after the check; a file that changed in between
# fails.
#
# Prints `archive_sha256=<hex>` (also written to --sha256-out) and exits 0 only when the check passed.
set -euo pipefail

die() { echo "check_portable_archive.sh: $*" >&2; exit 1; }

OUT=""
if [ "${1:-}" = --sha256-out ]; then OUT="${2:?--sha256-out needs a file}"; shift 2; fi
TGZ="${1:?usage: check_portable_archive.sh [--sha256-out FILE] <archive.tgz> <x64|arm64>}"
case "${2:?usage: check_portable_archive.sh [--sha256-out FILE] <archive.tgz> <x64|arm64>}" in
  x64|x86_64|amd64) ARCH=x64 ;;
  arm64|aarch64) ARCH=arm64 ;;
  *) die "unknown architecture '$2' (x64 or arm64)" ;;
esac
[ -f "$TGZ" ] || die "no such archive: $TGZ"
[ -s "$TGZ" ] || die "$TGZ is empty"
REPO="$(cd "$(dirname "$0")/../.." && pwd)"
PY="${PYTHON:-}"
if [ -z "$PY" ]; then
  for c in python3 python; do command -v "$c" >/dev/null 2>&1 && { PY="$c"; break; }; done
fi
[ -n "$PY" ] || die "needs python3"

# The file goes in on stdin: sha256sum prefixes a name containing a backslash (a Windows path under
# Git Bash) with one, and tar reads "C:\..." as host:path.
sha256_of() {
  if command -v sha256sum >/dev/null 2>&1; then sha256sum < "$1" | cut -d' ' -f1
  else shasum -a 256 < "$1" | cut -d' ' -f1; fi
}
# a path for the Python that runs the check: native on Git Bash for Windows, unchanged elsewhere
native() { if command -v cygpath >/dev/null 2>&1; then cygpath -w "$1"; else printf '%s' "$1"; fi; }

BEFORE="$(sha256_of "$TGZ")"
echo "archive_sha256=$BEFORE"
echo "archive=$(basename "$TGZ")"
[ -z "$OUT" ] || echo "$BEFORE" > "$OUT"

WORK="$(mktemp -d)"
trap 'rm -rf "$WORK"' EXIT
tar -xzf - -C "$WORK" < "$TGZ" || die "$(basename "$TGZ") is not a readable .tgz"
TREE="$WORK/objeck-lang"
[ -d "$TREE" ] || die "$(basename "$TGZ") has no top-level objeck-lang/ directory (the layout every install step expects)"

"$PY" -I "$REPO/tools/cicd/check_docs_deps.py" --portable "$(native "$TREE")" --platform "linux-$ARCH" \
  || die "the portable check FAILED for $(basename "$TGZ") (sha256 $BEFORE)"

AFTER="$(sha256_of "$TGZ")"
[ "$AFTER" = "$BEFORE" ] || die "$(basename "$TGZ") changed while it was being checked ($BEFORE -> $AFTER)"
echo "PASS portable check: $(basename "$TGZ") linux-$ARCH sha256 $BEFORE"
