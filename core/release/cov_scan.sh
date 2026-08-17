#!/bin/sh
# Coverity Scan upload. Manual developer script -- CI does not run this.
#
# The token used to be hardcoded here, which meant it sat in a public repository
# from 2019-08-11 onward and was regenerated into cov_scan.sh on every version
# bump. It now comes from the environment:
#
#   export COVERITY_TOKEN=...    # from scan.coverity.com project settings
#
# Never commit the value back into this file.
if [ -z "$COVERITY_TOKEN" ]; then
  echo "COVERITY_TOKEN is not set -- export it before running this script." >&2
  exit 1
fi

rm -rf /tmp/cov-int
rm -f objeck-int.tgz
/home/randy/Documents/Code/coverity/bin/cov-build --dir /tmp/cov-int ./deploy_posix.sh 64
tar -czf objeck-int.tgz -C /tmp/ cov-int/
curl --form token="$COVERITY_TOKEN" \
  --form email=objeck@gmail.com \
  --form file=@objeck-int.tgz \
  --form version="2026.8.2" \
  --form description="Objeck" \
  https://scan.coverity.com/builds?project=Objeck
