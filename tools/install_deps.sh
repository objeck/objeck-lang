#!/usr/bin/env bash
#
# install_deps.sh [--check] [--print] [--dev] [--yes] [--sdl] [--tree <path>]
#
# Install the system libraries an Objeck distribution needs to run, using
# whatever package manager this machine actually has.
#
#   (no option)    install what is missing (asks first)
#   --yes, -y      ...without the prompt
#   --check        report what is missing, install nothing
#   --print        print the install command, run nothing
#   --dev          also the SDL2/OpenGL headers, to BUILD libobjk_sdl.so
#   --sdl          only what SDL2/OpenGL programs need (obc, obr, libobjk_sdl.so)
#   --tree <path>  the distribution to check; default: the one this script is in
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
#   Windows  the DLLs ship in bin, beside the binaries that load them, which is
#            where Windows looks first. Nothing to install.
#   Linux    the toolchain and its native libraries are linked against SYSTEM
#            libraries the distribution does not ship: obr, obd and obi need
#            mbedTLS, obr also nghttp2/ngtcp2/nghttp3 (HTTP/2 and HTTP/3), obd
#            readline, and lib/native needs SDL2 + libGL, OpenCV, unixODBC and
#            LAME. On a clean machine obr does not start at all. So Linux, and
#            only Linux, needs packages -- and nothing in the tree said which.
#
# On Linux the list is not hardcoded. The script asks ldd which libraries the
# tree's own binaries (bin/*, lib/native/libobjk_*.so) cannot resolve, then asks
# the package manager which package provides each one. Library versions and
# package names differ per distribution -- Ubuntu 24.04 calls mbedTLS 2.28
# libmbedtls14t64, Debian 12 libmbedtls14, and Fedora 44 ships no mbedTLS 2.x at
# all -- so a fixed list is wrong somewhere; the loader and the package database
# of the machine in front of us are not.
#
# Bundling these on Linux the way macOS bundles SDL2 is not a good trade: SDL2
# there pulls in X11/Wayland, ALSA/PulseAudio and glibc, so a bundled copy fights
# the host instead of working with it. One package-manager command is the honest
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
SDL_ONLY=0
TREE_ARG=""

while [ $# -gt 0 ]; do
	case "$1" in
		--check) CHECK_ONLY=1 ;;
		--print) PRINT_ONLY=1 ;;
		--dev)   WANT_DEV=1 ;;
		--yes|-y) ASSUME_YES=1 ;;
		--sdl)   SDL_ONLY=1 ;;
		--tree)  shift; TREE_ARG="${1:-}"; [ -n "$TREE_ARG" ] || { echo "--tree needs a path" >&2; exit 3; } ;;
		-h|--help)
			awk 'NR == 1 { next } /^#/ { sub(/^# ?/, ""); print; next } { exit }' "$0"
			exit 0 ;;
		*) echo "unknown option: $1" >&2; exit 3 ;;
	esac
	shift
done

say() { printf '%s\n' "$*"; }

SELF_DIR=$(cd "$(dirname "$0")" 2>/dev/null && pwd)
REPO=$(cd "$(dirname "$0")/.." 2>/dev/null && pwd)

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

# Quarantine only MATTERS when the binaries are not notarized. Apple's notary
# service records each binary's cdhash, so a notarized executable runs from a
# quarantined download with no user action -- Gatekeeper simply checks it. That
# is the entire point of notarizing, and since v2026.9.0 it is what releases do.
#
# Checking this rather than assuming it matters both ways. Telling someone their
# notarized install is about to be "killed outright" is a false alarm that
# invites a pointless xattr sweep; staying silent about an unsigned local build
# hides the silent SIGKILL this check exists to explain.
is_notarized() {
	local probe
	# any one Mach-O answers for the tree: they are signed and notarized together
	for probe in "$1/bin/obr" "$1/bin/obc"; do
		[ -f "$probe" ] || continue
		codesign --test-requirement="=notarized" --verify "$probe" >/dev/null 2>&1
		return $?
	done
	return 1
}

check_quarantine() {
	local tree="$1"
	local quarantined count
	quarantined=$(find_quarantined "$tree")
	[ -z "$quarantined" ] && return 0

	count=$(printf '%s\n' "$quarantined" | wc -l | tr -d ' ')

	if is_notarized "$tree"; then
		say ""
		say "  quarantine   $count file(s) carry com.apple.quarantine, which is normal for"
		say "               a download and harmless here: these binaries are notarized, so"
		say "               macOS runs them as they are. Nothing to do."
		return 0
	fi

	say ""
	say "  QUARANTINED  $count file(s) carry com.apple.quarantine, and these binaries"
	say "               are not notarized. macOS will refuse to run them -- a quarantined"
	say "               executable is killed outright, with no error message at all."
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
# Package names per distro family for SDL2 and OpenGL. These are used for --dev
# (headers to BUILD libobjk_sdl.so, which no tree can tell us about) and, for
# runtime, only when there is no distribution to inspect -- a repo checkout that
# has not been built. With a distribution in hand the list comes from ldd.
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
		# -Syu, not -S: a fresh Arch install or container has no sync database,
		# so -S finds no targets at all, and refreshing it with -Sy but no -u is
		# the partial upgrade Arch does not support.
		pacman) echo "pacman -Syu --needed --noconfirm $*" ;;
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

as_root() {
	if [ "$(id -u)" -eq 0 ]; then "$@"; else sudo "$@"; fi
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

# The files whose loader view decides whether a distribution works: every
# binary in bin/ and every Objeck native library. The ONNX Runtime libraries
# that ship beside libobjk_onnx.so are reached through its $ORIGIN RUNPATH, so
# checking the library that loads them covers them too.
tree_files() {
	local tree="$1" f
	if [ "$SDL_ONLY" -eq 1 ]; then
		set -- "$tree/bin/obc" "$tree/bin/obr" "$tree/lib/native/libobjk_sdl.so"
	else
		set -- "$tree"/bin/* "$tree"/lib/native/libobjk_*.so
	fi
	for f in "$@"; do
		[ -f "$f" ] && printf '%s\n' "$f"
	done
}

# ldd prints "not a dynamic executable" for an ELF built for another
# architecture, and musl's loader cannot run a glibc binary at all. In both
# cases no package will help, so say so rather than listing phantom libraries.
#
# Output is captured before matching: ldd exits non-zero in exactly these cases,
# and under pipefail "ldd | grep -q" would report ldd's failure, not grep's match.
tree_unusable_reason() {
	local obr="$1/bin/obr" out
	out=$(ldd --version 2>&1)
	if printf '%s' "$out" | grep -qi musl; then
		say "This system uses musl libc (Alpine and similar); Objeck's Linux releases"
		say "are built against glibc."
		return 0
	fi
	out=$(ldd "$obr" 2>&1)
	if printf '%s' "$out" | grep -q "not a dynamic executable"; then
		say "bin/obr was built for a different architecture than this machine ($(uname -m))."
		return 0
	fi
	out=$(tree_version_errors "$1")
	if [ -n "$out" ]; then
		say "This system's libraries are older than the ones the distribution was built"
		say "against:"
		printf '%s\n' "$out" | awk '{ printf "  %-22s has no %s\n", $1, $2 }'
		say "Linux releases are built on Ubuntu 24.04 and need glibc 2.38 or newer."
		say "Installing packages cannot upgrade a distribution's C library; use a newer"
		say "distribution, or build Objeck from source here: core/release/deploy_posix.sh"
		return 0
	fi
	return 1
}

# A library that is present but older than the one the tree was linked against
# shows up in ldd as "version `GLIBC_2.38' not found". This is Debian 12: every
# soname resolves, so a soname check alone passes, yet nothing starts, because
# its glibc 2.36 predates what the release needs. Prints "<library> <version>".
tree_version_errors() {
	local f
	tree_files "$1" | while IFS= read -r f; do
		ldd "$f" 2>&1 | awk '/: version .* not found/ {
			lib = $2; sub(/:$/, "", lib); n = split(lib, part, "/")
			v = $0; sub(/.*: version ./, "", v); sub(/. not found.*/, "", v)
			print part[n], v }'
	done | sort -u
}

# One line per unresolved dependency: "<soname> <file relative to the tree>".
tree_missing() {
	local tree="$1" f
	tree_files "$tree" | while IFS= read -r f; do
		ldd "$f" 2>/dev/null | awk -v file="${f#"$tree"/}" '/=> not found/ { print $1, file }'
	done
}

report_missing() {
	printf '%s\n' "$1" | awk 'NF { need[$1] = need[$1] " " $2 }
		END { for (s in need) printf "  MISSING  %-30s needed by%s\n", s, need[s] }' | sort
}

# --- soname -> package, per package manager ----------------------------------
#
# apt: Debian names a shared-library package after the SONAME it carries (Debian
# Policy 8.1): lowercase, '_' -> '-', the version appended, with a '-' between
# when the name already ends in a digit. So
#     libmbedtls.so.14       -> libmbedtls14
#     libnghttp2.so.14       -> libnghttp2-14
#     libSDL2-2.0.so.0       -> libsdl2-2.0-0
#     libopencv_core.so.406  -> libopencv-core406
# The 64-bit time_t transition then renamed many of them with a t64 suffix
# (Ubuntu 24.04: libmbedtls14t64, libreadline8t64, libopencv-core406t64) and
# left the old name behind as a virtual package, while Debian 12 still carries
# the plain names. Derive both and let apt say which one really exists.
apt_package_for() {
	local so="$1" base ver name c cand
	case "$so" in *.so.*) ;; *) return 1 ;; esac
	base=$(printf '%s' "${so%%.so.*}" | tr '[:upper:]' '[:lower:]' | tr '_' '-')
	ver="${so#*.so.}"
	case "$base" in
		*[0-9]) name="$base-$ver" ;;
		*)      name="$base$ver" ;;
	esac
	for c in "${name}t64" "$name"; do
		cand=$(apt-cache policy "$c" 2>/dev/null | awk '/Candidate:/ { print $2 }')
		if [ -n "$cand" ] && [ "$cand" != "(none)" ]; then
			printf '%s\n' "$c"
			return 0
		fi
	done
	return 1
}

# RPM records every library a package carries as a capability, e.g.
# "libmbedtls.so.14()(64bit)", so dnf, yum and zypper can be asked directly.
rpm_capability() {
	if [ "$(getconf LONG_BIT 2>/dev/null)" = "64" ]; then
		printf '%s()(64bit)' "$1"
	else
		printf '%s' "$1"
	fi
}

# name-[epoch:]version-release.arch -> name
nevra_name() {
	sed -e 's/\.[^.]*$//' -e 's/-[^-]*-[^-]*$//'
}

package_for() {
	local so="$1" cap
	case "$PM" in
		apt)
			apt_package_for "$so" ;;
		dnf)
			cap=$(rpm_capability "$so")
			dnf -q repoquery --whatprovides "$cap" 2>/dev/null | grep -v '^$' | nevra_name | head -1 ;;
		yum)
			cap=$(rpm_capability "$so")
			if command -v repoquery >/dev/null 2>&1; then
				repoquery -q --whatprovides "$cap" 2>/dev/null | grep -v '^$' | nevra_name | head -1
			else
				yum -q provides "$cap" 2>/dev/null | awk '/ : / { print $1; exit }' | nevra_name
			fi ;;
		zypper)
			cap=$(rpm_capability "$so")
			zypper -n search --provides --match-exact "$cap" 2>/dev/null |
				awk -F'|' '$4 ~ /package/ { gsub(/ /, "", $2); print $2; exit }' ;;
		pacman)
			# needs the files database (pacman -Fy), which pm_refresh fetches
			pacman -Fq "usr/lib/$so" 2>/dev/null | head -1 | sed 's|^.*/||' ;;
		apk)
			# apk resolves "so:" provides itself; unreachable in practice, since
			# a glibc release tree is refused on musl before this is asked
			printf 'so:%s\n' "$so" ;;
	esac
}

# Sets RESOLVED_PKGS and UNRESOLVED from a whitespace-separated soname list.
resolve_packages() {
	local so pkg
	RESOLVED_PKGS=""
	UNRESOLVED=""
	for so in $1; do
		pkg=$(package_for "$so")
		if [ -n "$pkg" ]; then
			case " $RESOLVED_PKGS " in
				*" $pkg "*) ;;
				*) RESOLVED_PKGS="$RESOLVED_PKGS $pkg" ;;
			esac
		else
			UNRESOLVED="$UNRESOLVED $so"
		fi
	done
	RESOLVED_PKGS="${RESOLVED_PKGS# }"
	UNRESOLVED="${UNRESOLVED# }"
}

# Whether the package manager can answer soname questions without a refresh.
pm_metadata_present() {
	case "$PM" in
		# the trailing * matters: with Acquire::GzipIndexes (set in the official
		# Debian and Ubuntu container images) the lists are *_Packages.lz4
		apt)    ls /var/lib/apt/lists/*_Packages* >/dev/null 2>&1 ;;
		pacman) ls /var/lib/pacman/sync/*.files >/dev/null 2>&1 ;;
		*)      return 0 ;;  # dnf, yum and zypper fetch metadata on demand
	esac
}

pm_refresh() {
	case "$PM" in
		apt)    as_root apt-get update ;;
		pacman) as_root pacman -Fy ;;
		*)      return 0 ;;
	esac
}

# A mirror caught mid-sync fails apt-get update with "Hash Sum mismatch" and a
# non-zero exit, leaving whole indices unread (seen on noble/universe while
# testing this). A lookup against that finds no package for most libraries and
# the install goes ahead with a fraction of them. It clears within seconds, so
# try once more before settling for what was fetched.
pm_refresh_checked() {
	case "$PM" in
		apt|pacman) ;;
		*) return 0 ;;
	esac
	say "Refreshing $PM package metadata."
	pm_refresh && return 0
	say "The refresh failed; retrying once."
	sleep 5
	pm_refresh && return 0
	say "Package metadata could not be fully refreshed; some lookups may fail." >&2
	return 1
}

explain_unresolved() {
	local so
	if ! pm_metadata_present; then
		say "  Could not look up packages for:"
		for so in $UNRESOLVED; do say "    $so"; done
		case "$PM" in
			apt)    say "  The package lists have not been downloaded; run 'apt-get update' first." ;;
			pacman) say "  The files database has not been downloaded; run 'pacman -Fy' first." ;;
		esac
		return
	fi
	say "  No package in this system's $PM repositories provides:"
	for so in $UNRESOLVED; do say "    $so"; done
	say "  The distribution was linked against exactly these library versions, and"
	say "  this system's repositories carry different ones (or none). Linux releases"
	say "  are built on Ubuntu 24.04 and run as shipped there. On this distribution,"
	say "  build Objeck from source: core/release/deploy_posix.sh"
}

# The whole Linux run against a distribution tree. Exits.
run_linux_tree() {
	local tree="$1" reason missing sonames cmd round pkgs scope

	if reason=$(tree_unusable_reason "$tree"); then
		say "Cannot run the distribution in $tree on this machine." >&2
		printf '%s\n' "$reason" | sed 's/^/  /' >&2
		exit 1
	fi

	scope="every binary in bin/ and lib/native/libobjk_*.so"
	[ "$SDL_ONLY" -eq 1 ] && scope="obc, obr and libobjk_sdl.so"

	missing=$(tree_missing "$tree")
	sonames=$(printf '%s\n' "$missing" | awk 'NF { print $1 }' | sort -u | tr '\n' ' ')
	sonames="${sonames% }"

	local dev_pkgs=""
	[ "$WANT_DEV" -eq 1 ] && dev_pkgs=$(linux_packages "$PM")

	# --check reports on the runtime only, so --dev does not make it fail; --print
	# and an install still act on the headers.
	if [ -z "$sonames" ] && { [ -z "$dev_pkgs" ] || [ "$CHECK_ONLY" -eq 1 ]; }; then
		if [ "$PRINT_ONLY" -eq 1 ]; then
			say "# nothing to install: every library $tree needs already resolves" >&2
		else
			say "Every system library needed by $scope resolves. Nothing to do."
			say "  (checked $tree)"
		fi
		exit 0
	fi

	if [ "$PRINT_ONLY" -eq 1 ]; then
		resolve_packages "$sonames"
		pkgs="$RESOLVED_PKGS${dev_pkgs:+ $dev_pkgs}"
		pkgs="${pkgs# }"
		if [ -n "$pkgs" ]; then
			cmd=$(linux_install_cmd "$PM" "$pkgs")
			[ "$(id -u)" -eq 0 ] || cmd="sudo $cmd"
			say "$cmd"
		fi
		[ -n "$UNRESOLVED" ] && explain_unresolved >&2
		exit 0
	fi

	if [ -n "$sonames" ]; then
		say "Checked $scope in $tree:"
		report_missing "$missing"
		say ""
	else
		say "Installing development headers (--dev)."
	fi

	if [ "$CHECK_ONLY" -eq 1 ]; then
		resolve_packages "$sonames"
		if [ -n "$RESOLVED_PKGS" ]; then
			cmd=$(linux_install_cmd "$PM" "$RESOLVED_PKGS")
			[ "$(id -u)" -eq 0 ] || cmd="sudo $cmd"
			say "Install them with:"
			say "  $cmd"
			say "or just run:  $0"
			[ -n "$UNRESOLVED" ] && say ""
		fi
		[ -n "$UNRESOLVED" ] && explain_unresolved
		exit 1
	fi

	# No prompt when there is no terminal to prompt on: a piped or CI run would
	# otherwise read EOF and silently take the "no" branch. Decided before any
	# sudo, so a non-interactive run without --yes changes nothing at all.
	if [ "$ASSUME_YES" -eq 0 ] && [ ! -t 0 ]; then
		say "Not a terminal, and --yes was not given. Re-run with --yes to install."
		exit 1
	fi

	# Refresh once, before any lookup, so the names and the install itself both
	# come from current metadata: apt resolves names from its lists, and pacman -F
	# needs its files database.
	pm_refresh_checked

	# Installing a library can expose the next one: ldd cannot see the
	# dependencies of a library it did not find. Package dependencies normally
	# bring those along, so the second and third rounds are a safety net.
	for round in 1 2 3; do
		resolve_packages "$sonames"

		pkgs="$RESOLVED_PKGS${dev_pkgs:+ $dev_pkgs}"
		pkgs="${pkgs# }"
		[ -z "$pkgs" ] && break

		cmd=$(linux_install_cmd "$PM" "$pkgs")
		[ "$(id -u)" -eq 0 ] || cmd="sudo $cmd"

		say ""
		say "About to run:"
		say "  $cmd"
		[ -n "$UNRESOLVED" ] && { say ""; explain_unresolved; }
		say ""

		if [ "$ASSUME_YES" -eq 0 ] && [ "$round" -eq 1 ]; then
			printf 'Proceed? [y/N] '
			read -r reply
			case "$reply" in
				y|Y|yes|YES) ;;
				*) say "Aborted. Re-run with --yes to skip this prompt."; exit 1 ;;
			esac
		fi

		# shellcheck disable=SC2086
		eval "$cmd"
		local status=$?
		if [ "$status" -ne 0 ]; then
			say ""
			say "Package installation failed (exit $status)." >&2
			exit 1
		fi

		dev_pkgs=""
		missing=$(tree_missing "$tree")
		local next
		next=$(printf '%s\n' "$missing" | awk 'NF { print $1 }' | sort -u | tr '\n' ' ')
		next="${next% }"
		# stop once nothing is missing, or nothing changed since the last round
		[ -z "$next" ] && break
		[ "$next" = "$sonames" ] && break
		sonames="$next"
	done

	missing=$(tree_missing "$tree")
	if [ -n "$missing" ]; then
		say ""
		say "Still not resolvable by the loader:" >&2
		report_missing "$missing" >&2
		sonames=$(printf '%s\n' "$missing" | awk 'NF { print $1 }' | sort -u | tr '\n' ' ')
		resolve_packages "$sonames"
		if [ -n "$UNRESOLVED" ]; then
			say "" >&2
			explain_unresolved >&2
		else
			say "Their packages are installed; try 'sudo ldconfig'." >&2
		fi
		exit 1
	fi

	# a library the install just brought in can itself be older than the build
	if reason=$(tree_unusable_reason "$tree"); then
		say "" >&2
		printf '%s\n' "$reason" >&2
		exit 1
	fi

	say ""
	say "Every system library needed by $scope is installed and visible to the loader."
	exit 0
}

# ------------------------------------------------------------------------- main
UNAME=$(uname -s)

case "$UNAME" in
	Darwin)
		say "platform: macOS -- SDL2 is bundled, OpenGL is a system framework"
		say ""
		# find a deploy tree to validate; the repo layout first, then an install
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
		say "platform: Windows -- the SDL2 DLLs ship in bin. Nothing to install."
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
	say "Install what 'ldd bin/obr' and 'ldd lib/native/libobjk_*.so' report as \"not found\" by hand." >&2
	exit 2
fi

# The distribution to inspect: an explicit --tree, else the one this script sits
# at the root of (that is where it ships), else a repo build or an install.
TREE=""
if [ -n "$TREE_ARG" ]; then
	if [ ! -f "$TREE_ARG/bin/obr" ]; then
		say "No Objeck distribution at $TREE_ARG (expected $TREE_ARG/bin/obr)." >&2
		exit 1
	fi
	TREE=$(cd "$TREE_ARG" && pwd)
else
	for c in "$SELF_DIR" "$REPO/core/release/deploy" "/usr/local/objeck-lang"; do
		[ -f "$c/bin/obr" ] && { TREE="$c"; break; }
	done
fi

[ -n "$TREE" ] && run_linux_tree "$TREE"

# No distribution to inspect -- a repo checkout that has not been built yet. The
# toolchain's own libraries cannot be known without its binaries, so fall back
# to the SDL2/OpenGL set, which is all --dev (building libobjk_sdl.so) needs.
say "No Objeck distribution found (looked beside this script, in core/release/deploy"
say "and /usr/local/objeck-lang); checking only the SDL2 and OpenGL libraries."
say "Point --tree at a distribution to check everything it needs."
say ""

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
	as_root apt-get update
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
