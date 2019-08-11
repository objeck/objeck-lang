#!/bin/sh
rm -rf /tmp/cov-int
rm objeck-int.tgz
/home/randy/Documents/Code/Temp/cov-analysis-linux64-2019.03/bin/cov-build --dir /tmp/cov-int ./deploy.sh 64
tar -czf objeck-int.tgz -C /tmp/ cov-int/
curl --form token=ZmtAZyNCX5XLFzQLR9QMXg \
  --form email=objeck@gmail.com \
  --form file=@objeck-int.tgz \
  --form version="v5.1.6" \
  --form description="Objeck" \
  https://scan.coverity.com/builds?project=Objeck
