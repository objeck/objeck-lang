# Does a client that closes first (and so keeps TIME_WAIT on its own port)
# run out of ephemeral ports on loopback, and how does connect then fail?
# usage: exhaust.py <connections> <threads>
import socket, subprocess, sys, threading, time
from collections import Counter

n = int(sys.argv[1]); workers = int(sys.argv[2])
REQ = b"GET / HTTP/1.1\r\nHost: x\r\n\r\n"
REPLY = b"HTTP/1.1 200 OK\r\nContent-Length: 3\r\n\r\nabc"
lock = threading.Lock()
errs = Counter(); first = {}; done = Counter()
T0 = time.time()

def tw_count():
    out = subprocess.run(["netstat", "-ano", "-p", "tcp"], capture_output=True, text=True).stdout
    c = Counter(l.split()[3] for l in out.splitlines() if len(l.split()) >= 4 and l.split()[0] == "TCP")
    return dict(c)

ls = socket.socket(); ls.bind(("127.0.0.1", 0)); ls.listen(512)
port = ls.getsockname()[1]

def handle(c):
    c.settimeout(30)
    try:
        while True:
            buf = b""
            while b"\r\n\r\n" not in buf:
                d = c.recv(4096)
                if not d: c.close(); return
                buf += d
            c.sendall(REPLY)
    except OSError:
        c.close()

def acc():
    while True:
        c, _ = ls.accept()
        threading.Thread(target=handle, args=(c,), daemon=True).start()
threading.Thread(target=acc, daemon=True).start()

def client(k):
    for _ in range(k):
        s = socket.socket(); s.settimeout(60)
        t = time.time()
        try:
            s.connect(("127.0.0.1", port))
            s.sendall(REQ)
            got = b""
            while len(got) < len(REPLY):
                d = s.recv(100)
                if not d: raise OSError(0, "eof before reply")
                got += d
            s.shutdown(socket.SHUT_WR); s.close()
            with lock: done["ok"] += 1
        except OSError as e:
            code = getattr(e, "winerror", None) or e.errno
            with lock:
                errs[code] += 1
                if code not in first:
                    first[code] = (round(time.time() - T0, 2), round(time.time() - t, 4), done["ok"])
            s.close()
            time.sleep(0.01)

print("start states", tw_count())
ts = [threading.Thread(target=client, args=(n // workers,)) for _ in range(workers)]
for t in ts: t.start()
for t in ts: t.join()
print("exhaust n=%d threads=%d ok=%d errors=%s first(t_run, t_call, ok_before)=%s %.1fs" % (n, workers, done["ok"], dict(errs), first, time.time() - T0))
print("end states", tw_count())
