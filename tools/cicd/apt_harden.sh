#!/bin/bash
#
# Makes apt on a GitHub-hosted Ubuntu runner survive a bad package mirror or a name-lookup outage.
# Run it once per job, with sudo, before the first `apt-get`:
#
#     sudo bash tools/cicd/apt_harden.sh
#
# It does three things:
#   1. sets apt's own retries and timeouts for every apt command that follows;
#   2. waits (up to APT_HARDEN_WAIT seconds, 120 by default) for the name lookup apt needs to work,
#      then, if only the Azure mirror's name fails, points apt at the plain Ubuntu archive instead;
#   3. installs tools/cicd/apt-get-retry.sh as /usr/local/bin/apt-get, so each later
#      `sudo apt-get update` and `sudo apt-get install` retries a network failure with a growing delay.
#
# It never fails: a problem in here must not turn a healthy job red, so every step is best effort and
# the exit status is always 0. See apt-get-retry.sh for why this exists.
#
# Environment (for tests): APT_DIR (/etc/apt), APT_BIN_DIR (/usr/local/bin), APT_HARDEN_WAIT (120),
# APT_HARDEN_PRIMARY and APT_HARDEN_AZURE (the host names to probe), APT_HARDEN_TEST (allow non-root).

APT_DIR="${APT_DIR:-/etc/apt}"
BIN_DIR="${APT_BIN_DIR:-/usr/local/bin}"
WAIT="${APT_HARDEN_WAIT:-120}"
AZURE="${APT_HARDEN_AZURE:-azure.archive.ubuntu.com}"
HERE="$(cd "$(dirname "$0")" && pwd)"

if [ "$(id -u)" -ne 0 ] && [ -z "$APT_HARDEN_TEST" ]; then
	echo "apt_harden: needs root (run it with sudo); skipped"
	exit 0
fi
[ -d "$APT_DIR" ] || { echo "apt_harden: no $APT_DIR; skipped"; exit 0; }

resolves() { getent hosts "$1" >/dev/null 2>&1; }

# 1. apt's own retries and timeouts
mkdir -p "$APT_DIR/apt.conf.d" 2>/dev/null
cat > "$APT_DIR/apt.conf.d/80-ci-retries" 2>/dev/null <<'EOF'
Acquire::Retries "5";
Acquire::http::Timeout "30";
Acquire::https::Timeout "30";
EOF

# 2. wait for the lookup apt needs, then drop the Azure mirror if only it fails
arch="$(dpkg --print-architecture 2>/dev/null)"
primary="${APT_HARDEN_PRIMARY:-archive.ubuntu.com}"
[ -z "$APT_HARDEN_PRIMARY" ] && [ "$arch" = "arm64" ] && primary="ports.ubuntu.com"

waited=0
until resolves "$primary" || [ "$waited" -ge "$WAIT" ]; do
	[ "$waited" -eq 0 ] && echo "apt_harden: $primary does not resolve yet; waiting up to ${WAIT}s"
	sleep 5
	waited=$((waited + 5))
done
if resolves "$primary"; then
	[ "$waited" -gt 0 ] && echo "apt_harden: $primary resolves after ${waited}s"
else
	echo "apt_harden: $primary still does not resolve after ${WAIT}s; continuing, the apt retries may still get through"
fi

if resolves "$primary" && ! resolves "$AZURE"; then
	echo "apt_harden: $AZURE does not resolve but $primary does; using archive.ubuntu.com instead of the Azure mirror"
	for f in "$APT_DIR"/apt-mirrors.txt "$APT_DIR"/sources.list "$APT_DIR"/sources.list.d/*; do
		[ -f "$f" ] && sed -i "s#${AZURE//./\\.}#archive.ubuntu.com#g" "$f" 2>/dev/null
	done
fi

# 3. the retrying apt-get ahead of the real one
if mkdir -p "$BIN_DIR" 2>/dev/null && install -m 0755 "$HERE/apt-get-retry.sh" "$BIN_DIR/apt-get" 2>/dev/null; then
	echo "apt_harden: apt-get now retries network failures (installed $BIN_DIR/apt-get)"
else
	echo "apt_harden: could not install the retrying apt-get; apt's own retries are still on"
fi

exit 0
