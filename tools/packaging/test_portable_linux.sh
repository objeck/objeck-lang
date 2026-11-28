#!/usr/bin/env bash
#
# test_portable_linux.sh [--natives] <objeck-linux-ARCH_VERSION.tgz> [image...]
#
# Run a Linux release tarball on each distribution it promises to run on, as a
# clean container with nothing installed: whatever the tarball needs and the
# base image lacks fails here, not on a user's machine. The images default to
# the list in core/release/runtime_deps.json (linux.release.tested_images).
# Design: docs/design/PORTABLE_LINUX.md. Needs docker and openssl.
#
# The tarball is unpacked here and mounted read-only, so not even tar is asked
# of the image. In each one, tools/packaging/portable-linux/smoke.sh:
#
#   1. every tool in bin/ resolves its libraries and reports its version
#   2. compiles and runs a program
#   3. makes an HTTPS request to a TLS server it starts on the loopback, with a
#      certificate made here: accepted when trusted, refused when not
#   4. compiles and runs the first 20 shipped examples (JSON, XML, CSV, regex,
#      SQL, threads, files, SHA-256 through libobjk_crypto among them)
#   5. the self-contained native libraries load; one whose system library is
#      absent is refused with a message, and obr exits 1 rather than crashing
#   6. obd reads commands with no readline installed
#
# --natives runs tools/packaging/portable-linux/natives.sh instead, with the
# network on: the tarball's install_deps.sh must find a package for every system
# library the optional bindings use, after which every native library must
# resolve to the last symbol and the OpenCV, LAME and ONNX bindings must run.
# Its default images leave out linux.release.natives_untested, and on arm64
# both modes leave out x64_only_images.
#
# Before any image runs, tools/packaging/check_portable_archive.sh unpacks the tarball into a fresh
# tree and holds it to check_docs_deps.py --portable; the archive's sha256 is printed up front and in
# the final PASS line, and the smoke fails if the file changed under it.
#
# DOCKER="sudo docker" where docker needs root. Exit 0 only if every image passed.

set -euo pipefail

die() { echo "test_portable_linux.sh: $*" >&2; exit 1; }

MODE=smoke
if [ "${1:-}" = --natives ]; then MODE=natives; shift; fi
TGZ="${1:?usage: test_portable_linux.sh [--natives] <objeck-linux-ARCH_VERSION.tgz> [image...]}"
shift
[ -f "$TGZ" ] || die "no such file: $TGZ"
REPO="$(cd "$(dirname "$0")/../.." && pwd)"
if [ $# -gt 0 ]; then
  IMAGES=("$@")
else
  # Not `mapfile < <(python3 ...)`: a process substitution's exit status is lost, so a
  # selector that crashed gave an empty list and a PASS on no distributions.
  selected=$(python3 -c '
import json, sys
release = json.load(open(sys.argv[1]))["linux"]["release"]
skip = list(release["natives_untested"]) if sys.argv[2] == "natives" else []
if sys.argv[3] != "x86_64":
    skip += release["x64_only_images"]
print("\n".join(i for i in release["tested_images"] if i not in skip))' \
    "$REPO/core/release/runtime_deps.json" "$MODE" "$(uname -m)") || die "could not read the image list from core/release/runtime_deps.json"
  IMAGES=()
  while IFS= read -r image; do [ -z "$image" ] || IMAGES+=("$image"); done <<< "$selected"
fi
[ ${#IMAGES[@]} -gt 0 ] || die "no distributions selected ($MODE mode on $(uname -m)); refusing to report a pass on none"
read -r -a DOCKER <<< "${DOCKER:-docker}"
"${DOCKER[@]}" version >/dev/null 2>&1 || die "docker is not usable as '${DOCKER[*]}' (set DOCKER=\"sudo docker\"?)"
command -v openssl >/dev/null || die "needs openssl, for the test certificate"

WORK="$(mktemp -d)"
trap 'rm -rf "$WORK"' EXIT
mkdir -p "$WORK/pkg" "$WORK/fixture"

# Bind the smoke to the archive the portable gate inspected: check_portable_archive.sh unpacks THIS
# file into a fresh tree and runs check_docs_deps.py --portable on it, then prints its sha256. The
# distros below run the same bytes, and the hash is re-taken at the end, so a PASS here and a PASS
# there are about one file. (What the smoke tests is unchanged.)
case "$(basename "$TGZ")" in
  objeck-linux-arm64_*) ARCH=arm64 ;;
  objeck-linux-x64_*|objeck-linux-x86_64_*) ARCH=x64 ;;
  *) case "$(uname -m)" in aarch64|arm64) ARCH=arm64 ;; *) ARCH=x64 ;; esac ;;
esac
"$REPO/tools/packaging/check_portable_archive.sh" --sha256-out "$WORK/archive.sha256" "$TGZ" "$ARCH" \
  || die "the portable archive check failed; not running the distribution smoke on an archive that fails it"
ARCHIVE_SHA256="$(cat "$WORK/archive.sha256")"
echo "smoke runs archive sha256 $ARCHIVE_SHA256"

tar -xzf "$TGZ" -C "$WORK/pkg"
[ -x "$WORK/pkg/objeck-lang/bin/obr" ] || die "$(basename "$TGZ") has no objeck-lang/bin/obr"
cp "$REPO/tools/packaging/portable-linux/smoke.sh" "$REPO/tools/packaging/portable-linux/natives.sh" \
   "$REPO/tools/packaging/portable-linux/tls_smoke.obs" "$WORK/fixture/"
for t in core_opencv lame_encode_test onnx_runtime_test; do
  cp "$REPO/programs/regression/$t.obs" "$WORK/fixture/" || die "programs/regression/$t.obs is missing"
done

# A certificate for DNS:localhost, valid from 2025 rather than from this second:
# a clock that steps back (a VM resynchronising) would otherwise see one that is
# not valid yet. Only `openssl ca` can set the start before OpenSSL 3.4.
( cd "$WORK/fixture"
  mkdir ca && : > ca/index.txt && echo 01 > ca/serial
  cat > ca/openssl.cnf <<'EOF'
[req]
distinguished_name = dn
[dn]
[ca]
default_ca = fixture
[fixture]
database = ca/index.txt
serial = ca/serial
new_certs_dir = ca
policy = any
default_md = sha256
unique_subject = no
[any]
commonName = supplied
[server]
basicConstraints = critical,CA:TRUE
subjectKeyIdentifier = hash
subjectAltName = DNS:localhost
EOF
  openssl req -new -newkey rsa:2048 -nodes -config ca/openssl.cnf -subj "/CN=localhost" \
    -keyout key.pem -out ca/request.csr 2>/dev/null
  openssl ca -batch -selfsign -notext -config ca/openssl.cnf -extensions server -keyfile key.pem \
    -in ca/request.csr -startdate 250101000000Z -enddate 491231235959Z -out cert.pem 2>/dev/null
  rm -rf ca
  [ -s cert.pem ] && [ -s key.pem ] ) || die "openssl could not make the test certificate"
chmod -R a+rX "$WORK"

FAILED=()
for image in "${IMAGES[@]}"; do
  echo
  echo "================================================================ $image"
  mounts=(-v "$WORK/pkg/objeck-lang:/pkg:ro" -v "$WORK/fixture:/fixture:ro")
  if [ "$MODE" = natives ]; then
    run=("${DOCKER[@]}" run --rm "${mounts[@]}" "$image" bash /fixture/natives.sh)
  else
    run=("${DOCKER[@]}" run --rm --network none "${mounts[@]}" "$image" sh /fixture/smoke.sh)
  fi
  if "${run[@]}"; then
    echo "PASS $image"
  else
    echo "FAIL $image"
    FAILED+=("$image")
  fi
done

echo
if [ ${#FAILED[@]} -gt 0 ]; then
  echo "FAILED on: ${FAILED[*]}"
  exit 1
fi
if command -v sha256sum >/dev/null 2>&1; then AFTER_SHA="$(sha256sum "$TGZ" | cut -d' ' -f1)"; else AFTER_SHA="$(shasum -a 256 "$TGZ" | cut -d' ' -f1)"; fi
[ "$AFTER_SHA" = "$ARCHIVE_SHA256" ] || die "$(basename "$TGZ") changed during the smoke ($ARCHIVE_SHA256 -> $AFTER_SHA)"
echo "PASS ($MODE) $(basename "$TGZ") sha256 $ARCHIVE_SHA256 on ${#IMAGES[@]} distributions: ${IMAGES[*]}"
