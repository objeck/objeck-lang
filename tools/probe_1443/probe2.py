# Instrumented loopback probe for #1443.
# usage: probe2.py <closemode> <n> <workers> [opts]
#  closemode: shutclose | close | drain | linger0
#  opts (comma list): nodelay, rcvtimeo, select, srvclosefirst, bindany, onesend
import socket, sys, threading, time, select as sel
from collections import Counter, defaultdict

mode = sys.argv[1]
n = int(sys.argv[2]); workers = int(sys.argv[3])
opts = set(sys.argv[4].split(",")) if len(sys.argv) > 4 and sys.argv[4] else set()

body = b"x" * 300
head = b"HTTP/1.1 200 OK\r\nContent-Length: %d\r\n\r\n" % len(body)
lock = threading.Lock()
outcome = Counter()
hstate = {}
held = []
prestate = {}
eoft = {}
accepts = Counter()
srv_open = {}        # peer port -> accept time (open server ends)
srv_log = []         # (peer, outcome, t_accept, t_end)
cli_log = []         # (lport, t_conn_start, conn_dur, t_close, reused_open_server_end)
T0 = time.time()

def now(): return time.time() - T0

def handle(c, peer):
    ta = now()
    if "rcvtimeo" in opts:
        c.setsockopt(socket.SOL_SOCKET, socket.SO_RCVTIMEO, 30000)
    else:
        c.settimeout(30)
    if "nodelay" in opts:
        c.setsockopt(socket.IPPROTO_TCP, socket.TCP_NODELAY, 1)
    res = None
    served = 0
    hstate[peer] = "recv0"
    while res is None:
        buf = b""
        try:
            while b"\r\n\r\n" not in buf:
                d = c.recv(4096)
                if not d:
                    res = "eof"; break
                buf += d
        except socket.timeout:
            res = "timeout"
        except OSError as e:
            res = "err%s" % getattr(e, "winerror", e.errno)
        if res is None:
            if "onesend" in opts:
                c.sendall(head + body)
            else:
                c.sendall(head); c.sendall(body)
            served += 1
            hstate[peer] = "recv%d" % served
            if "srvclosefirst" in opts:
                res = "srvclose"
    c.close()
    with lock:
        outcome[res] += 1
        srv_open.pop(peer, None)
        srv_log.append((peer, res, ta, now()))

FAM = socket.AF_INET6 if "v6" in opts else socket.AF_INET
HOST = "::1" if "v6" in opts else "127.0.0.1"
srv = socket.socket(FAM)
srv.bind((HOST, 0))
srv.listen(128)
port = srv.getsockname()[1]

def acceptor():
    while True:
        c, a = srv.accept()
        with lock:
            srv_open[a[1]] = now()
            accepts[a[1]] += 1
        threading.Thread(target=handle, args=(c, a[1]), daemon=True).start()
threading.Thread(target=acceptor, daemon=True).start()

def client(k):
    for _ in range(k):
        t = now()
        s = socket.socket(FAM)
        s.settimeout(60)
        try:
            s.connect((HOST, port))
        except OSError as e:
            with lock:
                outcome["CONNECT_FAIL"] += 1
                cli_log.append((None, t, now() - t, None, str(e)))
            s.close(); continue
        cd = now() - t
        lp = s.getsockname()[1]
        with lock:
            reused = lp in srv_open and srv_open[lp] < t
        s.sendall(b"GET / HTTP/1.1\r\nHost: x\r\n\r\n")
        need = len(head) + len(body); got = b""
        while len(got) < need:
            d = s.recv(need - len(got))
            if not d: break
            got += d
        if mode == "shutclose":
            s.shutdown(socket.SHUT_WR); s.close()
        elif mode == "close":
            s.close()
        elif mode == "drain":
            s.shutdown(socket.SHUT_WR)
            dres = "eof"
            try:
                while s.recv(4096): pass
            except OSError as e: dres = "err%s" % getattr(e, "winerror", e.errno)
            with lock: outcome["cli_drain_" + dres] += 1
            s.close()
        elif mode == "drainhold":
            s.setblocking(False)
            pre = "none"
            try:
                d = s.recv(1, socket.MSG_PEEK)
                pre = "EOF" if d == b"" else "data"
            except BlockingIOError: pre = "none"
            except OSError as e: pre = "err%s" % getattr(e, "winerror", e.errno)
            s.settimeout(60)
            if "sleep5" in opts: time.sleep(0.005)
            if "sleep50" in opts: time.sleep(0.05)
            with lock: prestate[lp] = pre; outcome["pre_" + pre] += 1
            s.shutdown(socket.SHUT_WR)
            dres = "eof"
            t1 = time.perf_counter()
            try:
                while s.recv(4096): pass
            except OSError as e: dres = "err%s" % getattr(e, "winerror", e.errno)
            with lock:
                outcome["cli_drain_" + dres] += 1
                held.append(s); eoft[lp] = time.perf_counter() - t1
        elif mode == "linger0":
            import struct
            s.setsockopt(socket.SOL_SOCKET, socket.SO_LINGER, struct.pack("HH", 1, 0)); s.close()
        with lock:
            cli_log.append((lp, t, cd, now(), reused))

ts = [threading.Thread(target=client, args=(n // workers,)) for _ in range(workers)]
for t in ts: t.start()
for t in ts: t.join()
time.sleep(1)
slow = [c for c in cli_log if c[2] > 1.0]
print(mode, sorted(opts), dict(outcome), "slow_connects=%d" % len(slow), "still_open=%d" % len(srv_open), "%.1fs" % now())
for c in slow[:10]:
    print("  slow", c)
# For stuck server ends: when did the client close, was the port reused?
stuck = dict(srv_open)
byport = defaultdict(list)
for c in cli_log:
    if c[0]: byport[c[0]].append(c)
for p, ta in list(stuck.items())[:10]:
    print("  stuck peer", p, "accepted %.3f" % ta, [("%.3f" % c[1], "%.3f" % c[3]) for c in byport[p]])
print("  accepts", sum(accepts.values()), "dup ports", [p for p,v in accepts.items() if v > 1][:10])
print("  stuck states", [hstate.get(p) for p in stuck])
import subprocess
out = subprocess.run(["netstat", "-ano", "-p", "tcp"], capture_output=True, text=True).stdout
import statistics
allt = sorted(eoft.values())
print("  eof-wait median %.6f max %.6f" % (statistics.median(allt), allt[-1]) if allt else "")
print("  stuck pre", [prestate.get(p) for p in stuck])
print("  stuck eof-wait", ["%.6f" % eoft.get(p, -1) for p in stuck])
from collections import Counter as C2
st = C2()
for line in out.splitlines():
    f = line.split()
    if len(f) >= 4 and f[1].endswith(":%d" % port): st["srv:" + f[3]] += 1
    if len(f) >= 4 and f[2].endswith(":%d" % port): st["cli:" + f[3]] += 1
print("  states", dict(st))
for p in list(stuck)[:10]:
    for line in out.splitlines():
        if (":%d " % p) in line:
            print("  netstat", line.strip())
