#!/bin/sh
# Fail a POSIX deploy if a file an example opens by name did not land in the
# tree. The deploy scripts do not use 'set -e', so a cp that found nothing only
# printed "cannot stat" and the deploy carried on: every deploy script copied
# gl_crystal.obj from the day gl_model.obs shipped, and no distribution ever
# carried it, because .gitignore's *.obj had kept it out of git.
#
# deploy_windows.cmd holds the same list; keep the two in step.
#
# usage: verify_example_assets.sh <examples_dir>
#   e.g. verify_example_assets.sh core/release/deploy/examples
set -u
dir="$1"

failures=0
checked=0
for asset in \
  README.md \
  data/gender.csv \
  data/weather.json \
  opengl/cube_gl.obs \
  opengl/gl_crystal.obj \
  opengl/gl_model.obs
do
  if [ ! -f "$dir/$asset" ]; then
    echo "ERROR: missing example asset: $dir/$asset"
    failures=$((failures + 1))
    continue
  fi
  checked=$((checked + 1))
done

if [ "$failures" -ne 0 ]; then
  echo "============================================================"
  echo " ERROR: $failures example asset(s) missing - aborting deploy"
  echo "============================================================"
  exit 1
fi
echo "example assets verified: $checked in $dir"
