#!/bin/bash
#
# Installs an Objeck macOS .pkg on THIS machine and checks what a user gets.
# Meant for a clean CI runner, never a developer Mac: it runs `sudo installer`
# and briefly moves /opt/homebrew aside.
#
#   1. install layout: receipt, /usr/local/objeck-lang, /etc/paths.d/objeck
#   2. every Mach-O file in the package resolves its libraries INSIDE the
#      package or the OS -- a link into /opt/homebrew or /usr/local fails even
#      when this runner happens to have the file, because a user's Mac will not
#   3. with Homebrew moved aside, a fresh login shell finds obc and
#      OBJECK_LIB_PATH, obr runs a program, both HTTP engines are compiled in,
#      and the OpenCV and ONNX regression tests pass
#
# Why this exists: v2026.6.3 through v2026.9.2 shipped an obr that could not
# start on a user's Mac. It linked Homebrew's GnuTLS and an ngtcp2 GnuTLS
# backend that Homebrew does not ship, and the only machine it ever ran on had
# a hand-built copy.
#
# Usage: tools/cicd/macos_install_test.sh <objeck.pkg> <programs/regression dir>
# Env:
#   ALLOW_EXTERNAL  space-separated package file basenames (libobjk_onnx.dylib)
#                   whose links may still resolve outside the package, while
#                   that binding is being bundled; tests that need them are
#                   reported as not run instead of failed
#   REQUIRE_SIGNED  1 = obr must pass Gatekeeper (release packages)

set -u
PKG="${1:?usage: macos_install_test.sh <pkg> <regression dir>}"
REG="${2:?usage: macos_install_test.sh <pkg> <regression dir>}"
ROOT=/usr/local/objeck-lang
ALLOW_EXTERNAL="${ALLOW_EXTERNAL:-}"
SUMMARY="${GITHUB_STEP_SUMMARY:-/dev/null}"
WORK=$(mktemp -d)
FAILURES=()
HOMEBREW_HIDDEN=""

pass() { echo "  PASS  $1"; echo "| ✅ | $1 |" >> "$SUMMARY"; }
fail() { echo "  FAIL  $1"; echo "| ❌ | $1 |" >> "$SUMMARY"; FAILURES+=("$1"); }
note() { echo "  NOTE  $1"; echo "| ⏭️ | $1 |" >> "$SUMMARY"; }
allowed() { case " $ALLOW_EXTERNAL " in *" $1 "*) return 0 ;; esac; return 1; }

restore_homebrew() {
	if [ -n "$HOMEBREW_HIDDEN" ]; then
		sudo mv /opt/homebrew.install-test /opt/homebrew
		HOMEBREW_HIDDEN=""
	fi
}
trap restore_homebrew EXIT

# A new Terminal window: no inherited environment, login zsh.
fresh() { env -i HOME="$HOME" USER="$USER" TERM=dumb /bin/zsh -l -c "$1"; }

{
	echo "### macOS $(sw_vers -productVersion) install test: $(basename "$PKG")"
	echo ""
	echo "| | check |"
	echo "|---|---|"
} >> "$SUMMARY"

echo "== install"
if sudo installer -pkg "$PKG" -target / ; then pass "installer -pkg"; else fail "installer -pkg"; fi
pkgutil --pkg-info org.objeck.lang >/dev/null 2>&1 && pass "receipt org.objeck.lang" || fail "receipt org.objeck.lang"
[ -x "$ROOT/bin/obc" ] && [ -x "$ROOT/bin/obr" ] && pass "$ROOT/bin/{obc,obr}" || fail "$ROOT/bin/{obc,obr} missing"
[ "$(cat /etc/paths.d/objeck 2>/dev/null)" = "$ROOT/bin" ] && pass "/etc/paths.d/objeck" || fail "/etc/paths.d/objeck"

echo "== library resolution (inside the package only)"
# file<TAB>install-name for every Mach-O file in the tree
MACHO=()
while IFS= read -r f; do
	file -b "$f" | grep -q 'Mach-O' && MACHO+=("$f")
done < <(find "$ROOT" -type f \( -perm -u+x -o -name '*.dylib' \))
: > "$WORK/links"
: > "$WORK/ids"
for f in "${MACHO[@]}"; do
	id=$(otool -D "$f" | tail -n +2)
	printf '%s\t%s\n' "$f" "$(basename "${id:-$f}")" >> "$WORK/ids"
	otool -L "$f" | tail -n +2 | awk '{print $1}' | while read -r n; do
		[ -n "$id" ] && [ "$n" = "$id" ] && continue
		printf '%s\t%s\n' "$f" "$n"
	done >> "$WORK/links"
done
rpaths_of() { otool -l "$1" | awk '/cmd LC_RPATH/{r=1} r && / path /{print $2; r=0}' | sed "s#@loader_path#$(dirname "$1")#g; s#@executable_path#$ROOT/bin#g"; }

unresolved=0
while IFS=$'\t' read -r f n; do
	base=$(basename "$n")
	case "$n" in
		/usr/lib/*|/System/*) continue ;;
		@rpath/*)
			# dyld searches the file's own LC_RPATHs, then those of whatever loaded
			# it: obr, or a package library that links this one. Loaders are found
			# by install name, so an unversioned copy (libSDL2_image.dylib) is
			# judged by what loads its versioned id.
			found=""
			key=$(awk -F'\t' -v f="$f" '$1 == f {print $2}' "$WORK/ids")
			loaders=$(awk -F'\t' -v b="$key" '{ n=$2; sub(/.*\//, "", n); if (n == b) print $1 }' "$WORK/links")
			for src in "$f" $loaders "$ROOT/bin/obr"; do
				for rp in $(rpaths_of "$src"); do
					case "$rp" in "$ROOT"/*) [ -e "$rp/$base" ] && found=1 ;; esac
				done
			done
			;;
		@loader_path/*) [ -e "$(dirname "$f")/${n#@loader_path/}" ] && found=1 || found="" ;;
		"$ROOT"/*) [ -e "$n" ] && found=1 || found="" ;;
		*) found="" ;;   # /opt/homebrew, /usr/local, anything a user's Mac may lack
	esac
	if [ -z "$found" ]; then
		if allowed "$(basename "$f")"; then
			echo "    allowed external: ${f#$ROOT/} -> $n"
		else
			echo "    UNRESOLVED: ${f#$ROOT/} -> $n"
			unresolved=$((unresolved + 1))
		fi
	fi
done < "$WORK/links"
[ "$unresolved" = 0 ] && pass "all ${#MACHO[@]} Mach-O files resolve inside the package" \
	|| fail "$unresolved library link(s) resolve outside the package"

gk=$(spctl --assess --type execute -vv "$ROOT/bin/obr" 2>&1 | tr '\n' ' ')
if spctl --assess --type execute "$ROOT/bin/obr" 2>/dev/null; then
	pass "Gatekeeper accepts obr ($gk)"
elif [ "${REQUIRE_SIGNED:-0}" = 1 ]; then
	fail "Gatekeeper rejects obr ($gk)"
else
	note "Gatekeeper: $gk"
fi

echo "== run with Homebrew moved aside"
if [ -d /opt/homebrew ]; then
	sudo mv /opt/homebrew /opt/homebrew.install-test && HOMEBREW_HIDDEN=1
fi

shell_out=$(fresh 'printf "%s|%s" "$(command -v obc)" "$OBJECK_LIB_PATH"' 2>&1)
[ "$shell_out" = "$ROOT/bin/obc|$ROOT/lib" ] && pass "fresh login shell: obc on PATH, OBJECK_LIB_PATH set" \
	|| fail "fresh login shell: got '$shell_out'"

ver=$(fresh 'obc --version' 2>&1 | head -1)
[[ "$ver" == *Objeck* ]] && pass "obc --version: $ver" || fail "obc --version: $ver"

printf 'class Hello { function : Main(args : String[]) ~ Nil { "hello from obr"->PrintLine(); } }\n' > "$WORK/hello.obs"
out=$(cd "$WORK" && fresh "obc -src hello.obs -dest hello.obe && obr hello.obe" 2>&1)
[[ "$out" == *"hello from obr"* ]] && pass "obr runs a program" || { fail "obr runs a program"; echo "$out" | tail -5 | sed 's/^/    /'; }

run_regression() {   # name, needs (space-separated basenames that must be bundled)
	local name="$1" needs="$2" src="$REG/$1.obs" extra libs out rc lib
	for lib in $needs; do
		if allowed "$lib"; then note "$name: not run, $lib is not bundled yet"; return; fi
	done
	extra=$(grep -m1 '# EXTRA_LIBS:' "$src" | sed 's/.*# EXTRA_LIBS:[[:space:]]*//')
	libs="cipher,collect,xml,json${extra:+,$extra}"
	sed '1s/^\xEF\xBB\xBF//' "$src" > "$WORK/$name.obs"
	out=$(cd "$REG" && fresh "obc -src '$WORK/$name.obs' -lib $libs -opt s3 -dest '$WORK/$name.obe' && obr '$WORK/$name.obe'" 2>&1); rc=$?
	if [ $rc -eq 0 ] && ! grep -q 'FAIL' <<< "$out"; then
		pass "$name"
	else
		fail "$name (exit $rc)"; echo "$out" | tail -15 | sed 's/^/    /'
	fi
}
run_regression runtime_feature_test ""
out=$(cd "$WORK" && printf 'class F { function : Main(a : String[]) ~ Nil { System.Runtime->GetProperty("runtime.feature.http2")->PrintLine(); System.Runtime->GetProperty("runtime.feature.http3")->PrintLine(); } }\n' > f.obs && fresh "obc -src f.obs -dest f.obe && obr f.obe" 2>&1 | tail -2 | tr '\n' ' ')
[ "$out" = "1 1 " ] && pass "HTTP/2 and HTTP/3 compiled into obr" || fail "runtime.feature.http2/http3 = '$out'"
run_regression core_opencv "libobjk_opencv.dylib"
run_regression onnx_runtime_test "libobjk_onnx.dylib"

restore_homebrew
echo ""
if [ ${#FAILURES[@]} -eq 0 ]; then
	echo "macOS install test: all checks pass"
	exit 0
fi
echo "macOS install test: ${#FAILURES[@]} failure(s):"
printf '  - %s\n' "${FAILURES[@]}"
exit 1
