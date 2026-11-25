#!/bin/bash
# Build the Objeck artifacts for the Alderaan cluster.
#
#   build_image.sh portable-tgz                 [--jobs N]
#       Docker: compile obr + obc + libs on AlmaLinux 8 (glibc 2.28) from this checkout and
#       write dist/objeck-portable-linux-x64.tgz -- the no-container install, and the input
#       of the portable image.
#
#   build_image.sh docker-image  [--flavor portable|release] [--release-tgz FILE]
#       Docker: build the image and save it as dist/objeck-<flavor>.tar (a docker-archive).
#       Copy that to the cluster and convert it there WITHOUT root:
#           apptainer build objeck-<flavor>.sif docker-archive:objeck-<flavor>.tar
#       If apptainer/singularity is on this machine too, the .sif is built right here.
#
#   build_image.sh sif           [--flavor portable|release] [--release-tgz FILE]
#       On a machine with apptainer and --fakeroot permission (or root): build the .sif
#       straight from container/objeck-<flavor>.def.
#
# The release flavor needs the official tarball (objeck-linux-x64_<version>.tgz from the
# GitHub release page); it is not downloaded for you.
set -euo pipefail

HERE=$(cd "$(dirname "$0")" && pwd)
REPO=$(cd "$HERE/../../.." && pwd)
DIST=${DIST:-$HERE/dist}
MODE=${1:-}
[ -n "$MODE" ] && shift || true
FLAVOR=portable
RELEASE_TGZ=
JOBS=4
while [ $# -gt 0 ]; do
	case "$1" in
		--flavor) FLAVOR=$2; shift 2 ;;
		--release-tgz) RELEASE_TGZ=$2; shift 2 ;;
		--jobs) JOBS=$2; shift 2 ;;
		*) echo "unknown option: $1" >&2; exit 2 ;;
	esac
done

usage() { sed -n '2,24p' "$0" | sed 's/^# \{0,1\}//'; exit "${1:-2}"; }
case "$FLAVOR" in portable|release) ;; *) echo "--flavor must be portable or release" >&2; exit 2 ;; esac
mkdir -p "$DIST"

# Docker Desktop on Windows cannot see MSYS paths such as /tmp/x: translate them
hostpath() { if command -v cygpath >/dev/null 2>&1; then cygpath -m "$1"; else echo "$1"; fi; }

APPTAINER=$(command -v apptainer || command -v singularity || true)

# the tarball the chosen flavor installs
flavor_tgz() {
	if [ "$FLAVOR" = release ]; then
		[ -f "$RELEASE_TGZ" ] || { echo "release flavor: pass --release-tgz objeck-linux-x64_<version>.tgz" >&2; exit 2; }
		echo "$RELEASE_TGZ"
	else
		[ -f "$DIST/objeck-portable-linux-x64.tgz" ] || {
			echo "no $DIST/objeck-portable-linux-x64.tgz: run  build_image.sh portable-tgz  first" >&2; exit 2; }
		echo "$DIST/objeck-portable-linux-x64.tgz"
	fi
}

case "$MODE" in
portable-tgz)
	command -v docker >/dev/null || { echo "docker is required for portable-tgz" >&2; exit 1; }
	SRC=$(mktemp -d)
	trap 'rm -rf "$SRC"' EXIT
	# only what the portable build reads: the compiler, VM, shared headers, libs, the ML native, deps
	( cd "$REPO" && tar c core/compiler core/vm core/shared core/lib/matrix core/lib/*.obl \
		core/lib/configobjk.ini core/release/linux/env.sh tools/deps ) | tar x -C "$SRC"
	cp "$HERE/container/build_portable.sh" "$SRC/build_portable.sh"
	MSYS_NO_PATHCONV=1 docker run --rm -v "$(hostpath "$SRC"):/src:ro" -v "$(hostpath "$DIST"):/out" -e JOBS="$JOBS" \
		almalinux:8 bash /src/build_portable.sh
	echo "wrote $DIST/objeck-portable-linux-x64.tgz"
	;;
docker-image)
	command -v docker >/dev/null || { echo "docker is required for docker-image" >&2; exit 1; }
	TGZ=$(flavor_tgz)
	CTX=$(mktemp -d)
	trap 'rm -rf "$CTX"' EXIT
	cp "$TGZ" "$CTX/objeck.tgz"
	cp "$HERE/container/Dockerfile.image" "$CTX/Dockerfile"
	BASE=almalinux:8; [ "$FLAVOR" = release ] && BASE=ubuntu:24.04
	MSYS_NO_PATHCONV=1 docker build -t "objeck-alderaan:$FLAVOR" --build-arg BASE=$BASE \
		--build-arg FLAVOR="$FLAVOR" "$(hostpath "$CTX")"
	docker save -o "$(hostpath "$DIST")/objeck-$FLAVOR.tar" "objeck-alderaan:$FLAVOR"
	echo "wrote $DIST/objeck-$FLAVOR.tar"
	if [ -n "$APPTAINER" ]; then
		"$APPTAINER" build --force "$DIST/objeck-$FLAVOR.sif" "docker-archive:$DIST/objeck-$FLAVOR.tar"
		echo "wrote $DIST/objeck-$FLAVOR.sif"
	else
		cat <<EOF
No apptainer/singularity here. On the cluster (no root needed):
  scp $DIST/objeck-$FLAVOR.tar <user>@<alderaan-login>:~/
  ssh <user>@<alderaan-login>
  apptainer build ~/objeck-$FLAVOR.sif docker-archive:~/objeck-$FLAVOR.tar   # or: singularity build ...
EOF
	fi
	;;
sif)
	[ -n "$APPTAINER" ] || { echo "apptainer/singularity not found" >&2; exit 1; }
	TGZ=$(flavor_tgz)
	W=$(mktemp -d)
	trap 'rm -rf "$W"' EXIT
	cp "$HERE/container/objeck-$FLAVOR.def" "$W/"
	if [ "$FLAVOR" = release ]; then cp "$TGZ" "$W/objeck-release.tgz"; else cp "$TGZ" "$W/objeck-portable-linux-x64.tgz"; fi
	OPT=--fakeroot; [ "$(id -u)" = 0 ] && OPT=
	( cd "$W" && "$APPTAINER" build $OPT --force "$DIST/objeck-$FLAVOR.sif" "objeck-$FLAVOR.def" )
	echo "wrote $DIST/objeck-$FLAVOR.sif"
	;;
-h|--help|help|"") usage 0 ;;
*) echo "unknown mode: $MODE" >&2; usage 2 ;;
esac
