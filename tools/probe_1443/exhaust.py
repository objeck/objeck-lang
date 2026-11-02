# How do loopback HTTP-style exchanges use ephemeral ports, and how does
# connect fail when they run out? One request per connection unless noted.
# usage: exhaust.py <requests> <threads> <mode>
#   cfirst   client reads the reply, shutdown(SD_SEND) + close (Objeck today);
#            the server closes when it reads EOF
#   sfirst   the server shuts down + closes after the reply; the client reads
#            to EOF, then closes
#   linger0  as cfirst, but the client closes with SO_LINGER {1, 0} (a reset)
#   reuseN   N requests per connection, then as cfirst
import socket, struct, subprocess, sys, threading, time
from collections import Counter

n = int(sys.argv[1]); workers = int(sys.argv[2]); mode = sys.argv[3]
per_conn = int(mode[5:]) if mode.startswith("reuse") else 1
BODY = b"x" * 300
REPLY = b"HTTP/1.1 200 OK\r\nContent-Length: %d\r\n\r\n" % len(BODY) + BODY
lock = threading.Lock()
errs = Counter(); first = {}; done = Counter(); srv = Counter(); open_ends = set()
slow = []
T0 = time.time()

def states():
    out = subprocess.run(["netstat", "-ano", "-p", "tcp"], capture_output=True, text=True).stdout
    return dict(Counter(l.split()[3] for l in out.splitlines() if len(l.split()) >= 4 and l.split()[0] == "TCP"))

ls = socket.socket(); ls.bind(("127.0.0.1", 0)); ls.listen(512)
port = ls.getsockname()[1]

def handle(c, peer):
    c.settimeout(30)
    r = None
    try:
        while r is None:
            buf = b""
            while b"\r\n\r\n" not in buf:
                d = c.recv(4096)
                if not d: r = "eof"; break
                buf += d
            if r: break
            c.sendall(REPLY)
            if mode == "sfirst":
                c.shutdown(socket.SHUT_WR); r = "srvclose"
    except socket.timeout: r = "timeout30"
    except OSError as e: r = "err%s" % getattr(e, "winerror", e.errno)
    c.close()
    with lock: srv[r] += 1; open_ends.discard(peer)

def acc():
    while True:
        c, a = ls.accept()
        with lock: open_ends.add(a[1])
        threading.Thread(target=handle, args=(c, a[1]), daemon=True).start()
threading.Thread(target=acc, daemon=True).start()

def client(k):
    left = k
    while left > 0:
        s = socket.socket(); s.settimeout(60)
        t = time.time()
        try:
            s.connect(("127.0.0.1", port))
            ct = time.time() - t
            if ct > 1:
                with lock: slow.append(round(ct, 2))
            for _ in range(min(per_conn, left)):
                s.sendall(b"GET / HTTP/1.1\r\nHost: x\r\n\r\n")
                got = b""
                while len(got) < len(REPLY):
                    d = s.recv(4096)
                    if not d: raise OSError(0, "eof before reply")
                    got += d
                left -= 1
                with lock: done["ok"] += 1
            if mode == "sfirst":
                while s.recv(4096): pass
                s.close()
            elif mode == "linger0":
                s.setsockopt(socket.SOL_SOCKET, socket.SO_LINGER, struct.pack("HH", 1, 0)); s.close()
            else:
                s.shutdown(socket.SHUT_WR); s.close()
            with lock: done["conns"] += 1
        except OSError as e:
            code = getattr(e, "winerror", None) or e.errno
            left -= 1
            with lock:
                errs[code] += 1
                if code not in first:
                    first[code] = (round(time.time() - T0, 2), round(time.time() - t, 4), done["ok"])
            s.close()
            time.sleep(0.01)

st0 = states()
ts = [threading.Thread(target=client, args=(n // workers,)) for _ in range(workers)]
for t in ts: t.start()
peak = Counter()
while any(t.is_alive() for t in ts):
    for k, v in states().items(): peak[k] = max(peak[k], v)
    time.sleep(1)
time.sleep(1)
print("exhaust mode=%s requests=%d threads=%d ok=%d conns=%d connect/io errors=%s first(t_run,t_call,ok_before)=%s slow_connects=%s server=%s still_open_server_ends=%d %.1fs"
      % (mode, n, workers, done["ok"], done["conns"], dict(errs), first, slow[:10], dict(srv), len(open_ends), time.time() - T0))
print("   states before %s" % st0)
print("   states peak   %s" % dict(peak))
print("   states end    %s" % states(), flush=True)
