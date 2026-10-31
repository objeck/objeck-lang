# Like synwatch.py, but keeps every loopback socket's state history, so a stalled
# connect can be matched to what happened earlier on its port pair, and names the
# processes that own each end.
import subprocess
import sys
import time
from collections import Counter, defaultdict

out_path = sys.argv[1]
seconds = float(sys.argv[2])


def snapshot():
    text = subprocess.run(["netstat", "-ano", "-p", "tcp"], capture_output=True, text=True).stdout
    rows = []
    for line in text.splitlines():
        parts = line.split()
        if len(parts) == 5 and parts[0] == "TCP" and parts[1].startswith(("127.0.0.1:", "0.0.0.0:")):
            rows.append((parts[1], parts[2], parts[3], parts[4]))
    return rows


def port(addr):
    return addr.rsplit(":", 1)[1]


def pid_names(pids):
    names = {}
    text = subprocess.run(["tasklist", "/fo", "csv", "/nh"], capture_output=True, text=True).stdout
    for line in text.splitlines():
        cells = [c.strip('"') for c in line.split('","')]
        if len(cells) > 1 and cells[1] in pids:
            names[cells[1]] = cells[0]
    for p in pids:
        cmd = subprocess.run(["powershell", "-NoProfile", "-Command",
                              f"(Get-CimInstance Win32_Process -Filter 'ProcessId={p}').CommandLine"],
                             capture_output=True, text=True).stdout.strip()
        names[p] = names.get(p, "?") + " | " + cmd[-160:]
    return names


history = defaultdict(list)   # (local, remote) -> [(time, state, pid)]
first_seen = {}
reported = set()
end = time.time() + seconds
t0 = time.time()
with open(out_path, "a", buffering=1) as log:
    log.write(f"start {time.ctime()}\n")
    while time.time() < end:
        now = time.time()
        rows = snapshot()
        present = set()
        for r in rows:
            key = (r[0], r[1])
            present.add(key)
            h = history[key]
            if not h or h[-1][1] != r[2] or h[-1][2] != r[3]:
                h.append((round(now - t0, 1), r[2], r[3]))
        for key, h in history.items():
            if key not in present and h and h[-1][1] != "gone":
                h.append((round(now - t0, 1), "gone", ""))
        syn = {(r[0], r[1]) for r in rows if r[2] == "SYN_SENT"}
        for key in list(first_seen):
            if key not in syn:
                age = now - first_seen.pop(key)
                if key in reported:
                    log.write(f"{time.strftime('%H:%M:%S')} resolved after {age:.1f} s: {key}\n")
                    reported.discard(key)
        for key in syn:
            first_seen.setdefault(key, now)
            if now - first_seen[key] >= 2.0 and key not in reported:
                reported.add(key)
                lp, rp = port(key[0]), port(key[1])
                states = Counter(r[2] for r in rows)
                log.write(f"{time.strftime('%H:%M:%S')} (t={now - t0:.1f}) STALL {key[0]} -> {key[1]}; states {dict(states)}\n")
                pids = set()
                for k, h in history.items():
                    if {port(k[0]), port(k[1])} == {lp, rp}:
                        log.write(f"    {k}: {h}\n")
                        pids.update(x[2] for x in h if x[2])
                for r in rows:
                    if r[2] == "LISTENING" and port(r[0]) == rp:
                        log.write(f"    listener {r}\n")
                        pids.add(r[3])
                for r in rows:
                    if r[2] in ("FIN_WAIT_1", "FIN_WAIT_2", "CLOSE_WAIT", "CLOSED", "LAST_ACK") and rp in (port(r[0]), port(r[1])):
                        log.write(f"    other {r}\n")
                for p, n in pid_names(pids).items():
                    log.write(f"    pid {p}: {n}\n")
        # bound memory: forget sockets gone for a minute
        if int(now) % 30 == 0:
            for k in [k for k, h in history.items() if h[-1][1] == "gone" and now - t0 - h[-1][0] > 60]:
                del history[k]
        time.sleep(0.1)
    log.write(f"end {time.ctime()}\n")
