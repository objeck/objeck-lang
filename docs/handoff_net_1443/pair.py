# Pairs each traced client connection with its server end: connect, accept, the
# client's close, how the server's read ended (EOF, reset, timeout) and the server's
# close. Prints a summary and every connection whose server end never saw it end.
import glob
import os
import sys
from collections import Counter, defaultdict

trace = sys.argv[1]
ev = []
for f in glob.glob(os.path.join(trace, "sock_*.log")):
    for line in open(f, errors="replace"):
        parts = line.split()
        if len(parts) < 9:
            continue
        kv = dict(x.split("=", 1) for x in parts[4:])
        ev.append((int(parts[0]), int(parts[1]), int(parts[2]), parts[3], int(kv["h"]), int(kv["l"]), int(kv["p"]), int(kv["d"]), int(kv["pe"])))
ev.sort()
t0 = ev[0][0] if ev else 0
kinds = Counter(e[3] for e in ev)
print("events:", dict(kinds))

# a connection is (client port, server port); the same pair recurs when a port is reused,
# so connections are numbered per pair as they open
conns = []
open_by_pair = {}
server_pid = None
for t, pid, tid, what, h, l, p, d, pe in ev:
    if what == "CONNECT":
        c = {"pair": (l, p), "cpid": pid, "ch": h, "t_conn": t, "events": []}
        conns.append(c)
        open_by_pair[(l, p)] = c
    elif what == "ACCEPT":
        c = open_by_pair.get((p, l))
        if c:
            c["t_acc"] = t
            c["spid"] = pid
            c["sh"] = h
    if what in ("CLOSE", "READ_EOF", "READ_ERR", "CLOSE_ERR"):
        # client side: l is the client port; server side: p is the client port
        cc = open_by_pair.get((l, p))
        if cc and pid == cc["cpid"] and h == cc["ch"]:
            c, side = cc, "c"
        else:
            c, side = open_by_pair.get((p, l)), "s"
        if c:
            c["events"].append((round((t - c["t_conn"]) / 1000, 1), side + ":" + what + ("" if d == 0 else f"({d})") + ("" if pe == 0 else f"[pe{pe}]")))

fails = [e for e in ev if e[3] == "CONNECT_FAIL"]
print("connect failures:", [(round((e[0] - t0) / 1e6, 1), e[1], e[7]) for e in fails])

shape = Counter()
for c in conns:
    s = tuple(x[1].split("(")[0] for x in c["events"])
    shape[("acc" if "t_acc" in c else "noacc",) + s] += 1
print("connection shapes:")
for k, v in shape.most_common(20):
    print(f"  {v:6d}  {' '.join(k)}")

# does a reset follow the client process's last socket event (its exit, roughly)?
last_by_pid = {}
for e in ev:
    last_by_pid[e[1]] = e[0]
gap = Counter()
for c in conns:
    if c["cpid"] == c.get("spid"):
        continue
    end = [x for x in c["events"] if x[1].startswith("s:READ")]
    close = [x for x in c["events"] if x[1] == "c:CLOSE"]
    if end and close:
        kind = end[0][1].split("(")[0] + end[0][1][len(end[0][1].split("(")[0]):]
        is_last = (c["t_conn"] + close[0][0] * 1000) >= last_by_pid[c["cpid"]] - 1000
        gap[(kind, "client's last close" if is_last else "client closed earlier")] += 1
print("how the server's read ended, by whether it was the client's last socket event:")
for k, v in sorted(gap.items()):
    print(f"  {v:6d}  {k}")

print("server end never saw the connection end:")
n = 0
for c in conns:
    if not any(x[1].startswith("s:READ") for x in c["events"]):
        n += 1
        if n <= 40:
            print("  ", c["pair"], "conn at", round((c["t_conn"] - t0) / 1e6, 2), "s; accepted" if "t_acc" in c else "; NOT accepted", c["events"])
print("total", n, "of", len(conns))
