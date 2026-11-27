#!/bin/bash
#
# gcc, g++, cc and c++ in the portable-Linux build image.
#
# Until v2026.11.3 the Linux release was compiled by Ubuntu's GCC, which is
# configured to harden everything it builds: position-independent executables,
# stack protector, _FORTIFY_SOURCE, stack-clash and control-flow protection,
# read-only relocations bound at start-up, and --as-needed. gcc-toolset on
# AlmaLinux turns none of that on. This wrapper puts the same defaults in front
# of the caller's own arguments, so the release keeps the hardening it had and
# anything a build passes explicitly still wins.
#
# The rules follow Ubuntu's: -fPIE unless the caller chose a PIC mode, -pie and
# immediate binding only when an executable is being linked, _FORTIFY_SOURCE
# only with optimisation. It is 2 here and 3 on Ubuntu 24.04 because level 3
# needs glibc 2.34's headers.
#
# OBJECK_LINK_DIRS, a colon-separated list, is searched before every other
# library directory. tools/packaging/build_portable_linux.sh points it at the
# static libraries, so the makefiles' plain -lmbedtls finds the archive there
# before any shared library. LIBRARY_PATH cannot do this: GCC
# searches it after its own directories.

case "$(basename "$0")" in
  g++|c++) real=/opt/rh/gcc-toolset-14/root/usr/bin/g++ ;;
  *)       real=/opt/rh/gcc-toolset-14/root/usr/bin/gcc ;;
esac

pic=default; link=yes; exe=yes; optimised=no; fortify=default
for arg in "$@"; do
  case "$arg" in
    -fPIC|-fpic|-fPIE|-fpie|-fno-pic|-fno-PIC|-fno-pie|-fno-PIE) pic=explicit ;;
    -c|-S|-E|-M|-MM) link=no ;;
    -shared|-static|-static-pie|-r|-pie|-no-pie|-nostdlib) exe=no ;;
    -O0) optimised=no ;;
    -O*) optimised=yes ;;
    -D_FORTIFY_SOURCE*|-U_FORTIFY_SOURCE) fortify=explicit ;;
  esac
done

flags=(-fstack-protector-strong -fstack-clash-protection)
case "$(uname -m)" in
  x86_64)  flags+=(-fcf-protection) ;;
  aarch64) flags+=(-mbranch-protection=standard) ;;
esac
[ "$pic" = default ] && flags+=(-fPIE)
[ "$optimised" = yes ] && [ "$fortify" = default ] && flags+=(-D_FORTIFY_SOURCE=2)
if [ "$link" = yes ]; then
  flags+=(-Wl,-z,relro -Wl,--as-needed)
  [ "$exe" = yes ] && flags+=(-pie -Wl,-z,now)
  if [ -n "${OBJECK_LINK_DIRS:-}" ]; then
    IFS=: read -r -a dirs <<< "$OBJECK_LINK_DIRS"
    for dir in "${dirs[@]}"; do flags+=(-L"$dir"); done
  fi
fi

exec "$real" "${flags[@]}" "$@"
