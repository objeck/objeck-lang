#!/bin/sh
# Coverity Scan upload. Manual developer script -- CI does not run this.
#
# The token used to be hardcoded here, which meant it sat in a public repository
# from 2019-08-11 onward and was regenerated into cov_scan.sh on every version
# bump. It now comes from the environment, or from a file kept outside the repo:
#
#   export COVERITY_TOKEN=...       # from scan.coverity.com project settings
#   export COVERITY_TOKEN_FILE=...  # or a file containing just the token
#
# COVERITY_TOKEN wins when both are set. With neither, the default path below is
# read, so a bare ./cov_scan.sh works with no env prefix. Keep that file outside
# the repository -- an in-tree token is one 'git add -A' from being published,
# which is exactly how the original leaked.
#
# Never commit the value back into this file.
#
# cov-build is located via the environment too, so the install path does not go
# stale in the tree the way a hardcoded home directory did:
#
#   export COVERITY_HOME=...      # install root, the dir containing bin/cov-build
#
# Falling back to PATH if COVERITY_HOME is unset.
#
# The submitted version is read from version.h rather than hardcoded. It used to
# be substituted in by update_version.ps1, which no longer generates this file,
# so a literal here would silently keep reporting a stale version to Coverity
# after every bump.
: "${COVERITY_TOKEN_FILE:=$HOME/Documents/Code/cov_token.dat}"

if [ -z "$COVERITY_TOKEN" ] && [ -r "$COVERITY_TOKEN_FILE" ]; then
  COVERITY_TOKEN=$(cat "$COVERITY_TOKEN_FILE")
fi

if [ -z "$COVERITY_TOKEN" ]; then
  echo "no Coverity token -- export COVERITY_TOKEN, or put the token in" >&2
  echo "$COVERITY_TOKEN_FILE (override that path with COVERITY_TOKEN_FILE)." >&2
  exit 1
fi

if [ -n "$COVERITY_HOME" ]; then
  cov_build="$COVERITY_HOME/bin/cov-build"
  if [ ! -x "$cov_build" ]; then
    echo "no executable cov-build at $cov_build -- check COVERITY_HOME." >&2
    exit 1
  fi
elif cov_build=$(command -v cov-build); then
  : # found on PATH
else
  echo "cov-build not found -- set COVERITY_HOME to your Coverity install root," >&2
  echo "or put its bin/ directory on PATH." >&2
  exit 1
fi

version_h=../shared/version.h
if [ ! -r "$version_h" ]; then
  echo "cannot read $version_h -- run this script from core/release." >&2
  exit 1
fi
version=$(sed -nE 's/^#define[[:space:]]+VERSION_STRING[[:space:]]+L?"([^"]+)".*/\1/p' "$version_h")
if [ -z "$version" ]; then
  echo "no VERSION_STRING found in $version_h -- has the define been renamed?" >&2
  exit 1
fi
echo "submitting Objeck $version to Coverity Scan"

rm -rf /tmp/cov-int
rm -f objeck-int.tgz
"$cov_build" --dir /tmp/cov-int ./deploy_posix.sh 64
tar -czf objeck-int.tgz -C /tmp/ cov-int/
curl --form token="$COVERITY_TOKEN" \
  --form email=objeck@gmail.com \
  --form file=@objeck-int.tgz \
  --form version="$version" \
  --form description="Objeck" \
  https://scan.coverity.com/builds?project=Objeck
