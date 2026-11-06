#!/usr/bin/env bash
# Soak check for the objeck.org rehost. Runs on the droplet from a systemd timer
# and appends one line per probe to /var/log/objeck-org-soak.log:
#   <utc> <label> <http-code> <seconds>   (plus a mem/caddy line per run)
# A probe that is not 200 is also written with FAIL so `grep FAIL` is the report.
set -u
LOG="${SOAK_LOG:-/var/log/objeck-org-soak.log}"
BASE="${SOAK_BASE:-http://104.236.228.162}"
now() { date -u +%FT%TZ; }
probe() { # label url
  local out code secs
  out="$(curl -s -o /dev/null -m 20 -w '%{http_code} %{time_total}' "$2" 2>/dev/null)" || out="000 0"
  code="${out% *}"; secs="${out#* }"
  if [ "$code" = 200 ]; then echo "$(now) $1 $code $secs" >>"$LOG"
  else echo "$(now) FAIL $1 $code $secs" >>"$LOG"; fi
}
probe site-home        "$BASE/"
probe site-api         "$BASE/api/latest/"
probe site-key         "$BASE/objeck-release.pub"
probe_status() { # label expected url [curl args...]
  local label="$1" want="$2" url="$3"; shift 3
  local out code secs
  out="$(curl -s -o /dev/null -m 20 -w '%{http_code} %{time_total}' "$@" "$url" 2>/dev/null)" || out="000 0"
  code="${out% *}"; secs="${out#* }"
  if [ "$code" = "$want" ]; then echo "$(now) $label $code $secs" >>"$LOG"
  else echo "$(now) FAIL $label $code $secs (want $want)" >>"$LOG"; fi
}
etag="$(curl -sI -m 20 "$BASE/index.html" 2>/dev/null | tr -d '' | awk 'tolower($1)=="etag:"{print $2}')"
[ -n "$etag" ] && probe_status site-304 304 "$BASE/index.html" -H "If-None-Match: $etag"
probe_status site-gzip 200 "$BASE/index.html" -H "Accept-Encoding: gzip"
probe_status site-range 206 "$BASE/style/styles.css" -H "Range: bytes=0-99"
probe_status site-traversal "${SOAK_TRAVERSAL_WANT:-400}" "$BASE/a/../x" --path-as-is
probe playground-health "https://playground.objeck.org/api/health"
SVC_MEM=""
if [ -n "${SOAK_SERVICE:-}" ]; then
  m=$(systemctl show "$SOAK_SERVICE" -p MemoryCurrent --value 2>/dev/null); [ "$m" -gt 0 ] 2>/dev/null && SVC_MEM=" svc_mem_mb=$((m / 1048576)) svc_restarts=$(systemctl show "$SOAK_SERVICE" -p NRestarts --value)"
fi
echo "$(now) host mem_avail_mb=$(free -m | awk '/Mem:/{print $7}') caddy=$(systemctl is-active caddy) playground=$(systemctl is-active playground)${SVC_MEM}" >>"$LOG"
