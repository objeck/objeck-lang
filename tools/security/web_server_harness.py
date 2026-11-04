#!/usr/bin/env python3
"""Local black-box security harness for Web.HTTP.Server (plain HTTP target on 127.0.0.1:8098).

Each case sends hostile or malformed input, then probes that the server still answers a normal
request. A case reports PASS (rejected or handled safely), NOTE (works, but worth knowing) or
FINDING (a defect: crash, hang, leak, or accepted something it should not have).
"""
import os, socket, subprocess, sys, threading, time

HOST, PORT = "127.0.0.1", 8098
EXE, ENV = sys.argv[1], dict(os.environ)
server = None
results = []


def start():
    global server
    server = subprocess.Popen([os.environ["OBR"], EXE], env=ENV, stdout=open("server.out", "ab"), stderr=subprocess.STDOUT)
    for _ in range(80):
        try:
            socket.create_connection((HOST, PORT), 0.3).close()
            return
        except OSError:
            time.sleep(0.25)
    raise SystemExit("target never started")


def alive():
    return server.poll() is None


def raw(data, wait=3.0, limit=1 << 22):
    """Send data, read until the peer closes or `wait` seconds pass. -> (bytes, closed, seconds)"""
    t0 = time.time()
    s = socket.create_connection((HOST, PORT), 5)
    s.settimeout(wait)
    out, closed = b"", False
    try:
        if data:
            s.sendall(data)
        while len(out) < limit:
            chunk = s.recv(65536)
            if not chunk:
                closed = True
                break
            out += chunk
    except socket.timeout:
        pass
    except OSError:
        closed = True
    finally:
        s.close()
    return out, closed, time.time() - t0


def status(resp):
    line = resp.split(b"\r\n", 1)[0].decode("latin-1")
    return line if line.startswith("HTTP/") else ""


def code(resp):
    p = status(resp).split(" ")
    return int(p[1]) if len(p) > 1 and p[1].isdigit() else 0


def header(resp, name):
    head = resp.split(b"\r\n\r\n", 1)[0].decode("latin-1").split("\r\n")[1:]
    vals = [h.split(":", 1)[1].strip() for h in head if h.lower().startswith(name.lower() + ":")]
    return vals


def body(resp):
    return resp.split(b"\r\n\r\n", 1)[1] if b"\r\n\r\n" in resp else b""


def probe():
    r, _, _ = raw(b"GET /data HTTP/1.1\r\nHost: x\r\nConnection: close\r\n\r\n", 5)
    return code(r) == 200 and body(r) == b"abcdefghij"


def case(name, fn):
    """fn() -> (verdict, note). The probe afterwards turns any lost service into a FINDING."""
    global server
    t0 = time.time()
    try:
        verdict, note = fn()
    except Exception as e:  # harness error, not a server result
        verdict, note = "NOTE", "harness exception: %r" % (e,)
    ok = alive() and probe()
    if not ok:
        verdict, note = "FINDING", "SERVICE LOST after this case (%s)%s" % (
            "server process exited, rc=%s" % server.poll() if not alive() else "no longer answering", "; " + note if note else "")
        if not alive():
            start()
    results.append((verdict, name, note, time.time() - t0))
    print("%-8s %-46s %s" % (verdict, name, note), flush=True)


# ---- expectations ---------------------------------------------------------------------------

def rejected(resp, closed):
    """4xx, or the connection dropped with no 2xx: both are acceptable ways to refuse."""
    c = code(resp)
    return (400 <= c < 500) or (c == 0 and closed) or c == 0


def expect_reject(data, why, wait=3.0):
    def f():
        r, closed, secs = raw(data, wait)
        c = code(r)
        if 200 <= c < 300:
            return "FINDING", "accepted (%s): %s" % (why, status(r))
        if c >= 500:
            return "FINDING", "server error %d (%s)" % (c, why)
        return "PASS", ("rejected: %s" % (status(r) or "connection closed"))
    return f


def expect_survive(data, wait=3.0):
    def f():
        r, closed, secs = raw(data, wait)
        c = code(r)
        if c >= 500:
            return "FINDING", "server error: " + status(r)
        return "PASS", status(r) or ("closed" if closed else "no reply in %.0fs" % secs)
    return f


GET = b"GET %s HTTP/1.1\r\nHost: x\r\nConnection: close\r\n%s\r\n"


def get(path, extra=b""):
    return GET % (path if isinstance(path, bytes) else path.encode(), extra)


# ---- cases ----------------------------------------------------------------------------------

def c_pipeline():
    two = (b"GET /data HTTP/1.1\r\nHost: x\r\n\r\n" b"GET /data HTTP/1.1\r\nHost: x\r\nConnection: close\r\n\r\n")
    r, closed, _ = raw(two, 4)
    n = r.count(b"HTTP/1.1 200")
    return ("PASS", "both pipelined requests answered") if n == 2 else ("FINDING", "%d of 2 pipelined answers" % n)


def c_smuggle_cl_te():
    d = (b"POST /data HTTP/1.1\r\nHost: x\r\nContent-Length: 4\r\nTransfer-Encoding: chunked\r\n\r\n0\r\n\r\n"
         b"GET /data HTTP/1.1\r\nHost: x\r\nConnection: close\r\n\r\n")
    r, closed, _ = raw(d, 3)
    n = r.count(b"HTTP/1.1 200")
    c = code(r)
    if n >= 2:
        return "FINDING", "request smuggling: both parsed, %d responses" % n
    return "PASS", "CL+TE refused: %s" % (status(r) or "closed")


def c_dup_cl():
    d = b"POST /data HTTP/1.1\r\nHost: x\r\nContent-Length: 3\r\nContent-Length: 50\r\nConnection: close\r\n\r\nabc"
    r, closed, secs = raw(d, 3)
    c = code(r)
    if 200 <= c < 300:
        return "NOTE", "conflicting Content-Length values accepted (joined as '3,50'?): %s" % status(r)
    return "PASS", "refused: %s" % (status(r) or "closed")


def c_cl_longer_than_body():
    r, closed, secs = raw(b"POST /data HTTP/1.1\r\nHost: x\r\nContent-Length: 100\r\n\r\nshort", 8)
    if secs >= 7.5 and not r:
        return "NOTE", "server waits for the missing body past 8 s (idle timeout is 30 s; one thread held per slow client)"
    return "PASS", "%s after %.1fs" % (status(r) or "closed", secs)


def c_big_body():
    d = b"POST /data HTTP/1.1\r\nHost: x\r\nContent-Length: %d\r\nConnection: close\r\n\r\n" % (17 * 1048576)
    r, closed, _ = raw(d, 3)
    return ("PASS", "refused: " + status(r)) if code(r) == 413 else ("FINDING", "17 MB Content-Length not refused with 413: %s" % (status(r) or "closed"))


def c_neg_cl():
    r, closed, _ = raw(b"POST /data HTTP/1.1\r\nHost: x\r\nContent-Length: -5\r\nConnection: close\r\n\r\n", 3)
    return ("PASS", status(r) or "closed") if code(r) in (0, 400, 413) else ("FINDING", "negative Content-Length: " + status(r))


def c_long_header_name_flood():
    hdrs = b"".join(b"X-%d: v\r\n" % i for i in range(20000))
    r, closed, secs = raw(b"GET /data HTTP/1.1\r\nHost: x\r\nConnection: close\r\n" + hdrs + b"\r\n", 8)
    c = code(r)
    if 200 <= c < 300:
        return "NOTE", "20,000 headers accepted (no header-count limit)"
    return "PASS", status(r) or "closed"


def c_huge_header_value():
    r, closed, secs = raw(get("/data", b"X-Big: " + b"A" * (2 << 20) + b"\r\n"), 10)
    c = code(r)
    if 200 <= c < 300:
        return "NOTE", "2 MB header value accepted (no header size limit)"
    return "PASS", status(r) or "closed"


def c_huge_request_line():
    r, closed, secs = raw(b"GET /" + b"A" * (1 << 20) + b" HTTP/1.1\r\nHost: x\r\nConnection: close\r\n\r\n", 10)
    c = code(r)
    if 200 <= c < 300:
        return "NOTE", "1 MB request line accepted (no request-line limit; should be 414)"
    return "PASS", status(r) or "closed"


def c_reflect_inject():
    r, closed, _ = raw(b"GET /reflect HTTP/1.1\r\nHost: x\r\nConnection: close\r\nX-Test: a\rInjected: yes\r\n\r\n", 3)
    if header(r, "Injected"):
        return "FINDING", "header injection via bare CR reflected into the response"
    return "PASS", "no injected header (%s)" % (status(r) or "closed")


def c_reflect_lf():
    r, closed, _ = raw(b"GET /reflect HTTP/1.1\r\nHost: x\r\nConnection: close\r\nX-Test: a\nInjected: yes\r\n\r\n", 3)
    if header(r, "Injected"):
        return "FINDING", "header injection via bare LF reflected into the response"
    return "PASS", "no injected header (%s)" % (status(r) or "closed")


def c_echo_path(path, label):
    def f():
        r, closed, _ = raw(get(path), 3)
        b = body(r).decode("latin-1")
        return "NOTE", "handler saw: %s [%s]" % (b.strip()[:90], status(r) or "closed")
    return f


def c_range(spec, expect_codes):
    def f():
        r, closed, _ = raw(get("/data", b"Range: " + spec + b"\r\n"), 3)
        c = code(r)
        if c >= 500 or c == 0 and not closed:
            return "FINDING", "%s -> %s" % (spec[:40], status(r) or "no reply")
        if c not in expect_codes:
            return "FINDING", "%s -> %s (expected one of %s)" % (spec[:40], status(r), expect_codes)
        return "PASS", "%s -> %s" % (spec[:40].decode(), status(r))
    return f


def c_slowloris():
    socks = []
    try:
        for _ in range(300):
            s = socket.create_connection((HOST, PORT), 5)
            s.sendall(b"GET /data HTTP/1.1\r\nHost: x\r\nX-A: ")
            socks.append(s)
        t0 = time.time()
        ok = probe()
        return ("PASS", "300 stalled partial requests held; a normal request still answered") if ok else ("FINDING", "300 stalled partial requests starve new clients")
    finally:
        for s in socks:
            s.close()


def c_many_idle():
    socks = []
    try:
        for _ in range(1500):
            socks.append(socket.create_connection((HOST, PORT), 5))
        ok = probe()
        return ("PASS", "1500 idle connections open; a normal request still answered") if ok else ("FINDING", "1500 idle connections starve new clients")
    except OSError as e:
        return "NOTE", "could only open %d idle connections: %s" % (len(socks), e)
    finally:
        for s in socks:
            s.close()


def c_flood():
    bad, lock = [0], threading.Lock()

    def one():
        try:
            r, _, _ = raw(b"GET /data HTTP/1.1\r\nHost: x\r\nConnection: close\r\n\r\n", 10)
            if code(r) != 200:
                with lock:
                    bad[0] += 1
        except OSError:
            with lock:
                bad[0] += 1
    ts = [threading.Thread(target=one) for _ in range(350)]
    t0 = time.time()
    [t.start() for t in ts]
    [t.join() for t in ts]
    return ("PASS", "350 concurrent requests, all answered in %.1fs" % (time.time() - t0)) if bad[0] == 0 else ("FINDING", "%d of 350 concurrent requests failed" % bad[0])


def c_slow_read():
    """Request 5 MB and never read it: the handler thread blocks in a send. Does the server stay up?"""
    s = socket.create_connection((HOST, PORT), 5)
    s.setsockopt(socket.SOL_SOCKET, socket.SO_RCVBUF, 4096)
    s.sendall(b"GET /big HTTP/1.1\r\nHost: x\r\n\r\n")
    time.sleep(1.5)
    ok = probe()
    s.close()
    return ("PASS", "a client that never reads a 5 MB response does not block others") if ok else ("FINDING", "non-reading client blocks the server")


def c_get_with_body():
    r, closed, _ = raw(b"GET /data HTTP/1.1\r\nHost: x\r\nContent-Length: 5\r\n\r\nhelloGET /data HTTP/1.1\r\nHost: x\r\nConnection: close\r\n\r\n", 3)
    n = r.count(b"HTTP/1.1 200")
    return ("PASS", "body on GET consumed, then next request parsed cleanly (%d answers)" % n) if n == 2 else ("FINDING", "%d answers; body bytes desynchronised the stream" % n)


def expect_status(data, wanted, why, wait=5.0):
    def f():
        r, closed, secs = raw(data, wait)
        c = code(r)
        if c in wanted:
            return "PASS", "%s (%s)" % (status(r) or "closed", why)
        return "FINDING", "%s: got %s, wanted %s" % (why, status(r) or ("closed" if closed else "no reply"), wanted)
    return f


def c_path(path, ok, why):
    """ok=True: served normally (200); ok=False: refused with 400."""
    def f():
        r, closed, _ = raw(get(path), 4)
        c = code(r)
        if ok:
            return ("PASS", "served: %s" % body(r).decode("latin-1")[:60]) if c == 200 else ("FINDING", "%s: legitimate path refused: %s" % (why, status(r) or "closed"))
        return ("PASS", "refused: %s" % status(r)) if c == 400 else ("FINDING", "%s: %s" % (why, status(r) or "closed"))
    return f


def c_absolute_uri():
    r, closed, _ = raw(b"GET http://evil.example/data HTTP/1.1\r\nHost: x\r\nConnection: close\r\n\r\n", 3)
    return ("PASS", "absolute-form served by its path") if code(r) == 200 and body(r) == b"abcdefghij" else ("FINDING", "absolute-form: " + (status(r) or "closed"))


def c_body_stall():
    r, closed, secs = raw(b"POST /data HTTP/1.1\r\nHost: x\r\nContent-Length: 100\r\n\r\nshort", 40)
    if secs <= 14 and (closed or code(r) in (400, 408)):
        return "PASS", "a body that never completes is dropped after %.1fs (%s)" % (secs, status(r) or "closed")
    return "FINDING", "a body that never completes held the connection %.1fs" % secs


def c_trickle():
    """One byte every 3 s inside a header: a per-read timeout never fires. Does the server cut it off?"""
    s = socket.create_connection((HOST, PORT), 5)
    s.sendall(b"GET /data HTTP/1.1\r\nHost: x\r\nX-A: ")
    t0, ended = time.time(), None
    try:
        while time.time() - t0 < 50:
            s.settimeout(3)
            try:
                s.sendall(b"a")
                d = s.recv(100)
                ended = time.time() - t0
                break
            except socket.timeout:
                continue
            except OSError:
                ended = time.time() - t0
                break
    finally:
        s.close()
    if ended is not None:
        return ("PASS", "a trickled header was cut off after %.0fs (deadline 30 s)" % ended) if ended <= 40 else ("FINDING", "trickle cut off only after %.0fs" % ended)
    return "FINDING", "a header trickled one byte per 3 s held its thread for the full 50 s (no total deadline)"


def c_cap():
    """More connections than the cap (400): the extra ones are refused with 503, the server recovers."""
    socks, refused = [], 0
    try:
        for _ in range(600):
            try:
                c = socket.create_connection((HOST, PORT), 5)
            except OSError:
                refused += 1
                continue
            socks.append(c)
            c.settimeout(0.02)
            try:
                d = c.recv(64)
                if d.startswith(b"HTTP/1.1 503") or d == b"":
                    refused += 1
            except socket.timeout:
                pass
    finally:
        for c in socks:
            c.close()
    time.sleep(2.0)
    recovered = probe()
    if refused >= 100 and recovered:
        return "PASS", "%d of 600 connections refused at the cap; normal service resumed after" % refused
    if not recovered:
        return "FINDING", "server did not recover after the connection flood"
    return "FINDING", "cap not enforced: only %d of 600 refused" % refused


def main():
    start()
    print("target up, pid", server.pid, flush=True)
    case("probe: baseline", lambda: ("PASS", "ok") if probe() else ("FINDING", "baseline failed"))

    # parsing and framing
    case("LF-only line endings", expect_survive(b"GET /data HTTP/1.1\nHost: x\nConnection: close\n\n"))
    case("request line: no version", expect_reject(b"GET /data\r\nHost: x\r\n\r\n", "no version"))
    case("request line: 4 parts", expect_reject(b"GET /data HTTP/1.1 junk\r\nHost: x\r\n\r\n", "extra token"))
    case("version HTTP/9.9", expect_status(b"GET /data HTTP/9.9\r\nHost: x\r\nConnection: close\r\n\r\n", (505,), "unsupported version"))
    case("method lowercase 'get'", expect_survive(b"get /data HTTP/1.1\r\nHost: x\r\nConnection: close\r\n\r\n"))
    case("method unknown 'BREW'", expect_survive(b"BREW /data HTTP/1.1\r\nHost: x\r\nConnection: close\r\n\r\n"))
    case("method CONNECT", expect_survive(b"CONNECT x:443 HTTP/1.1\r\nHost: x\r\nConnection: close\r\n\r\n"))
    case("missing Host header", expect_status(b"GET /data HTTP/1.1\r\nConnection: close\r\n\r\n", (400,), "HTTP/1.1 needs Host"))
    case("two Host headers", expect_status(b"GET /data HTTP/1.1\r\nHost: a\r\nHost: b\r\nConnection: close\r\n\r\n", (400,), "one Host only"))
    case("HTTP/1.0 without Host", expect_status(b"GET /data HTTP/1.0\r\n\r\n", (200,), "1.0 needs none"))
    case("absolute-URI request target", c_absolute_uri)
    case("header with no colon", expect_status(get("/data", b"garbage-line\r\n"), (400,), "no colon"))
    case("header: space before colon", expect_status(get("/data", b"X-A : v\r\n"), (400,), "space before colon"))
    case("header: obs-fold continuation", expect_status(get("/data", b"X-A: v\r\n  folded\r\n"), (400,), "obs-fold"))
    case("NUL byte in path", expect_status(get(b"/da\x00ta"), (0, 400, 408), "NUL in path (refused: closed, 400 or 408)"))
    case("NUL byte in header value", expect_status(get("/data", b"X-A: a\x00b\r\n"), (0, 400, 408), "NUL in value (refused: closed, 400 or 408)"))
    case("header name with a bad character", expect_status(get("/data", b"X(A): v\r\n"), (400,), "non-token name"))
    case("header: empty name", expect_status(get("/data", b": v\r\n"), (400,), "empty name"))
    case("non-ASCII bytes in path", expect_survive(get(b"/d\xc3\xa4ta\xff")))
    case("pipelined requests", c_pipeline)
    case("GET with a body", c_get_with_body)
    case("Content-Length + Transfer-Encoding", c_smuggle_cl_te)
    case("duplicate Content-Length", c_dup_cl)
    case("negative Content-Length", c_neg_cl)
    case("Content-Length 17 MB (limit 16 MB)", c_big_body)
    case("Content-Length > bytes sent", c_body_stall)

    # size limits
    case("request line 1 MB", expect_status(b"GET /" + b"A" * (1 << 20) + b" HTTP/1.1\r\nHost: x\r\nConnection: close\r\n\r\n", (414, 400), "oversized request line", 10))
    case("request line 9000 bytes", expect_status(b"GET /" + b"A" * 9000 + b" HTTP/1.1\r\nHost: x\r\nConnection: close\r\n\r\n", (414, 400), "request line over the limit", 10))
    case("one header value 2 MB", expect_status(get("/data", b"X-Big: " + b"A" * (2 << 20) + b"\r\n"), (431,), "oversized header", 10))
    case("one header value 9000 bytes", expect_status(get("/data", b"X-Big: " + b"A" * 9000 + b"\r\n"), (431,), "header line over the limit"))
    case("header value 7000 bytes (allowed)", expect_status(get("/data", b"X-Ok: " + b"A" * 7000 + b"\r\n"), (200,), "under the limit"))
    case("20,000 headers", expect_status(b"GET /data HTTP/1.1\r\nHost: x\r\nConnection: close\r\n" + b"".join(b"X-%d: v\r\n" % i for i in range(20000)) + b"\r\n", (431,), "too many headers", 10))
    case("100 headers (allowed)", expect_status(b"GET /data HTTP/1.1\r\nHost: x\r\nConnection: close\r\n" + b"".join(b"X-%d: v\r\n" % i for i in range(96)) + b"\r\n", (200,), "under the count limit"))

    # injection and path handling (what a handler is given)
    case("response-header injection (bare CR)", c_reflect_inject)
    case("response-header injection (bare LF)", c_reflect_lf)
    for label, path, ok in [
            ("dot-dot literal", "/echo/../../etc/passwd", False), ("encoded dot-dot %2e%2e", "/echo/%2e%2e/%2e%2e/etc/passwd", False),
            ("mixed .%2e", "/echo/.%2e/x", False), ("encoded slash %2f", "/echo/..%2f..%2fetc/passwd", False),
            ("encoded slash alone %2f", "/echo/a%2fb", False), ("encoded backslash %5c", "/echo/a%5cb", False),
            ("double-encoded %252e", "/echo/%252e%252e/x", False), ("triple-encoded %25252e", "/echo/%25252e%25252e/x", False),
            ("double-encoded slash %252f", "/echo/a%252fb", False), ("backslash dot-dot", "/echo/..\\..\\x", False),
            ("overlong %c0%ae", "/echo/%c0%ae%c0%ae/x", False), ("encoded NUL %00", "/echo/a%00b", False),
            ("encoded CRLF in path %0d%0a", "/echo/a%0d%0ab", False), ("bad escape %zz", "/echo/a%zzb", False),
            ("truncated escape %4", "/echo/a%4", False), ("trailing dot-dot segment", "/echo/x/..", False),
            ("dot-dot in the query only", "/echo?next=../../x", True), ("encoded CRLF in the query", "/echo?a=%0d%0aSet-Cookie:x=1", True),
            ("file name with two dots a..b", "/echo/a..b.html", True), ("three dots segment", "/echo/.../x", True),
            ("double slash", "//echo//x", True), ("dot segment /./", "/echo/./x", True),
            ("encoded space %20", "/echo/a%20b", True), ("encoded UTF-8 %C3%A4", "/echo/%C3%A4", True),
            ("percent-encoded letters %65cho", "/%65cho/x", True)]:
        case("path: " + label, c_path(path, ok, label))

    # Range (the new code)
    for spec, ok in [(b"bytes=0-99999999999999999999", (200, 206, 416)), (b"bytes=-", (200,)), (b"bytes=--1", (200,)),
                     (b"bytes=1-1-", (200,)), (b"bytes=,", (200,)), (b"bytes=" + b"1-2," * 5000, (200, 431)),
                     (b"bytes=-99999999999999999999", (200, 206, 416)), (b"bytes=9999999999-", (200, 416)),
                     (b"bytes=\x00-1", (200, 400, 408)), (b"bytes=0-0", (206,)), (b"", (200,)), (b"bytes=1-2" + b"x" * 100000, (200, 431))]:
        case("Range: " + repr(spec[:28]), c_range(spec, ok))

    # resource exhaustion
    case("300 stalled partial requests", c_slowloris)
    case("600 connections against a cap of 400", c_cap)
    case("header trickled one byte per 3 s", c_trickle)
    case("350 concurrent requests (under the cap)", c_flood)
    case("client never reads a 5 MB response", c_slow_read)

    server.terminate()
    f = [r for r in results if r[0] == "FINDING"]
    n = [r for r in results if r[0] == "NOTE"]
    print("\n== %d cases: %d PASS, %d NOTE, %d FINDING ==" % (len(results), len(results) - len(f) - len(n), len(n), len(f)))
    for v, name, note, _ in f + n:
        print("  %-8s %s -- %s" % (v, name, note))


main()
