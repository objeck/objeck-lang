"""Raw-socket negative/mutation fuzz of the Objeck HTTP server. Usage: http_fuzz.py <port> <count> <seed> <pid>
After every 100 cases the server must still answer a plain GET with 200 and its process must be alive."""
import collections, os, random, socket, subprocess, sys, time
from concurrent.futures import ThreadPoolExecutor

port, count, seed, pid = int(sys.argv[1]), int(sys.argv[2]), int(sys.argv[3]), int(sys.argv[4])
rng = random.Random(seed)
H = "Host: 127.0.0.1\r\n"
SEEDS = [
    "GET /x HTTP/1.1\r\n" + H + "\r\n",
    "GET /a/b?q=1&r=%20z HTTP/1.1\r\n" + H + "Connection: close\r\n\r\n",
    "POST /p HTTP/1.1\r\n" + H + "Content-Length: 5\r\n\r\nhello",
    "POST /p HTTP/1.1\r\n" + H + "Transfer-Encoding: chunked\r\n\r\n5\r\nhello\r\n0\r\n\r\n",
    "GET /x HTTP/1.1\r\n" + H + "Range: bytes=0-4\r\nAccept: */*\r\nUser-Agent: f\r\n\r\n",
    "HEAD /x HTTP/1.1\r\n" + H + "\r\n",
    "OPTIONS * HTTP/1.1\r\n" + H + "\r\n",
    "GET /x HTTP/1.0\r\n\r\n",
]
WEIRD = [b"\x00", b"\r\n", b"\n", b"\r", b" ", b"\t", b"%", b"%00", b"..", b"\xff\xfe", b"\x80", b":", b";", b"\"", b"\\", b"\x7f", b"A" * 70000]
NUMS = [b"-1", b"0", b"99999999999999999999", b"4294967296", b"2147483648", b"-2147483649", b"1e9", b"0x10", b"+5"]
METHODS = [b"GET", b"POST", b"PUT", b"DELETE", b"PATCH", b"TRACE", b"CONNECT", b"BREW", b"", b"get", b"G\x00T"]


def mutate():
    base = bytearray(rng.choice(SEEDS).encode())
    k = rng.choice(["flip", "ins", "del", "trunc", "dupline", "bigheader", "manyheaders", "cl", "method", "version", "pipeline", "chunk", "slow"])
    n = len(base)
    if k == "flip":
        for _ in range(rng.randint(1, 6)):
            base[rng.randrange(n)] ^= 1 << rng.randrange(8)
    elif k == "ins":
        p = rng.randrange(n); base[p:p] = rng.choice(WEIRD)
    elif k == "del":
        p = rng.randrange(n); del base[p:p + rng.randint(1, 20)]
    elif k == "trunc":
        del base[rng.randrange(1, n):]
    elif k == "dupline":
        lines = bytes(base).split(b"\r\n"); i = rng.randrange(len(lines)); lines.insert(i, lines[i]); base = bytearray(b"\r\n".join(lines))
    elif k == "bigheader":
        base = bytearray(b"GET /x HTTP/1.1\r\nHost: 127.0.0.1\r\nX-Big: " + b"B" * rng.choice([8000, 70000, 1000000]) + b"\r\n\r\n")
    elif k == "manyheaders":
        base = bytearray(b"GET /x HTTP/1.1\r\nHost: 127.0.0.1\r\n" + b"".join(b"X-%d: v\r\n" % i for i in range(rng.choice([100, 1000, 20000]))) + b"\r\n")
    elif k == "cl":
        base = bytearray(b"POST /p HTTP/1.1\r\nHost: 127.0.0.1\r\nContent-Length: " + rng.choice(NUMS) + b"\r\n\r\n" + b"x" * rng.randint(0, 50))
    elif k == "method":
        base = bytearray(rng.choice(METHODS) + b" /x HTTP/1.1\r\nHost: 127.0.0.1\r\n\r\n")
    elif k == "version":
        base = bytearray(b"GET /x " + rng.choice([b"HTTP/9.9", b"HTTP/1.", b"HTTP/", b"http/1.1", b"HTTP/1.1.1", b"", b"HTTP/2.0", b"HTTP/0.9"]) + b"\r\nHost: 127.0.0.1\r\n\r\n")
    elif k == "pipeline":
        base = bytearray(b"".join(rng.choice(SEEDS).encode() for _ in range(rng.randint(2, 30))))
    elif k == "chunk":
        sz = rng.choice([b"5", b"-1", b"FFFFFFFFFFFFFFFF", b"7fffffff", b"zz", b"", b"5;ext=" + b"e" * 5000, b"0"])
        base = bytearray(b"POST /p HTTP/1.1\r\nHost: 127.0.0.1\r\nTransfer-Encoding: chunked\r\n\r\n" + sz + b"\r\nhello\r\n" + rng.choice([b"0\r\n\r\n", b"", b"3\r\n"]))
    elif k == "slow":
        pass
    return k, bytes(base)


def exchange(case):
    k, data = case
    t0 = time.time()
    try:
        s = socket.create_connection(("127.0.0.1", port), timeout=4)
        s.settimeout(1)
        if k == "slow":
            for ch in b"GET /x HTT":
                s.send(bytes([ch])); time.sleep(0.01)
            s.close(); return k, "slow-close", None
        s.sendall(data)
        out = b""
        try:
            while len(out) < 4096:
                c = s.recv(4096)
                if not c:
                    break
                out += c
        except socket.timeout:
            s.close()
            if out:
                return k, out.split(b"\r\n", 1)[0][:30].decode("latin1"), None
            return k, "silent-wait", None
        s.close()
        line = out.split(b"\r\n", 1)[0][:30].decode("latin1") if out else "(closed, no reply)"
        return k, line, None
    except (ConnectionResetError, BrokenPipeError, ConnectionAbortedError):
        return k, "reset", None
    except OSError as e:
        return k, "conn-error", str(e)[:60]


def alive():
    for _ in range(8):
        try:
            s = socket.create_connection(("127.0.0.1", port), timeout=5); s.settimeout(5)
            s.sendall(b"GET /alive HTTP/1.1\r\nHost: 127.0.0.1\r\nConnection: close\r\n\r\n")
            d = s.recv(200); s.close()
            if d.startswith(b"HTTP/1.1 200"):
                return True
        except OSError:
            pass
        time.sleep(2)
    return False


def proc_up():
    out = subprocess.run(["tasklist", "/FI", "PID eq %d" % pid, "/NH"], capture_output=True, text=True).stdout
    return str(pid) in out


stat = collections.Counter(); bykind = collections.defaultdict(collections.Counter); down_at = None
done = 0
while done < count:
    batch = [mutate() for _ in range(100)]
    with ThreadPoolExecutor(4) as ex:
        for k, line, err in ex.map(exchange, batch):
            stat[line.split(" ")[1] if line.startswith("HTTP") and " " in line else line] += 1
            bykind[k][line.split(" ")[1] if line.startswith("HTTP") and " " in line else line] += 1
    done += 100
    if not proc_up():
        down_at = done; print("SERVER PROCESS GONE after", done, "cases"); break
    if not alive():
        down_at = done; print("SERVER NOT ANSWERING after", done, "cases"); break
print("cases", done, "responses", dict(stat))
for k, v in sorted(bykind.items()):
    print("  %-11s %s" % (k, dict(v)))
print("server survived" if down_at is None else "SERVER FAILED at %d" % down_at)
