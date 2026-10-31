# The Objeck HTTP exchange in plain Python sockets, to see whether Windows turns a
# clean client close into a reset by itself. Server: thread per connection, 30 s
# receive timeout, TCP_NODELAY, reply as two sends, then reads for the next request.
# Client: request, read exactly the reply, shutdown(SHUT_WR), close. Counts how
# each server read ended.
import socket
import sys
import threading
import time
from collections import Counter

mode = sys.argv[1] if len(sys.argv) > 1 else "shutclose"   # client close style
n = int(sys.argv[2]) if len(sys.argv) > 2 else 2000
workers = int(sys.argv[3]) if len(sys.argv) > 3 else 6

body = b"x" * 300
reply_head = b"HTTP/1.1 200 OK\r\nContent-Length: %d\r\nConnection: keep-alive\r\n\r\n" % len(body)
outcome = Counter()
lock = threading.Lock()


def handle(c):
    c.setsockopt(socket.SOL_SOCKET, socket.SO_RCVTIMEO, 30000)
    c.setsockopt(socket.IPPROTO_TCP, socket.TCP_NODELAY, 1)
    while True:
        buf = b""
        try:
            while b"\r\n\r\n" not in buf:
                d = c.recv(1)
                if not d:
                    with lock:
                        outcome["eof" if not buf else "eof-mid"] += 1
                    c.close()
                    return
                buf += d
        except OSError as e:
            with lock:
                outcome[f"err{getattr(e, 'winerror', e.errno)}"] += 1
            c.close()
            return
        c.sendall(reply_head)
        c.sendall(body)


srv = socket.socket()
srv.bind(("0.0.0.0", 0))
srv.listen(128)
port = srv.getsockname()[1]


def acceptor():
    while True:
        c, _ = srv.accept()
        threading.Thread(target=handle, args=(c,), daemon=True).start()


threading.Thread(target=acceptor, daemon=True).start()


def client(k):
    for _ in range(k):
        s = socket.create_connection(("127.0.0.1", port))
        s.setsockopt(socket.SOL_SOCKET, socket.SO_RCVTIMEO, 30000)
        s.sendall(b"GET /data HTTP/1.1\r\nHost: x\r\nConnection: keep-alive\r\n\r\n")
        need = len(reply_head) + len(body)
        got = b""
        while len(got) < need:
            got += s.recv(need - len(got))
        if mode == "shutclose":
            s.shutdown(socket.SHUT_WR)
            s.close()
        elif mode == "close":
            s.close()
        elif mode == "drain":
            s.shutdown(socket.SHUT_WR)
            try:
                while s.recv(4096):
                    pass
            except OSError:
                pass
            s.close()


t0 = time.time()
ts = [threading.Thread(target=client, args=(n // workers,)) for _ in range(workers)]
for t in ts:
    t.start()
for t in ts:
    t.join()
time.sleep(2)
print(mode, dict(outcome), f"{time.time() - t0:.1f}s")
