#!/bin/bash
# Installs the SQLite ODBC driver and registers the objeck_sqlite_test data
# source the ODBC regression tests connect to -- the macOS counterpart of
# ci-build.yml's Linux "Configure ODBC test DSN" step and of
# tools/cicd/setup_sqlite_odbc.ps1 on Windows.
#
# The driver is Homebrew's sqliteodbc formula (Christian Werner's sqliteodbc,
# 0.99991 at the time of writing; Homebrew verifies the tarball's SHA-256 on
# install). libobjk_odbc.dylib links libiodbc statically, and iODBC reads a
# user's data sources from ~/.odbc.ini, where a section's Driver is the full
# path of the driver library -- so the DSN names the formula's dylib directly
# and needs no odbcinst.ini entry.
#
# Usage: tools/cicd/setup_sqlite_odbc.sh            (macOS; needs brew)
set -euo pipefail

DSN=objeck_sqlite_test
EXPECTED_VERSION=0.99991

if [ "$(uname -s)" != "Darwin" ]; then
	echo "this script configures macOS; ci-build.yml sets the Linux DSN up inline" >&2
	exit 1
fi
command -v brew >/dev/null 2>&1 || { echo "brew is needed" >&2; exit 1; }

echo "--- sqliteodbc from Homebrew"
if brew list --versions sqliteodbc >/dev/null 2>&1; then
	echo "  $(brew list --versions sqliteodbc) already installed"
else
	brew install sqliteodbc
fi
installed=$(brew list --versions sqliteodbc | awk '{print $2}')
if [ "$installed" != "$EXPECTED_VERSION" ]; then
	# Homebrew cannot pin a formula version from here; say so rather than fail,
	# since a newer driver is still the driver the tests want.
	echo "::warning::sqliteodbc $installed installed; this script was written against $EXPECTED_VERSION"
fi

prefix=$(brew --prefix sqliteodbc)
driver=
for cand in "$prefix/lib/libsqlite3odbc.dylib" "$prefix/lib/libsqlite3odbc.so"; do
	if [ -f "$cand" ]; then driver=$cand; break; fi
done
if [ -z "$driver" ]; then
	echo "no libsqlite3odbc under $prefix/lib:" >&2
	ls -la "$prefix/lib" >&2 || true
	exit 1
fi
# brew --prefix returns the opt/ symlink; the driver manager is happier with a real path
driver=$(cd "$(dirname "$driver")" && pwd -P)/$(basename "$driver")
echo "  driver: $driver"

echo "--- register user DSN '$DSN' -> $driver (in-memory database)"
ini="$HOME/.odbc.ini"
if [ -f "$ini" ] && grep -q "^\[$DSN\]" "$ini"; then
	echo "  $ini already has [$DSN]; leaving it"
else
	{
		echo "[ODBC Data Sources]"
		echo "$DSN=SQLite3 ODBC Driver"
		echo ""
		echo "[$DSN]"
		echo "Description=Objeck ODBC regression test DSN"
		echo "Driver=$driver"
		echo "Database=:memory:"
	} >> "$ini"
fi
cat "$ini"

echo "--- prove the driver loads"
# The formula installs no client; load the dylib the way the driver manager
# will and resolve SQLConnect, which is what a wrong path or a broken link
# would break. python3 is on every macOS runner.
python3 - "$driver" <<'PY'
import ctypes, sys
lib = ctypes.CDLL(sys.argv[1])
for name in ("SQLConnect", "SQLDriverConnect", "SQLExecDirect"):
    getattr(lib, name)
print("  loaded %s; SQLConnect resolved" % sys.argv[1])
PY

echo "ODBC data source '$DSN' is ready: sqliteodbc $installed at $driver"
