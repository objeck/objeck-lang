#!/bin/sh
rm -rf /tmp/cov-int
rm -f objeck-int.tgz
/home/randy/Documents/Code/coverity/bin/cov-build --dir /tmp/cov-int ./deploy_posix.sh 64
tar -czf objeck-int.tgz -C /tmp/ cov-int/
curl --form token=ZmtAZyNCX5XLFzQLR9QMXg \
  --form email=objeck@gmail.com \
  --form file=@objeck-int.tgz \
  --form version="2025.7.1" \
  --form description="Objeck" \
  https://scan.coverity.com/builds?project=Objeck
