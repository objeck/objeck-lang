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

echo "--- prove the driver loads and allocates an environment"
# The formula installs no client; load the dylib the way the driver manager
# will, resolve the entry points the driver manager wants first, and call the
# driver's own SQLAllocHandle/SQLAllocEnv -- iODBC reports IM004 when either
# is missing or fails. python3 is on every macOS runner.
python3 - "$driver" <<'PY'
import ctypes, sys
lib = ctypes.CDLL(sys.argv[1])
for name in ("SQLAllocHandle", "SQLAllocEnv", "SQLConnect", "SQLDriverConnect", "SQLExecDirect", "SQLGetDiagRec"):
    getattr(lib, name)
h = ctypes.c_void_p()
rc = lib.SQLAllocHandle(ctypes.c_short(1), ctypes.c_void_p(0), ctypes.byref(h))
print("  driver SQLAllocHandle(SQL_HANDLE_ENV) -> rc %d, handle %s" % (rc, "set" if h.value else "NULL"))
e = ctypes.c_void_p()
rc2 = lib.SQLAllocEnv(ctypes.byref(e))
print("  driver SQLAllocEnv -> rc %d, handle %s" % (rc2, "set" if e.value else "NULL"))
if rc != 0 or rc2 != 0:
    sys.exit("the driver cannot allocate an environment on its own")
PY

# The faithful check: connect through the SAME driver manager libobjk_odbc
# carries -- the static libiodbc tools/deps/build_macos_deps.sh built under
# $OBJECK_DEPS/macos-bundle/iodbc -- with iODBC tracing on, so a failure
# names the call that failed instead of leaving a one-line IM004 in a
# regression log. Skipped on a box without those deps.
iodbc="${OBJECK_DEPS:-$HOME/objeck-deps/darwin-arm64}/macos-bundle/iodbc"
if [ -f "$iodbc/lib/libiodbc.a" ] && [ -f "$iodbc/include/sql.h" ]; then
	echo "--- connect through the bundled static libiodbc ($iodbc)"
	work=$(mktemp -d)
	cat > "$work/probe.c" <<'EOF'
#include <stdio.h>
#include <sql.h>
#include <sqlext.h>
static void diag(const char *what, SQLSMALLINT t, SQLHANDLE h) {
	SQLCHAR st[6], msg[1024]; SQLINTEGER ne; SQLSMALLINT len, i = 1;
	while (SQLGetDiagRec(t, h, i, st, &ne, msg, sizeof msg, &len) == SQL_SUCCESS) {
		printf("  %s diag %d: [%s] %s (%d)\n", what, i, st, msg, (int)ne);
		i++;
	}
}
int main(int argc, char **argv) {
	SQLHENV env; SQLHDBC dbc; SQLHSTMT stmt; SQLRETURN rc; SQLCHAR v[64]; SQLLEN l;
	rc = SQLAllocHandle(SQL_HANDLE_ENV, SQL_NULL_HANDLE, &env);
	printf("  SQLAllocHandle(ENV) rc=%d\n", (int)rc);
	rc = SQLSetEnvAttr(env, SQL_ATTR_ODBC_VERSION, (void *)SQL_OV_ODBC3, 0);
	printf("  SQLSetEnvAttr(ODBC3) rc=%d\n", (int)rc);
	rc = SQLAllocHandle(SQL_HANDLE_DBC, env, &dbc);
	printf("  SQLAllocHandle(DBC) rc=%d\n", (int)rc);
	rc = SQLConnect(dbc, (SQLCHAR *)argv[1], SQL_NTS, (SQLCHAR *)"", SQL_NTS, (SQLCHAR *)"", SQL_NTS);
	printf("  SQLConnect(%s) rc=%d\n", argv[1], (int)rc);
	diag("connect", SQL_HANDLE_DBC, dbc);
	if (!SQL_SUCCEEDED(rc)) return 1;
	SQLAllocHandle(SQL_HANDLE_STMT, dbc, &stmt);
	rc = SQLExecDirect(stmt, (SQLCHAR *)"select sqlite_version()", SQL_NTS);
	printf("  SQLExecDirect rc=%d\n", (int)rc);
	diag("exec", SQL_HANDLE_STMT, stmt);
	if (!SQL_SUCCEEDED(rc)) return 1;
	if (SQLFetch(stmt) == SQL_SUCCESS && SQLGetData(stmt, 1, SQL_C_CHAR, v, sizeof v, &l) == SQL_SUCCESS)
		printf("  sqlite_version() = %s\n", v);
	SQLFreeHandle(SQL_HANDLE_STMT, stmt);
	SQLDisconnect(dbc);
	SQLFreeHandle(SQL_HANDLE_DBC, dbc);
	SQLFreeHandle(SQL_HANDLE_ENV, env);
	return 0;
}
EOF
	cc -I"$iodbc/include" -o "$work/probe" "$work/probe.c" "$iodbc/lib/libiodbc.a" -liconv
	# a copy of the ini with tracing on, so the user's file stays quiet
	{ cat "$ini"; echo ""; echo "[ODBC]"; echo "Trace=1"; echo "TraceFile=$work/iodbc.trace"; } > "$work/odbc.ini"
	if ODBCINI="$work/odbc.ini" "$work/probe" "$DSN"; then
		echo "  connected through libiodbc $(ls "$iodbc/lib")"
	else
		echo "::error::the bundled libiodbc could not use the '$DSN' data source; iODBC trace follows"
		cat "$work/iodbc.trace" 2>/dev/null || echo "  (no trace written)"
		exit 1
	fi
else
	echo "  (no bundled libiodbc under $iodbc; the regression tests are the first connection)"
fi

echo "ODBC data source '$DSN' is ready: sqliteodbc $installed at $driver"
