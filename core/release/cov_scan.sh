#!/bin/sh
# Coverity Scan upload, Linux (and WSL). Manual developer script -- CI does not
# run this. Companion to cov_scan.cmd, which covers the Windows build.
#
# Usage:  ./cov_scan.sh [x64|arm64]         (default x64; any directory)
#
#   COVERITY_DRY_RUN=1 ./cov_scan.sh        build, capture and verify, then STOP
#                                           before uploading. Needs no token.
#   COVERITY_UPLOAD_ONLY=1 ./cov_scan.sh    skip the rebuild; verify and upload
#   COVERITY_FORCE_LARGE=1 ./cov_scan.sh    use the large-submission flow even
#                                           when the archive would fit the form
#                                           the archive a dry run left behind.
#
# The two modes exist so a capture can be checked before a scan is spent on it,
# and so an upload can be retried without another full rebuild.
#
# ---------------------------------------------------------------------------
# Token -- never in the tree:
#
#   export COVERITY_TOKEN=...       from scan.coverity.com -> Project Settings
#   export COVERITY_TOKEN_FILE=...  or a file containing just the token
#
# COVERITY_TOKEN wins when both are set. With neither, $HOME/Documents/Code/
# cov_token.dat is read; under WSL, where $HOME is the Linux home, the same path
# under the Windows user profile is tried next, so the one file serves both
# cov_scan.sh and cov_scan.cmd. Keep it OUTSIDE the repository -- an in-tree token
# is one 'git add -A' from being published, which is exactly how the original
# leaked (public 2019-08-11 to 2026-08, and now rotated). Never paste the value
# back into this file.
#
# ---------------------------------------------------------------------------
# Toolchain:
#
#   export COVERITY_HOME=...      install root -- the dir holding bin/cov-build
#
# Falls back to PATH when COVERITY_HOME is unset. This needs the LINUX build tool
# (cov-analysis-linux64-*); the Windows one cannot run under WSL.
#
# ---------------------------------------------------------------------------
# What is captured: a full deploy_posix.sh build, so the native libraries
# (crypto, lame, diags, odbc, onnx, opencv) are analysed as well as the
# compiler, VM and tools. The Windows scan's default does not reach them, which
# makes this the more complete of the two; the 2026-08-17 ONNX findings came
# from here. deploy_posix.sh does a clean rebuild, which cov-build needs: it can
# only capture translation units that actually compile.
#
# The submitted version is read from version.h, never hardcoded -- a literal
# would keep reporting a stale version to Coverity after every bump.

DRY_RUN=${COVERITY_DRY_RUN:-0}
UPLOAD_ONLY=${COVERITY_UPLOAD_ONLY:-0}
ARCH=${1:-x64}

fail() {
  echo "ERROR: $*" >&2
  exit 1
}

case "$ARCH" in
  x64|arm64) ;;
  *) fail "unknown architecture '$ARCH' -- use x64 or arm64." ;;
esac

if [ "$DRY_RUN" = "1" ] && [ "$UPLOAD_ONLY" = "1" ]; then
  fail "COVERITY_DRY_RUN and COVERITY_UPLOAD_ONLY together would do nothing -- pick one."
fi

# Work from core/release whatever the caller's directory: deploy_posix.sh and the
# relative path to version.h both assume it.
cd "$(dirname "$0")" || fail "cannot cd to the script's directory."

# Staging. The emit directory is large and transient, so it lives in /tmp. The
# archive stays here, where .gitignore already covers it, because it must outlive
# the capture for COVERITY_UPLOAD_ONLY: WSL clears /tmp when the distribution
# restarts, and an idle WSL distribution shuts down within minutes.
COV_DIR=/tmp/cov-int
COV_LOG=/tmp/objeck-cov-build.log
ARCHIVE=objeck-int.tgz

# ----------------------------------------------------------------- token ----
# Under WSL, the Windows user profile as a Linux path, or nothing. cmd.exe needs
# a Windows working directory or it warns about UNC paths, hence the cd.
wsl_windows_profile() {
  grep -qi microsoft /proc/version 2>/dev/null || return 1
  command -v wslpath >/dev/null 2>&1 || return 1
  command -v cmd.exe >/dev/null 2>&1 || return 1
  _profile=$(cd /mnt/c 2>/dev/null && cmd.exe /c 'echo %USERPROFILE%' 2>/dev/null | tr -d '\r')
  [ -n "$_profile" ] || return 1
  wslpath -u "$_profile" 2>/dev/null
}

read_token() {
  [ -n "$COVERITY_TOKEN" ] && return 0

  if [ -z "$COVERITY_TOKEN_FILE" ]; then
    COVERITY_TOKEN_FILE="$HOME/Documents/Code/cov_token.dat"
    if [ ! -r "$COVERITY_TOKEN_FILE" ]; then
      _win=$(wsl_windows_profile) && [ -r "$_win/Documents/Code/cov_token.dat" ] &&
        COVERITY_TOKEN_FILE="$_win/Documents/Code/cov_token.dat"
    fi
  fi

  if [ -r "$COVERITY_TOKEN_FILE" ]; then
    # A file written on Windows ends its line in CRLF. $(...) strips the \n but
    # not the \r, and a token carrying a trailing \r is rejected by the upload as
    # an authentication failure -- so strip all whitespace; a token has none.
    COVERITY_TOKEN=$(tr -d ' \t\r\n' < "$COVERITY_TOKEN_FILE")
  fi

  [ -n "$COVERITY_TOKEN" ] && return 0

  echo "ERROR: no Coverity token. Export COVERITY_TOKEN, or put the token in" >&2
  echo "       $COVERITY_TOKEN_FILE (override that path with COVERITY_TOKEN_FILE)." >&2
  exit 1
}

# A dry run never uploads, so it never needs the token. Everything else checks for
# it now, before a rebuild that takes many minutes, rather than after.
[ "$DRY_RUN" = "1" ] || read_token

# --------------------------------------------------------------- version ----
version_h=../shared/version.h
[ -r "$version_h" ] || fail "cannot read $version_h -- is this a complete checkout?"
version=$(sed -nE 's/^#define[[:space:]]+VERSION_STRING[[:space:]]+L?"([^"]+)".*/\1/p' "$version_h")
[ -n "$version" ] || fail "no VERSION_STRING found in $version_h -- has the define been renamed?"

echo
echo "============================================================"
echo " Coverity Scan: Objeck $version (Linux $ARCH)"
[ "$DRY_RUN" = "1" ]     && echo " mode: DRY RUN -- capture and verify only, no upload"
[ "$UPLOAD_ONLY" = "1" ] && echo " mode: UPLOAD ONLY -- no rebuild"
echo "============================================================"

# ------------------------------------------------------- build and capture ---
if [ "$UPLOAD_ONLY" != "1" ]; then
  if [ -n "$COVERITY_HOME" ]; then
    cov_build="$COVERITY_HOME/bin/cov-build"
    cov_manage="$COVERITY_HOME/bin/cov-manage-emit"
    [ -x "$cov_build" ] || fail "no executable cov-build at $cov_build -- check COVERITY_HOME."
  elif cov_build=$(command -v cov-build); then
    cov_manage=$(command -v cov-manage-emit)
  else
    fail "cov-build not found. Set COVERITY_HOME to your Coverity install root, or put its bin/ directory on PATH."
  fi

  echo " cov-build : $cov_build"
  echo " build     : ./deploy_posix.sh $ARCH"
  echo " intdir    : $COV_DIR"
  echo

  rm -rf "$COV_DIR"
  rm -f "$ARCHIVE" "$COV_LOG"

  echo "Building under cov-build (full rebuild, this takes a while)..."
  "$cov_build" --dir "$COV_DIR" ./deploy_posix.sh "$ARCH" > "$COV_LOG" 2>&1
  build_rc=$?
  grep -iE 'compilation units|ready for analysis|No files were emitted' "$COV_LOG"

  # The build's own status first. Without this, a failed build was still archived
  # and uploaded -- a partial or empty capture, reported to Coverity as if whole.
  if [ "$build_rc" -ne 0 ]; then
    echo >&2
    fail "the build failed under cov-build (exit $build_rc). Full log: $COV_LOG"
  fi

  # A build that compiled nothing still exits 0 and still uploads. Coverity accepts
  # an empty emit and reports a CLEAN scan, which is indistinguishable from having
  # no defects. cov-build does not say "0 compilation units"; it prints the warning
  # matched here. The emit directory exists even when nothing was captured, and
  # cov-manage-emit exits 0 on an empty emit, so only the warning and the line
  # count tell the two apart (the same two checks cov_scan.cmd makes).
  if grep -q 'No files were emitted' "$COV_LOG"; then
    fail "cov-build emitted no files -- nothing would be analysed. Not uploading. Log: $COV_LOG"
  fi

  if [ -n "$cov_manage" ] && [ -x "$cov_manage" ]; then
    # Count the "Translation unit:" headers, not lines: list prints two lines per
    # unit (the header, then "N -> path"), so wc -l reported double.
    tu_count=$("$cov_manage" --dir "$COV_DIR" list 2>/dev/null | grep -c '^Translation unit:')
    [ "${tu_count:-0}" -gt 0 ] ||
      fail "the emit contains 0 translation units -- nothing would be analysed. Not uploading. Log: $COV_LOG"
    echo "Captured $tu_count translation unit(s)."
  else
    echo "WARNING: cov-manage-emit not found beside cov-build; translation units not counted."
  fi

  # A capture short of 100% means whole libraries were skipped, and a library that
  # was never analysed is reported as clean. Say so loudly rather than submit
  # silently -- the same blind spot as an empty emit, just harder to notice.
  if ! grep -q '(100%)' "$COV_LOG"; then
    echo
    echo "WARNING: capture is INCOMPLETE -- not every compilation unit was emitted."
    grep -i 'compilation units' "$COV_LOG"
    echo "         Code missing from the emit is reported as clean because it was never"
    echo "         analysed. Check $COV_DIR/build-log.txt before trusting the results."
    echo
  fi

  echo
  echo "Archiving the intermediate directory..."
  # -C into /tmp and add the bare name, so the archive holds cov-int/... and not an
  # absolute or differently-named path: that is the shape Coverity Scan unpacks.
  tar -czf "$ARCHIVE" -C /tmp cov-int || fail "tar failed."
fi

# ---------------------------------------------------------------- archive ----
[ -f "$ARCHIVE" ] || fail "no $ARCHIVE in $(pwd) -- run COVERITY_DRY_RUN=1 ./cov_scan.sh first."

# A wrongly-named or nested directory uploads with HTTP 200 and only fails later,
# server-side, where the cause is invisible from here. Check before spending an upload.
if ! tar -tzf "$ARCHIVE" 2>/dev/null | grep -q '^cov-int/'; then
  fail "$ARCHIVE has no top-level cov-int/ directory. Coverity Scan would accept the upload and then fail to analyse it."
fi
echo "Archive verified: $(pwd)/$ARCHIVE ($(du -h "$ARCHIVE" | cut -f1))"

# The single-shot form upload is documented only up to 500 MB, and this capture
# grows with the codebase: 13 translation units in early 2026, 72 by 2026-09,
# 174 MB compressed. Past the limit the form endpoint does not say "too large"
# -- it fails in a way that reads as a rejected token or a network error, on a
# script whose whole job is to be trusted about why an upload did not happen.
#
# So the size decides the route: the form under the limit, and Coverity's
# three-step flow for large submissions above it (initialize the build, PUT the
# tarball to the returned URL, then enqueue it). Measured here, where the
# archive is already in hand and a dry run can report the headroom.
COV_FORM_MAX_BYTES=524288000                       # 500 MB, the documented limit
archive_bytes=$(wc -c < "$ARCHIVE" | tr -d ' ')
archive_pct=$(( archive_bytes * 100 / COV_FORM_MAX_BYTES ))
if [ "$archive_bytes" -gt "$COV_FORM_MAX_BYTES" ]; then
  UPLOAD_FLOW=large
  echo "Upload size: ${archive_pct}% of the 500 MB single-shot limit -- using the large-submission flow."
elif [ "$archive_pct" -ge 80 ]; then
  UPLOAD_FLOW=form
  echo "WARNING: archive is ${archive_pct}% of the 500 MB single-shot upload limit."
  echo "         The large-submission flow takes over automatically past it."
else
  UPLOAD_FLOW=form
  echo "Upload size: ${archive_pct}% of the 500 MB single-shot limit."
fi

# COVERITY_FORCE_LARGE=1 runs the large flow on an archive that would fit the
# form, so that path can be exercised without waiting for the capture to grow
# past 500 MB. Without it the code would first run on the day it is needed,
# which is the worst day to discover it does not work.
if [ "${COVERITY_FORCE_LARGE:-0}" = "1" ]; then
  UPLOAD_FLOW=large
  echo "COVERITY_FORCE_LARGE=1 -- using the large-submission flow regardless of size."
fi

if [ "$DRY_RUN" = "1" ]; then
  echo
  echo "============================================================"
  echo " DRY RUN complete: capture verified, nothing uploaded."
  echo " To submit it without rebuilding:"
  echo "   COVERITY_UPLOAD_ONLY=1 $0 $ARCH"
  echo "============================================================"
  exit 0
fi

# ---------------------------------------------------------------- upload -----
# Two flows. Under 500 MB Coverity takes a single form POST; above it that form
# fails in a way that reads as a rejected token or a network error, and the
# documented route is initialize -> PUT -> enqueue against the project ID.
COV_PROJECT_ID=10314
# Overridable so the three-step flow can be exercised against a local mock --
# see cov_scan_test.sh. Nothing else should change it.
COV_API_BASE=${COVERITY_API_BASE:-https://scan.coverity.com}

# Reads one field out of a JSON object without jq, which is not installed on
# every machine that runs this script. Deliberately strict: the caller checks
# what comes back and refuses to continue on a value that does not look right,
# rather than PUTting the archive at an empty URL and reporting success.
json_string() {
  sed -n 's/.*"'"$1"'"[[:space:]]*:[[:space:]]*"\([^"]*\)".*/\1/p' | head -1 | sed 's|\/|/|g'
}

json_number() {
  sed -n 's/.*"'"$1"'"[[:space:]]*:[[:space:]]*\([0-9][0-9]*\).*/\1/p' | head -1
}

retry_hint="$ARCHIVE is kept, so it can be retried without another full rebuild: COVERITY_UPLOAD_ONLY=1 $0 $ARCH"

echo
echo "Uploading to Coverity Scan..."

if [ "$UPLOAD_FLOW" = "form" ]; then
  # --fail-with-body: without it curl exits 0 on an HTTP error, so a rejected
  # token or an oversized archive read as a successful submission.
  if ! curl --fail-with-body \
    --form token="$COVERITY_TOKEN" \
    --form email=objeck@gmail.com \
    --form file=@"$ARCHIVE" \
    --form version="$version" \
    --form description="Objeck $version (Linux $ARCH)" \
    "$COV_API_BASE/builds?project=Objeck"; then
    echo >&2
    fail "upload failed. $retry_hint"
  fi
else
  # step 1 of 3 -- initialize the build and get a one-time upload URL
  echo "  [1/3] initializing the build..."
  if ! init_response=$(curl --fail-with-body -sS -X POST \
    --data-urlencode "token=$COVERITY_TOKEN" \
    --data-urlencode "email=objeck@gmail.com" \
    --data-urlencode "version=$version" \
    --data-urlencode "description=Objeck $version (Linux $ARCH)" \
    --data-urlencode "file_name=$ARCHIVE" \
    "$COV_API_BASE/projects/$COV_PROJECT_ID/builds/init"); then
    echo >&2
    fail "build init failed. $retry_hint"
  fi

  upload_url=$(printf '%s' "$init_response" | json_string url)
  build_id=$(printf '%s' "$init_response" | json_number build_id)

  # Validate rather than assume. A response that parsed to an empty URL would
  # be PUT into nothing and enqueued against build "", and the script would then
  # print "Submitted" over a scan that never happened -- which is the exact
  # failure this path exists to prevent. The response itself is never echoed: it
  # carries a signed URL, and this script does not print credentials.
  case "$upload_url" in
    https://*) ;;
    # A non-default COVERITY_API_BASE is a deliberate test target, so a URL
    # under it is allowed to be plain http. Against the real endpoint the base
    # is https, so this branch cannot loosen anything in production.
    "$COV_API_BASE"/*) ;;
    *) fail "build init returned no usable upload URL. $retry_hint" ;;
  esac
  [ -n "$build_id" ] || fail "build init returned no build_id. $retry_hint"
  echo "        build_id=$build_id"

  # step 2 of 3 -- PUT the tarball at the returned URL
  echo "  [2/3] uploading $(du -h "$ARCHIVE" | cut -f1)..."
  if ! curl --fail-with-body -sS -X PUT \
    --header 'Content-Type: application/json' \
    --upload-file "$ARCHIVE" \
    "$upload_url"; then
    echo >&2
    fail "upload PUT failed for build $build_id. $retry_hint"
  fi

  # step 3 of 3 -- enqueue it. Until this succeeds the bytes are uploaded but
  # nothing will ever look at them, which is indistinguishable from success
  # anywhere except the project page.
  echo "  [3/3] enqueuing for analysis..."
  if ! curl --fail-with-body -sS -X PUT \
    --data-urlencode "token=$COVERITY_TOKEN" \
    "$COV_API_BASE/projects/$COV_PROJECT_ID/builds/$build_id/enqueue"; then
    echo >&2
    fail "enqueue failed for build $build_id -- the archive uploaded but was never queued. $retry_hint"
  fi
fi

rm -f "$ARCHIVE"
rm -rf "$COV_DIR"

echo
echo "============================================================"
echo " Submitted Objeck $version (Linux $ARCH)"
echo " Results: https://scan.coverity.com/projects/objeck"
echo "============================================================"
exit 0
