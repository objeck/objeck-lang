#!/usr/bin/env bash
#
# build_deb.sh <objeck-linux-ARCH_VERSION.tgz> [--out <dir>]
#
# Package a RELEASED Linux tarball as a .deb for Ubuntu 24.04 -- the same files,
# unchanged, under /usr/lib/objeck, with wrappers in /usr/bin. Design and the
# reasons for each choice: docs/DEB_PACKAGE_DESIGN.md.
#
# Run it on the architecture being packaged, on Ubuntu 24.04, with the tarball's
# runtime libraries installed (its own install_deps.sh --yes does that):
# dpkg-shlibdeps reads the package database of THIS machine to name the
# dependencies, so a machine without them, or of another release, would write a
# package that installs nowhere -- or everywhere except where it runs.
#
# Prints the path of the .deb it wrote on its last line.

set -euo pipefail

die() { echo "build_deb.sh: $*" >&2; exit 1; }

TGZ=""; OUT="$PWD"
while [ $# -gt 0 ]; do
  case "$1" in
    --out) shift; OUT="${1:?--out needs a directory}" ;;
    -h|--help) sed -n '3,14p' "$0"; exit 0 ;;
    *) [ -z "$TGZ" ] || die "one tarball at a time"; TGZ="$1" ;;
  esac
  shift
done
[ -n "$TGZ" ] && [ -f "$TGZ" ] || die "usage: build_deb.sh <objeck-linux-ARCH_VERSION.tgz> [--out <dir>]"

# The tarball's name is the only place the version and architecture are stated,
# and the release gates already hold it to this exact form.
name="$(basename "$TGZ")"
[[ "$name" =~ ^objeck-linux-(x64|arm64)_([0-9]+\.[0-9]+\.[0-9]+)\.tgz$ ]] \
  || die "$name is not objeck-linux-<x64|arm64>_<version>.tgz"
ARCH="${BASH_REMATCH[1]}"; VERSION="${BASH_REMATCH[2]}"
case "$ARCH" in x64) DEB_ARCH=amd64 ;; arm64) DEB_ARCH=arm64 ;; esac

for tool in dpkg-deb dpkg-shlibdeps dpkg-architecture; do
  command -v "$tool" >/dev/null || die "needs $tool (apt-get install dpkg-dev)"
done
HOST_ARCH="$(dpkg-architecture -qDEB_HOST_ARCH)"
[ "$HOST_ARCH" = "$DEB_ARCH" ] \
  || die "packaging $DEB_ARCH on a $HOST_ARCH machine -- dependencies are read from this machine, so run it on $DEB_ARCH"
# In a subshell: os-release defines VERSION too, and would overwrite ours.
OS="$(. /etc/os-release && echo "${ID:-}-${VERSION_ID:-} ${PRETTY_NAME:-an unknown system}")"
[ "${OS%% *}" = ubuntu-24.04 ] \
  || echo "build_deb.sh: WARNING: building on ${OS#* }, not Ubuntu 24.04 -- the package will depend on THIS system's library packages" >&2

WORK="$(mktemp -d)"
trap 'rm -rf "$WORK"' EXIT
ROOT="$WORK/root"
PREFIX=/usr/lib/objeck
mkdir -p "$ROOT$PREFIX" "$ROOT/usr/bin" "$ROOT/DEBIAN"

tar -xzf "$TGZ" -C "$WORK"
[ -x "$WORK/objeck-lang/bin/obc" ] || die "$name has no objeck-lang/bin/obc"
cp -a "$WORK/objeck-lang/." "$ROOT$PREFIX/"

# --------------------------------------------------------------- wrappers
# Not symlinks: without OBJECK_LIB_PATH the tools look for ../lib/ relative to the
# CURRENT directory, not to themselves (GetLibraryPath, core/shared/sys.h).
for tool in obc obr obi obd obb; do
  [ -x "$ROOT$PREFIX/bin/$tool" ] || die "$name has no bin/$tool"
  cat > "$ROOT/usr/bin/$tool" <<EOF
#!/bin/sh
# Installed by the objeck package: the real $tool is in $PREFIX/bin.
: "\${OBJECK_LIB_PATH:=$PREFIX/lib}"
export OBJECK_LIB_PATH
exec $PREFIX/bin/$tool "\$@"
EOF
done

# obu rewrites the install in place, which would leave dpkg's record of these
# files wrong. Its read-only commands are still worth having.
cat > "$ROOT/usr/bin/obu" <<EOF
#!/bin/sh
# Installed by the objeck package: the real obu is in $PREFIX/bin.
case "\${1:-}" in
  update|rollback)
    echo "obu: this Objeck was installed by apt, so 'obu \$1' would leave apt's record of it wrong." >&2
    echo "     Download the new .deb from https://github.com/objeck/objeck-lang/releases/latest and run:" >&2
    echo "       sudo apt install ./objeck-ubuntu24.04-${ARCH}_<version>.deb" >&2
    exit 1 ;;
esac
: "\${OBJECK_LIB_PATH:=$PREFIX/lib}"
export OBJECK_LIB_PATH
exec $PREFIX/bin/obu "\$@"
EOF

# objeck-shell finds env.sh relative to itself, so it must be run from the tree.
cat > "$ROOT/usr/bin/objeck-shell" <<EOF
#!/bin/sh
exec $PREFIX/bin/objeck-shell "\$@"
EOF
chmod 755 "$ROOT"/usr/bin/*

# The tarball's installers have nothing to do in a package: apt installs the
# libraries, and the desktop entry is installed below.
rm -f "$ROOT$PREFIX/install_deps.sh" "$ROOT$PREFIX/install-desktop.sh"

# ----------------------------------------------------------- desktop entry
if [ -f "$ROOT$PREFIX/objeck.desktop.in" ]; then
  mkdir -p "$ROOT/usr/share/applications"
  sed "s|@OBJECK_HOME@|$PREFIX|g" "$ROOT$PREFIX/objeck.desktop.in" \
    | grep -v '^#' > "$ROOT/usr/share/applications/objeck.desktop"
  rm -f "$ROOT$PREFIX/objeck.desktop.in"
fi

mkdir -p "$ROOT/usr/share/doc/objeck"
cp "$ROOT$PREFIX/LICENSE" "$ROOT/usr/share/doc/objeck/copyright"
# The third-party license texts the tarball carries in doc/licenses, one folder
# per component. Linked rather than copied, so the package holds one copy and
# /usr/share/doc/objeck/licenses is where Debian users look for them.
[ -d "$ROOT$PREFIX/doc/licenses" ] || die "$name has no doc/licenses"
ln -s "$PREFIX/doc/licenses" "$ROOT/usr/share/doc/objeck/licenses"
printf '\nThe full license texts of the third-party software above are in\n/usr/share/doc/objeck/licenses, one folder per component.\n' \
  >> "$ROOT/usr/share/doc/objeck/copyright"

# ------------------------------------------------------------ dependencies
# dpkg-shlibdeps wants a source package's debian/control to exist; it reads
# nothing from it that matters here.
mkdir -p "$WORK/src/debian"
printf 'Source: objeck\n\nPackage: objeck\nArchitecture: any\n' > "$WORK/src/debian/control"

elf_files() { for f in "$@"; do [ -f "$f" ] && head -c4 "$f" | grep -q 'ELF' && echo "$f"; done; }
mapfile -t CORE < <(elf_files "$ROOT$PREFIX"/bin/*)
mapfile -t OPTIONAL < <(elf_files "$ROOT$PREFIX"/lib/native/libobjk_*.so)
[ "${#CORE[@]}" -gt 0 ] || die "no binaries found in bin/"

# -l: the bundled ONNX Runtime resolves through $ORIGIN, which dpkg-shlibdeps does
# not follow, so it is told where to look instead of reporting it missing.
shlibdeps() {
  ( cd "$WORK/src" && dpkg-shlibdeps -O -l"$ROOT$PREFIX/lib/native" \
      --ignore-missing-info "$@" 2>"$WORK/shlibdeps.err" ) \
    | sed -n 's/^shlibs:Depends=//p'
  if grep -q 'error' "$WORK/shlibdeps.err"; then cat "$WORK/shlibdeps.err" >&2; die "dpkg-shlibdeps failed"; fi
}
DEPENDS="$(shlibdeps "${CORE[@]}")"
[ -n "$DEPENDS" ] || die "dpkg-shlibdeps named no dependencies for bin/ -- are the runtime libraries installed? (install_deps.sh --yes)"

# Recommends: what only the optional modules need, minus what Depends already has.
RECOMMENDS=""
if [ "${#OPTIONAL[@]}" -gt 0 ]; then
  ALL_OPT="$(shlibdeps "${OPTIONAL[@]}")"
  core_names=" $(echo "$DEPENDS" | tr ',' '\n' | awk '{print $1}' | tr '\n' ' ') "
  RECOMMENDS="$(echo "$ALL_OPT" | tr ',' '\n' | sed 's/^ *//' | grep -v '^$' | while read -r d; do
    case "$core_names" in *" ${d%% *} "*) ;; *) echo "$d" ;; esac
  done | paste -sd, - | sed 's/,/, /g')"
fi

INSTALLED_KB="$(du -sk "$ROOT" --exclude=DEBIAN | awk '{print $1}')"

{
  echo "Package: objeck"
  echo "Version: $VERSION"
  echo "Architecture: $DEB_ARCH"
  echo "Maintainer: Randy Hollines <objeck@gmail.com>"
  echo "Installed-Size: $INSTALLED_KB"
  echo "Depends: $DEPENDS"
  [ -z "$RECOMMENDS" ] || echo "Recommends: $RECOMMENDS"
  echo "Section: devel"
  echo "Priority: optional"
  echo "Homepage: https://www.objeck.org"
  echo "Description: Objeck programming language: compiler, VM with JIT, and tools"
  echo " Objeck is an object-oriented, functional language with a JIT-compiling"
  echo " virtual machine for AMD64 and ARM64. This package installs the compiler"
  echo " (obc), the VM (obr), the REPL (obi), the debugger (obd), the app builder"
  echo " (obb) and the updater's verify command (obu), with the standard libraries,"
  echo " API documentation and examples, under $PREFIX."
  echo " ."
  echo " The recommended packages are needed only by the libraries that wrap them:"
  echo " OpenCV, SDL2 and OpenGL, unixODBC and LAME."
} > "$ROOT/DEBIAN/control"

mkdir -p "$OUT"
DEB="$(cd "$OUT" && pwd)/objeck-ubuntu24.04-${ARCH}_${VERSION}.deb"
dpkg-deb --root-owner-group -Zxz --build "$ROOT" "$DEB" >/dev/null

echo "Depends:    $DEPENDS" >&2
echo "Recommends: ${RECOMMENDS:-(none)}" >&2
echo "$DEB"
