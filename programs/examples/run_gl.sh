#!/bin/sh
# Build and run an Objeck OpenGL example on Linux or macOS.
#
#   ./run_gl.sh                    # the 3D walkthrough
#   ./run_gl.sh cube_gl            # any example in this directory
#   ./run_gl.sh --verify           # run the GL self-test instead, and report
#   ./run_gl.sh --tree <path>      # use a specific deploy tree
#
# Exists because getting a GL demo running by hand means knowing four things that
# are easy to get wrong: which deploy tree to use, whether its toolchain matches
# the source, where the SDL runtime libraries live on this platform, and which
# -lib flags the example needs. All four have cost real debugging time.

set -e

HERE=$(cd "$(dirname "$0")" && pwd)
REPO=$(cd "$HERE/../.." && pwd)

DEMO="gl_walkthrough"
VERIFY=0
TREE=""

while [ $# -gt 0 ]; do
	case "$1" in
		--verify) VERIFY=1 ;;
		--tree) shift; TREE="$1" ;;
		-h|--help)
			# print the header comment, stopping at the first non-comment line, so
			# this cannot drift out of sync with a hardcoded line range
			awk 'NR == 1 { next } /^#/ { sub(/^# ?/, ""); print; next } { exit }' "$0"
			exit 0 ;;
		-*) echo "unknown option: $1"; exit 2 ;;
		*) DEMO="$1" ;;
	esac
	shift
done

# ---------------------------------------------------------------- deploy tree
if [ -z "$TREE" ]; then
	for candidate in deploy deploy-arm64 deploy-x64; do
		if [ -x "$REPO/core/release/$candidate/bin/obc" ]; then
			TREE="$REPO/core/release/$candidate"
			break
		fi
	done
fi

if [ -z "$TREE" ] || [ ! -x "$TREE/bin/obc" ]; then
	echo "No Objeck deploy tree with a POSIX obc found under core/release."
	echo ""
	echo "Build one first:"
	echo "  cd core/release"
	case "$(uname -s)" in
		Darwin) echo "  ./deploy_macos_arm64.sh" ;;
		*)      echo "  ./deploy_posix.sh x64" ;;
	esac
	exit 1
fi

OBC="$TREE/bin/obc"
OBR="$TREE/bin/obr"

# A stale tree is the most confusing failure here: obc and the .obl carry a
# version stamp, and a mismatch surfaces as "This executable appears to be
# invalid or compiled with an incompatible version of the tool chain" -- which
# reads like a corrupt program, not an out-of-date build. Catch it up front.
SRC_VERSION=$(sed -n 's/^#define VERSION_STRING L"\(.*\)"/\1/p' "$REPO/core/shared/version.h")
TREE_VERSION=$("$OBC" -version 2>/dev/null | head -1 | awk '{print $1}')
if [ -n "$SRC_VERSION" ] && [ "$SRC_VERSION" != "$TREE_VERSION" ]; then
	echo "Deploy tree is stale: $TREE"
	echo "  its obc says $TREE_VERSION, but core/shared/version.h says $SRC_VERSION"
	echo ""
	echo "Rebuild the tree, or pass --tree <path> to point at a current one."
	exit 1
fi

# ---------------------------------------------------------------- prerequisites
case "$(uname -s)" in
	Darwin) NATIVE="libobjk_sdl.dylib" ;;
	*)      NATIVE="libobjk_sdl.so" ;;
esac

if [ ! -f "$TREE/lib/native/$NATIVE" ]; then
	echo "Missing $TREE/lib/native/$NATIVE"
	echo ""
	echo "Build the SDL native library:"
	echo "  cd core/lib/sdl"
	case "$(uname -s)" in
		Darwin) echo "  xcodebuild -project macos/xcode/sdl.xcodeproj build" ;;
		*)      echo "  ./build_linux.sh sdl && cp sdl.so \"$TREE/lib/native/$NATIVE\"" ;;
	esac
	exit 1
fi

for obl in sdl2.obl sdl_gl.obl gen_collect.obl; do
	if [ ! -f "$TREE/lib/$obl" ]; then
		echo "Missing $TREE/lib/$obl -- rebuild the Objeck libraries:"
		echo "  cd core/compiler && ./update_version.sh"
		exit 1
	fi
done

# On macOS the SDL2 dylibs ship inside the tree and are found via an @rpath
# baked into libobjk_sdl.dylib, so nothing needs setting. On Linux SDL2 comes
# from the system. Either way, exporting the tree's lib dir helps the VM find
# its own natives when run from elsewhere.
export OBJECK_LIB_PATH="$TREE/lib"
if [ -d "$TREE/lib/native" ]; then
	export LD_LIBRARY_PATH="$TREE/lib/native:${LD_LIBRARY_PATH}"
	export DYLD_LIBRARY_PATH="$TREE/lib/native:${DYLD_LIBRARY_PATH}"
fi

# ---------------------------------------------------------------- run
cd "$TREE/bin"

if [ "$VERIFY" -eq 1 ]; then
	echo "Building the OpenGL self-test..."
	"$OBC" -src "$REPO/programs/regression/gl_context_test.obs" \
	       -lib cipher,collect,xml,json,sdl2,sdl_gl \
	       -dest /tmp/objeck_gl_test.obe > /dev/null
	echo ""
	# OBJECK_GL_REQUIRED turns "skip when there is no GL" into "fail when there
	# is no GL", which is what you want when testing deliberately.
	OBJECK_GL_REQUIRED=1 "$OBR" /tmp/objeck_gl_test.obe
	exit $?
fi

SRC="$HERE/$DEMO.obs"
if [ ! -f "$SRC" ]; then
	echo "No such example: $SRC"
	echo "Available:"
	ls "$HERE"/gl_*.obs "$HERE"/cube_gl.obs 2>/dev/null | sed 's|.*/|  |; s|\.obs$||'
	exit 1
fi

# Assets a demo needs. We run from the deploy tree's bin directory, because the
# demos reach for fonts at ../lib/sdl/fonts -- so a demo that also loads a file
# from its own source directory cannot find it by a relative path, and has to be
# told where it is. gl_model was silently failing this way.
DEMO_ARGS=""
case "$DEMO" in
	gl_model) DEMO_ARGS="$HERE/gl_crystal.obj" ;;
esac

echo "Building $DEMO..."
"$OBC" -src "$SRC" -lib sdl2,sdl_gl -dest "/tmp/objeck_$DEMO.obe" > /dev/null
echo "Running $DEMO -- escape to quit."
echo ""
if [ -n "$DEMO_ARGS" ]; then
	exec "$OBR" "/tmp/objeck_$DEMO.obe" "$DEMO_ARGS"
fi
exec "$OBR" "/tmp/objeck_$DEMO.obe"
