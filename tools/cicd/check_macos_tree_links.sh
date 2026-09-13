#!/bin/bash
#
# Checks that every Mach-O file in a macOS Objeck tree -- core/release/deploy,
# or a package's payload -- loads its libraries from that tree or the OS,
# following rpaths the way dyld will on a user's Mac. It fails on:
#
#   - an LC_RPATH outside the tree, such as /opt/homebrew/lib
#   - an @rpath dependency no in-tree rpath reaches. dyld tries the file's own
#     rpaths, then those of whatever loaded it: a tree library that links it by
#     install name, or bin/obr, which dlopens the bindings
#   - an @loader_path or @executable_path dependency that is not there
#   - an absolute dependency outside /usr/lib and /System
#
# Why this exists: v2026.9.2's obr loaded an ngtcp2 GnuTLS backend through its
# only LC_RPATH, /opt/homebrew/lib, where the Mac that built it had a hand-built
# copy. The deploy's other checks read obr's absolute links and the bindings'
# install names, and neither sees an rpath. tools/cicd/macos_install_test.sh
# finds the same problems by installing the package on a clean runner; this
# finds them where the tree is built, before it is signed or archived.
#
# Usage: tools/cicd/check_macos_tree_links.sh <tree>
# Env:
#   ALLOW_EXTERNAL  space-separated file basenames (libobjk_onnx.dylib) whose
#                   links may still resolve outside the tree, while that binding
#                   is being bundled -- the variable macos_install_test.sh reads
#
# Exits 0 when every link resolves (allowed files aside), 1 when one does not,
# and 2 on a usage error.

set -u
[ $# -eq 1 ] || { echo "usage: $0 <tree>" >&2; exit 2; }
TREE=$(cd "$1" 2>/dev/null && pwd -P) || { echo "no such tree: $1" >&2; exit 2; }
ALLOW=" ${ALLOW_EXTERNAL:-} "
allowed() { case "$ALLOW" in *" $1 "*) return 0 ;; esac; return 1; }
WORK=$(mktemp -d)
trap 'rm -rf "$WORK"' EXIT

find "$TREE" -type f \( -perm -u+x -o -name '*.dylib' \) > "$WORK/candidates"
: > "$WORK/macho"
while IFS= read -r f; do
	file -b "$f" | grep -q 'Mach-O' && printf '%s\n' "$f" >> "$WORK/macho"
done < "$WORK/candidates"

# file<TAB>install-name basename, and file<TAB>dependency, for every Mach-O file
: > "$WORK/ids"; : > "$WORK/links"; : > "$WORK/allowed"
while IFS= read -r f; do
	id=$(otool -D "$f" | tail -n +2)
	printf '%s\t%s\n' "$f" "$(basename "${id:-$f}")" >> "$WORK/ids"
	otool -L "$f" | tail -n +2 | awk '{print $1}' > "$WORK/deps"
	while IFS= read -r n; do
		[ -n "$id" ] && [ "$n" = "$id" ] && continue
		printf '%s\t%s\n' "$f" "$n" >> "$WORK/links"
	done < "$WORK/deps"
done < "$WORK/macho"

rpaths_of() {
	otool -l "$1" | awk '/cmd LC_RPATH/{r=1} r && / path /{print $2; r=0}' \
		| sed "s#@loader_path#$(dirname "$1")#g; s#@executable_path#$TREE/bin#g"
}
in_tree() {
	local p
	p=$(cd "$1" 2>/dev/null && pwd -P) || p="$1"
	case "$p" in "$TREE"|"$TREE"/*) return 0 ;; esac
	return 1
}

bad=0
report() {   # file message
	if allowed "$(basename "$1")"; then
		basename "$1" >> "$WORK/allowed"
	else
		echo "  ERROR  ${1#$TREE/}: $2"
		bad=$((bad + 1))
	fi
}

while IFS= read -r f; do
	rpaths_of "$f" > "$WORK/rp"
	while IFS= read -r rp; do
		in_tree "$rp" || report "$f" "LC_RPATH outside the tree: $rp"
	done < "$WORK/rp"
done < "$WORK/macho"

while IFS=$'\t' read -r f n; do
	base=$(basename "$n")
	found=""
	case "$n" in
		/usr/lib/*|/System/*) continue ;;
		@rpath/*)
			key=$(awk -F'\t' -v f="$f" '$1 == f {print $2}' "$WORK/ids")
			awk -F'\t' -v b="$key" '{ x = $2; sub(/.*\//, "", x); if (x == b) print $1 }' "$WORK/links" > "$WORK/loaders"
			printf '%s\n%s\n' "$f" "$TREE/bin/obr" >> "$WORK/loaders"
			while IFS= read -r src; do
				[ -f "$src" ] || continue
				rpaths_of "$src" > "$WORK/srp"
				while IFS= read -r rp; do
					in_tree "$rp" && [ -e "$rp/$base" ] && found=1
				done < "$WORK/srp"
			done < "$WORK/loaders"
			;;
		@loader_path/*) [ -e "$(dirname "$f")/${n#@loader_path/}" ] && found=1 ;;
		@executable_path/*) [ -e "$TREE/bin/${n#@executable_path/}" ] && found=1 ;;
		"$TREE"/*) [ -e "$n" ] && found=1 ;;
	esac
	[ -n "$found" ] || report "$f" "links $n, which does not resolve inside the tree"
done < "$WORK/links"

count=$(wc -l < "$WORK/macho" | tr -d ' ')
if [ -s "$WORK/allowed" ]; then
	echo "  allowed while their bundling is pending (ALLOW_EXTERNAL):"
	sort "$WORK/allowed" | uniq -c | awk '{printf "    %s: %s link(s) outside the tree\n", $2, $1}'
fi
if [ "$bad" -eq 0 ]; then
	echo "all $count Mach-O files resolve inside the tree or macOS"
	exit 0
fi
echo "$bad problem(s) in $count Mach-O files"
exit 1
