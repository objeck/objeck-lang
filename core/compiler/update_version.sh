#!/bin/sh
# Every line below is a compiler invocation. Without this, only the LAST one's
# exit code escaped: a failure mid-list left the previously committed .obl in
# place, the script exited 0, and CI shipped a stale library green.
set -e
# Target architecture: amd64 (default) or arm64. update_version_arm.sh used to
# be a byte-for-byte copy of this file with four characters changed, and CI
# carried a third copy of the same thirty compiler lines inline.
ARCH=${1:-amd64}
case "$ARCH" in amd64|arm64) ;; *) echo "usage: $0 [amd64|arm64]"; exit 2 ;; esac

make -f make/Makefile.sys.$ARCH clean
make -f make/Makefile.sys.$ARCH
./sys_obc -src lib_src/lang.obs -tar lib -opt s2 -dest ../lib/lang.obl -strict

make -f make/Makefile.$ARCH clean
make -f make/Makefile.$ARCH

# every other library, in dependency order (one list, shared with CI)
sh ./build_libs.sh ./obc
