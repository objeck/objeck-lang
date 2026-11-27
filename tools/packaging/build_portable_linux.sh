#!/usr/bin/env bash
#
# build_portable_linux.sh [--deps <dir>] [--out <dir>] [--no-gate]
#
# Build the Linux release tree and tarball the way the release does: inside the
# AlmaLinux 8 image from tools/packaging/portable-linux/Dockerfile, so the
# binaries run on every glibc 2.28+ distribution. Design and reasons:
# docs/design/PORTABLE_LINUX.md.
#
# Run it from a checkout, on the architecture being built (x86_64 or aarch64),
# with docker. It builds IN the checkout, as deploy_posix.sh always has:
#
#   core/release/deploy                    the tree
#   <out>/objeck-linux-<arch>_0.0.0.tgz    the tarball (default <out>: ~/Desktop)
#   <deps>                                 the static libraries, kept between runs
#                                          (default: ~/objeck-deps/portable-linux-<machine>)
#
# and then holds the tree to core/release/runtime_deps.json
# (tools/cicd/check_docs_deps.py --portable), unless --no-gate.
#
# DOCKER="sudo docker" where docker needs root. OBJECK_REQUIRE_API_DOCS=1 makes a
# missing docs/api.zip an error, as the release build does.

set -euo pipefail

die() { echo "build_portable_linux.sh: $*" >&2; exit 1; }

case "$(uname -m)" in
  x86_64)  ARCH=x64 ;;
  aarch64) ARCH=arm64 ;;
  *) die "unsupported machine $(uname -m)" ;;
esac

# ------------------------------------------------------------------ inside
# The second half of this script runs in the container, started by the first.
if [ "${1:-}" = --in-container ]; then
  [ -f /etc/almalinux-release ] || die "--in-container is for the build image only"
  # The checkout, the output and the libraries belong to whoever started the
  # container, however this ends.
  # /out is the caller's directory and may hold other releases (another
  # architecture's, built by someone else) and backups. This invocation owns
  # exactly one file there, the staging name the host passed in STAGE_NAME, so
  # that one file is all that is chowned: a glob over objeck-linux-*.tgz would
  # take ownership of a neighbour's archive even when this build fails early.
  [ -n "${STAGE_NAME:-}" ] || die "STAGE_NAME names the staging archive in /out and is required"
  case "$STAGE_NAME" in */*) die "STAGE_NAME must be a file name" ;; esac
  trap 'chown -R "$HOST_UID:$HOST_GID" /src /deps; [ ! -e "/out/$STAGE_NAME" ] || chown "$HOST_UID:$HOST_GID" "/out/$STAGE_NAME" || true' EXIT
  export OBJECK_DEPS=/deps
  bash /src/tools/deps/build_linux_portable_deps.sh "$OBJECK_DEPS"

  # Headers and archives for every make and build_linux.sh below. The makefiles
  # under core/vm/arch and the native libraries' scripts name no include or
  # library directory for mbedTLS; they find the system's on a developer
  # machine and these here. /deps/lib holds archives only and the compiler
  # wrapper searches it first, so -lmbedtls links statically.
  # sdl2-link is what libobjk_sdl is compiled and linked against, not shipped.
  export CPATH="$OBJECK_DEPS/include"
  export OBJECK_LINK_DIRS="$OBJECK_DEPS/lib:$OBJECK_DEPS/sdl2-link/lib"

  # Eigen is header-only and vendored (core/lib/matrix/lib/eigen-3.4.0);
  # AlmaLinux 8 packages 3.3.4. OpenCV is the static build in /deps/opencv.
  PC=$(mktemp -d)
  cat > "$PC/eigen3.pc" <<EOF
Name: Eigen3
Description: the copy vendored in core/lib/matrix/lib
Version: 3.4.0
Cflags: -I/src/core/lib/matrix/lib/eigen-3.4.0
EOF
  export PKG_CONFIG_PATH="$PC:$OBJECK_DEPS/opencv/lib/pkgconfig:$OBJECK_DEPS/sdl2-link/lib/pkgconfig"

  # deploy_posix.sh stages and tars in ~/Desktop, and empties ~/Desktop/objeck*
  # before it does. That is a private directory here, not --out: --out may hold
  # an older release or a backups folder, which that cleanup would delete. Only
  # the finished tarball is moved to /out.
  export HOME=/home/build
  mkdir -p "$HOME/Desktop"

  cd /src/core/release
  ./deploy_posix.sh "$ARCH" deploy
  TARBALL="$HOME/Desktop/objeck-linux-${ARCH}_0.0.0.tgz"
  [ -f "$TARBALL" ] || die "deploy_posix.sh wrote no $TARBALL"

  # A shared library links with symbols left undefined, and the loader only
  # looks for a function when it is first called. So a native library that
  # names a system library's function without naming the library would pass
  # every check here and fail in a user's program. Here, where every library
  # the bindings use is installed, nothing may be left unresolved.
  unresolved=0
  for lib in deploy/lib/native/libobjk_*.so; do
    out=$(LD_LIBRARY_PATH="$OBJECK_DEPS/sdl2-link/lib" ldd -r "$lib" 2>&1 | grep -E "undefined symbol|not found" || true)
    if [ -n "$out" ]; then
      echo "ERROR: $lib does not resolve:"; echo "$out" | sed 's/^/    /'
      unresolved=1
    fi
  done
  [ $unresolved -eq 0 ] || die "a native library has unresolved symbols"
  echo "native libraries: every symbol resolves"
  # Only a build that passed its checks leaves anything in /out, and under the
  # staging name: the host publishes it after its own gate.
  mv -f "$TARBALL" "/out/$STAGE_NAME"
  exit 0
fi

# ----------------------------------------------------------------- outside
REPO="$(cd "$(dirname "$0")/../.." && pwd)"
DEPS="$HOME/objeck-deps/portable-linux-$(uname -m)"
OUT="$HOME/Desktop"
GATE=1
while [ $# -gt 0 ]; do
  case "$1" in
    --deps) shift; DEPS="${1:?--deps needs a directory}" ;;
    --out) shift; OUT="${1:?--out needs a directory}" ;;
    --no-gate) GATE=0 ;;
    -h|--help) sed -n '3,22p' "$0"; exit 0 ;;
    *) die "unknown argument $1" ;;
  esac
  shift
done
read -r -a DOCKER <<< "${DOCKER:-docker}"
"${DOCKER[@]}" version >/dev/null 2>&1 || die "docker is not usable as '${DOCKER[*]}' (set DOCKER=\"sudo docker\"?)"
mkdir -p "$DEPS" "$OUT"
# docker run -v treats a path with no slash ("out") as a named volume, not a
# directory, and the host would then check an out/ the build never wrote to.
# Mount absolute host paths only.
DEPS="$(cd "$DEPS" && pwd -P)"
OUT="$(cd "$OUT" && pwd -P)"

IMAGE=objeck-portable-linux:el8
echo "== build image $IMAGE"
"${DOCKER[@]}" build -q -t "$IMAGE" "$REPO/tools/packaging/portable-linux"

# The archive a previous build left at $TGZ is the last good one, and stays until
# a new one has passed every check. This build writes under a name of its own
# (unique, so a file found there can only be this build's) and replaces $TGZ with
# one atomic mv at the very end. A failed build, in the container or in the gate,
# leaves $TGZ as it was and removes its staging file.
TGZ="$OUT/objeck-linux-${ARCH}_0.0.0.tgz"
STAGE_NAME=".staging-$$-$(date +%s)-${RANDOM}.tgz"
STAGE="$OUT/$STAGE_NAME"
trap 'rm -f "$STAGE"' EXIT

echo "== build linux-$ARCH in $IMAGE"
"${DOCKER[@]}" run --rm \
  -v "$REPO:/src" -v "$DEPS:/deps" -v "$OUT:/out" \
  -e HOST_UID="$(id -u)" -e HOST_GID="$(id -g)" -e STAGE_NAME="$STAGE_NAME" \
  -e OBJECK_REQUIRE_API_DOCS="${OBJECK_REQUIRE_API_DOCS:-}" \
  -e GITHUB_ACTIONS="${GITHUB_ACTIONS:-}" -e VERBOSE="${VERBOSE:-}" \
  "$IMAGE" bash /src/tools/packaging/build_portable_linux.sh --in-container

[ -f "$STAGE" ] || die "the build wrote no archive"

if [ "$GATE" = 1 ]; then
  python3 "$REPO/tools/cicd/check_docs_deps.py" --portable "$REPO/core/release/deploy" --platform "linux-$ARCH"
fi
# Published only now, after the container's checks and the gate.
mv -f "$STAGE" "$TGZ"
echo "$TGZ"
