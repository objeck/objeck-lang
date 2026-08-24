#!/usr/bin/env bash
#
# install_deps.sh [--check] [--print] [--dev] [--yes] [--tree <path>]
#
# Install the native libraries Objeck's SDL2 and OpenGL bindings need, using
# whatever package manager this machine actually has.
#
# WHY THIS EXISTS
# ---------------
# The three platforms do not need the same thing, and that asymmetry used to be
# invisible to the user:
#
#   macOS    SDL2 ships INSIDE the distribution (lib/sdl, reached by an @rpath
#            baked into lib/native/libobjk_sdl.dylib) and OpenGL is a system
#            framework. Nothing to install -- but the old instructions still
#            told people to untar sdl2_arm64.tgz into /usr/local/lib, a
#            sudo-level system install that collides with a Homebrew SDL2 and,
#            on Apple Silicon, put the libraries somewhere Homebrew never looks.
#   Windows  the DLLs ship in lib/sdl next to the binaries. Nothing to install.
#   Linux    libobjk_sdl.so is linked against the SYSTEM SDL2 and libGL
#            (see core/lib/sdl/build_linux.sh: -lSDL2 ... -lGL), and
#            deploy_posix.sh ships neither. So Linux, and only Linux, needs
#            packages -- and nothing in the tree told the user which ones.
#
# Bundling SDL2 on Linux the way macOS does is not a good trade: SDL2 there
# pulls in X11/Wayland, ALSA/PulseAudio and glibc, so a bundled copy fights the
# host instead of working with it. One package-manager command is the honest
# answer, and this is that command.
#
# Exit codes:
#   0  everything needed is present (or was just installed)
#   1  something is missing and could not be installed
#   2  this platform/package manager is not recognised
#   3  bad usage
set -uo pipefail

CHECK_ONLY=0
PRINT_ONLY=0
WANT_DEV=0
ASSUME_YES=0
TREE_ARG=""

while [ $# -gt 0 ]; do
	case "$1" in
		--check) CHECK_ONLY=1 ;;
		--print) PRINT_ONLY=1 ;;
		--dev)   WANT_DEV=1 ;;
		--yes|-y) ASSUME_YES=1 ;;
		--tree)  shift; TREE_ARG="${1:-}"; [ -n "$TREE_ARG" ] || { echo "--tree needs a path" >&2; exit 3; } ;;
		-h|--help)
			awk 'NR == 1 { next } /^#/ { sub(/^# ?/, ""); print; next } { exit }' "$0"
			exit 0 ;;
		*) echo "unknown option: $1" >&2; exit 3 ;;
	esac
	shift
done

say() { printf '%s\n' "$*"; }

# --------------------------------------------------------------- macOS / Windows
# Both bundle their own SDL2, so "check" here means verifying the bundle is
# actually intact rather than asking a package manager about it. A tree whose
# @rpath does not resolve fails at dlopen time with a message that says nothing
# about SDL2, so it is worth catching here.
check_bundled_macos() {
	local root="$1" objk="$1/lib/native/libobjk_sdl.dylib" missing=0

	if [ ! -f "$objk" ]; then
		say "no SDL native library at $objk"
		say "build one first:  cd core/release && ./deploy_macos_arm64.sh"
		return 1
	fi

	# every @rpath SDL2 dependency must exist where the rpath points
	local rpath dep base
	rpath=$(otool -l "$objk" 2>/dev/null | awk '/LC_RPATH/{f=1} f && /path /{print $2; exit}')
	for dep in $(otool -L "$objk" 2>/dev/null | awk '/@rpath\/libSDL2/ {print $1}'); do
		base=$(basename "$dep")
		# @loader_path in the rpath is relative to the dylib's own directory
		local resolved="${rpath/@loader_path/$(dirname "$objk")}"
		if [ -f "$resolved/$base" ]; then
			say "  ok       $base"
		else
			say "  MISSING  $base (expected in $resolved)"
			missing=1
		fi
	done

	if otool -L "$objk" 2>/dev/null | grep -q "/usr/local/lib/libSDL2"; then
		say "  WARNING  still references /usr/local/lib for SDL2 -- this tree needs a"
		say "           manual SDL2 install and is not self-contained"
		missing=1
	fi

	[ "$missing" -eq 0 ] && say "SDL2 is bundled in this distribution; nothing to install."
	return $missing
}

# macOS stamps com.apple.quarantine on everything unpacked from a downloaded
# archive, and Gatekeeper then refuses the toolchain. This is worth a dedicated
# check because the headline failure is SILENT: a quarantined obr is SIGKILLed
# (exit 137) with nothing on stdout or stderr. A quarantined dylib is only
# slightly better -- "library load disallowed by system policy", which never
# says the word quarantine. Neither symptom is diagnosable from what the user
# sees, and both are one xattr command away from working.
#
# The .pkg installer is not affected; files it lays down are not quarantined.
# This is specifically the .tgz path.
find_quarantined() {
	find "$1" -type f \( -name "*.dylib" -o -perm -u+x \) 2>/dev/null | while read -r f; do
		if xattr "$f" 2>/dev/null | grep -q "com.apple.quarantine"; then
			printf '%s\n' "$f"
		fi
	done
}

check_quarantine() {
	local tree="$1"
	local quarantined count
	quarantined=$(find_quarantined "$tree")
	[ -z "$quarantined" ] && return 0

	count=$(printf '%s\n' "$quarantined" | wc -l | tr -d ' ')
	say ""
	say "  QUARANTINED  $count file(s) carry com.apple.quarantine."
	say "               macOS will refuse to run them -- a quarantined executable is"
	say "               killed outright, with no error message at all."
	printf '%s\n' "$quarantined" | sed 's|^|                 |' | head -5
	[ "$count" -gt 5 ] && say "                 ... and $((count - 5)) more"
	say ""

	if [ "$CHECK_ONLY" -eq 1 ] || [ "$PRINT_ONLY" -eq 1 ]; then
		say "  Clear it with:"
		say "    xattr -dr com.apple.quarantine \"$tree\""
		return 1
	fi

	if [ "$ASSUME_YES" -eq 0 ]; then
		if [ -t 0 ]; then
			printf '  Remove the quarantine flag now? [y/N] '
			read -r reply
			case "$reply" in
				y|Y|yes|YES) ;;
				*) say "  Left in place. Clear it with:"
				   say "    xattr -dr com.apple.quarantine \"$tree\""
				   return 1 ;;
			esac
		else
			say "  Clear it with:"
			say "    xattr -dr com.apple.quarantine \"$tree\""
			return 1
		fi
	fi

	if xattr -dr com.apple.quarantine "$tree" 2>/dev/null; then
		if [ -z "$(find_quarantined "$tree")" ]; then
			say "  Quarantine cleared."
			return 0
		fi
	fi
	say "  Could not clear the quarantine flag; you may need to run this as the" >&2
	say "  owner of $tree." >&2
	return 1
}

# ------------------------------------------------------------------------ Linux
# Package names per distro family. Runtime is what you need to RUN a GL program
# from a release build; --dev adds the headers needed to BUILD libobjk_sdl.so.
linux_packages() {
	case "$1" in
		apt)
			if [ "$WANT_DEV" -eq 1 ]; then
				echo "libsdl2-dev libsdl2-image-dev libsdl2-mixer-dev libsdl2-ttf-dev libgl1-mesa-dev"
			else
				echo "libsdl2-2.0-0 libsdl2-image-2.0-0 libsdl2-mixer-2.0-0 libsdl2-ttf-2.0-0 libgl1"
			fi ;;
		dnf|yum)
			if [ "$WANT_DEV" -eq 1 ]; then
				echo "SDL2-devel SDL2_image-devel SDL2_mixer-devel SDL2_ttf-devel mesa-libGL-devel"
			else
				echo "SDL2 SDL2_image SDL2_mixer SDL2_ttf mesa-libGL"
			fi ;;
		pacman)
			# Arch has no split runtime/dev packages. It also replaced 'sdl2' with
			# 'sdl2-compat'; pick whichever this machine's repos actually carry, or
			# a fresh Arch install fails on a package name that no longer exists.
			local base="sdl2"
			if ! pacman -Si sdl2 >/dev/null 2>&1 && pacman -Si sdl2-compat >/dev/null 2>&1; then
				base="sdl2-compat"
			fi
			echo "$base sdl2_image sdl2_mixer sdl2_ttf libglvnd" ;;
		zypper)
			if [ "$WANT_DEV" -eq 1 ]; then
				echo "SDL2-devel SDL2_image-devel SDL2_mixer-devel SDL2_ttf-devel Mesa-libGL-devel"
			else
				echo "libSDL2-2_0-0 libSDL2_image-2_0-0 libSDL2_mixer-2_0-0 libSDL2_ttf-2_0-0 Mesa-libGL1"
			fi ;;
		apk)
			if [ "$WANT_DEV" -eq 1 ]; then
				echo "sdl2-dev sdl2_image-dev sdl2_mixer-dev sdl2_ttf-dev mesa-dev"
			else
				echo "sdl2 sdl2_image sdl2_mixer sdl2_ttf mesa-gl"
			fi ;;
	esac
}

linux_install_cmd() {
	local pm="$1"; shift
	case "$pm" in
		apt)    echo "apt-get install -y $*" ;;
		dnf)    echo "dnf install -y $*" ;;
		yum)    echo "yum install -y $*" ;;
		pacman) echo "pacman -S --needed --noconfirm $*" ;;
		zypper) echo "zypper install -y $*" ;;
		apk)    echo "apk add $*" ;;
	esac
}

detect_pm() {
	for pm in apt-get dnf yum pacman zypper apk; do
		if command -v "$pm" >/dev/null 2>&1; then
			[ "$pm" = "apt-get" ] && echo "apt" || echo "$pm"
			return 0
		fi
	done
	return 1
}

# Ask the dynamic linker what is actually loadable, rather than the package
# database what is nominally installed -- they disagree often enough to matter,
# and it is the loader's opinion that decides whether a GL program runs.
linux_missing_libs() {
	local missing=""
	local cache; cache=$(ldconfig -p 2>/dev/null)
	for lib in libSDL2-2.0.so libSDL2_image-2.0.so libSDL2_mixer-2.0.so libSDL2_ttf-2.0.so libGL.so; do
		if ! printf '%s' "$cache" | grep -q "$lib"; then
			missing="$missing $lib"
		fi
	done
	printf '%s' "$missing"
}

# ------------------------------------------------------------------------- main
UNAME=$(uname -s)

case "$UNAME" in
	Darwin)
		say "platform: macOS -- SDL2 is bundled, OpenGL is a system framework"
		say ""
		# find a deploy tree to validate; the repo layout first, then an install
		SELF_DIR=$(cd "$(dirname "$0")" 2>/dev/null && pwd)
		REPO=$(cd "$(dirname "$0")/.." 2>/dev/null && pwd)
		TREE=""
		if [ -n "$TREE_ARG" ]; then
			TREE="$TREE_ARG"
		else
			# $SELF_DIR first: this script ships at the root of the distribution, so
			# when a user runs it from an unpacked tarball or an installed copy, the
			# tree to check is the one it is sitting in -- not some repo checkout
			# that may also exist on the same machine.
			for c in "$SELF_DIR" "$REPO/core/release/deploy" "$REPO/core/release/deploy-arm64" "/usr/local/objeck-lang"; do
				[ -f "$c/lib/native/libobjk_sdl.dylib" ] && { TREE="$c"; break; }
			done
		fi
		if [ -z "$TREE" ]; then
			say "No built distribution found to verify."
			say "Build one:  cd core/release && ./deploy_macos_arm64.sh"
			exit 1
		fi
		say "verifying bundled SDL2 in $TREE"
		check_bundled_macos "$TREE"; BUNDLE_STATUS=$?
		check_quarantine "$TREE"; QUARANTINE_STATUS=$?
		[ "$BUNDLE_STATUS" -eq 0 ] && [ "$QUARANTINE_STATUS" -eq 0 ]
		exit $?
		;;

	MINGW*|MSYS*|CYGWIN*)
		say "platform: Windows -- the SDL2 DLLs ship in lib/sdl. Nothing to install."
		exit 0
		;;

	Linux) ;;  # handled below

	*)
		say "Unrecognised platform: $UNAME" >&2
		exit 2
		;;
esac

PM=$(detect_pm)
if [ -z "$PM" ]; then
	say "No supported package manager found (looked for apt-get, dnf, yum, pacman, zypper, apk)." >&2
	say "Install SDL2 (core, image, mixer, ttf) and an OpenGL runtime by hand." >&2
	exit 2
fi

PKGS=$(linux_packages "$PM")
CMD=$(linux_install_cmd "$PM" "$PKGS")
[ "$(id -u)" -eq 0 ] || CMD="sudo $CMD"

MISSING=$(linux_missing_libs)

if [ "$PRINT_ONLY" -eq 1 ]; then
	say "$CMD"
	exit 0
fi

if [ "$CHECK_ONLY" -eq 1 ]; then
	if [ -z "$MISSING" ]; then
		say "All SDL2 and OpenGL runtime libraries are present."
		exit 0
	fi
	say "Missing:$MISSING"
	say ""
	say "Install them with:"
	say "  $CMD"
	say "or just run:  $0"
	exit 1
fi

if [ -z "$MISSING" ] && [ "$WANT_DEV" -eq 0 ]; then
	say "All SDL2 and OpenGL runtime libraries are already present. Nothing to do."
	exit 0
fi

[ -n "$MISSING" ] && say "Missing:$MISSING" || say "Installing development headers (--dev)."
say ""
say "About to run:"
say "  $CMD"
say ""

if [ "$ASSUME_YES" -eq 0 ]; then
	# No prompt when there is no terminal to prompt on: a piped or CI run would
	# otherwise read EOF and silently take the "no" branch.
	if [ -t 0 ]; then
		printf 'Proceed? [y/N] '
		read -r reply
		case "$reply" in
			y|Y|yes|YES) ;;
			*) say "Aborted. Re-run with --yes to skip this prompt."; exit 1 ;;
		esac
	else
		say "Not a terminal, and --yes was not given. Re-run with --yes to install."
		exit 1
	fi
fi

if [ "$PM" = "apt" ]; then
	if [ "$(id -u)" -eq 0 ]; then apt-get update; else sudo apt-get update; fi
fi

# shellcheck disable=SC2086
eval "$CMD"
STATUS=$?

if [ "$STATUS" -ne 0 ]; then
	say ""
	say "Package installation failed (exit $STATUS)." >&2
	exit 1
fi

STILL_MISSING=$(linux_missing_libs)
if [ -n "$STILL_MISSING" ]; then
	say ""
	say "Installed, but these are still not visible to the loader:$STILL_MISSING" >&2
	say "Try running 'sudo ldconfig', or check that the package names above match" >&2
	say "what this distribution actually calls them." >&2
	exit 1
fi

say ""
say "SDL2 and OpenGL runtime libraries are installed and visible to the loader."
exit 0
