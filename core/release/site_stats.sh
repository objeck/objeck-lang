#!/bin/bash
#
# Visitor report for www.objeck.org, built from Caddy's access log with GoAccess.
#
#     sudo bash site_stats.sh               write the report and print a short summary
#     sudo bash site_stats.sh --quiet       write the report only (what cron runs)
#
# No JavaScript, cookies or third-party service: the site's own access log is the only input,
# so visitors are not tracked beyond what the web server already records. The log is
# /var/log/caddy/objeck-org-access.log (see objeck-org.Caddyfile); Caddy rolls it at 100 MiB and
# keeps rolled files 90 days, so the report covers at most the last 90 days. It reads the rolled
# files too, compressed or not.
#
# The log is readable by the caddy user only, so this runs as root. The report goes to
# /home/objeckorg/stats/ (mode 700, outside the web root: nothing here is served).
#
#   report.html   the full report: visitors per day, pages, referrers, browsers, status codes,
#                 the 404s, and countries when a GeoIP database is present
#   summary.txt   the same headline numbers as plain text
#
# What the numbers mean:
#   * a visitor is a distinct IP + user agent per day, which is an estimate: it counts one
#     person on two networks twice and several people behind one address once;
#   * crawlers (search engines, link checkers, scripts) are left out, by user agent, and so are
#     requests answered 400, 404 or 405: a scanner probing for /.env or /xmlrpc.php is not a
#     visitor (the most common 404s are listed separately, from the raw log);
#   * pages are counted by path, so /page?x=1 and /page are one page, and a visit from this
#     site's own pages is not listed as a referrer;
#   * client addresses are counted but never printed in the report (the HOSTS panel is off);
#   * downloads happen on GitHub, not here: count those from the release's own download numbers.
#
# Countries: set GEOIP_DB (default /var/lib/goaccess/geo.mmdb) to a country or city database in
# MaxMind DB format (the free DB-IP "IP to Country Lite" works) and the report gains a country
# panel. Without it the report is complete except for that panel. The droplet uses DB-IP's
# database (CC BY 4.0, "IP Geolocation by DB-IP", https://db-ip.com), published monthly; refresh it
# now and then (country data changes slowly) with, as root:
#     m=$(date +%Y-%m); curl -fsSL "https://download.db-ip.com/free/dbip-country-lite-$m.mmdb.gz" | gunzip > /tmp/geo.mmdb && install -m 0644 /tmp/geo.mmdb /var/lib/goaccess/geo.mmdb
#
# Environment (for tests): LOG_DIR, LOG_NAME, OUT_DIR, OUT_OWNER, GEOIP_DB.

LOG_DIR="${LOG_DIR:-/var/log/caddy}"
LOG_NAME="${LOG_NAME:-objeck-org-access}"
OUT_DIR="${OUT_DIR:-/home/objeckorg/stats}"
OUT_OWNER="${OUT_OWNER:-objeckorg}"
GEOIP_DB="${GEOIP_DB:-/var/lib/goaccess/geo.mmdb}"

quiet=0
[ "$1" = "--quiet" ] && quiet=1

command -v goaccess >/dev/null 2>&1 || { echo "site_stats: goaccess is not installed (apt-get install goaccess)" >&2; exit 1; }
command -v python3 >/dev/null 2>&1 || { echo "site_stats: python3 is not installed" >&2; exit 1; }

# the current log and its rolled files, oldest first ('-' sorts before '.', so the live one is last)
shopt -s nullglob
logs=("$LOG_DIR/$LOG_NAME"*.log "$LOG_DIR/$LOG_NAME"*.log.gz)
if [ "${#logs[@]}" -eq 0 ]; then
	echo "site_stats: no $LOG_NAME logs in $LOG_DIR" >&2
	exit 1
fi

umask 077
mkdir -p "$OUT_DIR" || exit 1
work="$(mktemp -d "$OUT_DIR/.work.XXXXXX")" || exit 1
trap 'rm -rf "$work"' EXIT

geo=()
[ -r "$GEOIP_DB" ] && geo=(--geoip-database="$GEOIP_DB")

# a visit that came from this site's own pages is not a referral: blank those Referer values
# (the log is one JSON object per line, so this keeps every line valid)
own_site_referer='s#"Referer":\["https?://([a-z0-9-]+\.)?objeck\.org(:[0-9]+)?[^"]*"\]#"Referer":[""]#g'

# zcat -f copies a plain file through unchanged, so one pipe handles both kinds
zcat -f "${logs[@]}" | sed -E "$own_site_referer" | goaccess - \
	--log-format=CADDY --no-progress \
	--ignore-crawlers --ignore-panel=HOSTS \
	--ignore-status=400 --ignore-status=404 --ignore-status=405 \
	--no-query-string --http-method=no --http-protocol=no \
	--html-report-title="objeck.org visitors" \
	"${geo[@]}" \
	-o "$work/report.html" -o "$work/report.json" || { echo "site_stats: goaccess failed" >&2; exit 1; }

python3 - "$work/report.json" "$work/summary.txt" "${logs[@]}" <<'PY' || { echo "site_stats: could not summarise the report" >&2; exit 1; }
import collections, gzip, json, sys
d = json.load(open(sys.argv[1]))
g = d["general"]
logs = sys.argv[3:]
out = []
out.append("objeck.org visitors, %s to %s (%d log file%s)" % (
    g["start_date"], g["end_date"], len(logs), "" if len(logs) == 1 else "s"))
out.append("unique visitors (crawlers and scanners excluded): %d" % g["unique_visitors"])
out.append("requests counted: %d   (%d lines it could not read)" % (g["valid_requests"], g["failed_requests"]))
out.append("")
out.append("visitors per day:")
for row in sorted(d["visitors"]["data"], key=lambda r: r["data"]):
    day = row["data"]
    out.append("  %s-%s-%s  %6d visitors  %7d requests" % (
        day[0:4], day[4:6], day[6:8], row["visitors"]["count"], row["hits"]["count"]))
def top(section, title, n=8):
    rows = d.get(section, {}).get("data", [])
    if section == "geolocation":
        # GoAccess nests each country under its continent; list the countries
        rows = [c for continent in rows for c in continent.get("items", [])]
    rows = sorted(rows, key=lambda r: -r["visitors"]["count"])[:n]
    if rows:
        out.append("")
        out.append(title + ":")
        for r in rows:
            out.append("  %6d  %s" % (r["visitors"]["count"], r["data"]))
top("requests", "top pages (visitors)")
top("referring_sites", "top referring sites (visitors)")
top("geolocation", "top countries (visitors)")

# the 404s, from the raw log: GoAccess was told to leave them out of everything above
miss = collections.Counter()
for path in logs:
    opener = gzip.open if path.endswith(".gz") else open
    with opener(path, "rt", errors="replace") as f:
        for line in f:
            try:
                e = json.loads(line)
            except ValueError:
                continue
            if e.get("status") != 404:
                continue
            agent = (e["request"].get("headers", {}).get("User-Agent") or [""])[0].lower()
            if any(k in agent for k in ("bot", "spider", "crawl", "slurp")):
                continue
            miss[e["request"]["uri"].split("?")[0][:70]] += 1
if miss:
    out.append("")
    out.append("most common 404s (requests, bots excluded):")
    for uri, n in miss.most_common(10):
        out.append("  %6d  %s" % (n, uri))
with open(sys.argv[2], "w") as f:
    f.write("\n".join(out) + "\n")
PY

# publish both files only once both exist, so a failed run leaves yesterday's report in place
mv -f "$work/report.html" "$OUT_DIR/report.html" && mv -f "$work/summary.txt" "$OUT_DIR/summary.txt" || exit 1
chmod 700 "$OUT_DIR"
chmod 600 "$OUT_DIR/report.html" "$OUT_DIR/summary.txt"
if [ "$(id -u)" -eq 0 ] && id "$OUT_OWNER" >/dev/null 2>&1; then
	chown -R "$OUT_OWNER": "$OUT_DIR"
fi

[ "$quiet" -eq 1 ] || cat "$OUT_DIR/summary.txt"
exit 0
