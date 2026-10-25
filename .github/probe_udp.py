#!/usr/bin/env python3
# PROBE (do not merge): UdpAdvance's macOS path. A dual-stack UDP socket
# connected to ::1 (refused) moves to 127.0.0.1: AF_UNSPEC disconnect, connect
# the same socket, and when that fails a fresh dual-stack socket connected to
# the address is dup2'd onto the old descriptor. Then a round trip.
import ctypes, ctypes.util, errno, os, socket, time

libc = ctypes.CDLL(ctypes.util.find_library("c"), use_errno=True)

def show(label, value):
    print("%-34s %s" % (label, value), flush=True)

def unspec(fd):
    sa = (ctypes.c_ubyte * 16)()
    sa[0] = 16
    sa[1] = 0
    rc = libc.connect(fd, ctypes.byref(sa), 16)
    return rc, ctypes.get_errno()

def mapped(fam, sa):
    if fam == socket.AF_INET6:
        return sa
    return ("::ffff:" + sa[0], sa[1], 0, 0)

def dual():
    s = socket.socket(socket.AF_INET6, socket.SOCK_DGRAM)
    s.setsockopt(socket.IPPROTO_IPV6, socket.IPV6_V6ONLY, 0)
    return s

for server_host in ("127.0.0.1", "::1"):
    fam = socket.AF_INET6 if ":" in server_host else socket.AF_INET
    srv = socket.socket(fam, socket.SOCK_DGRAM)
    srv.bind((server_host, 0))
    srv.settimeout(0.05)
    port = srv.getsockname()[1]
    print("=== server %s:%d" % (server_host, port), flush=True)
    cands = [(f, sa) for f, _, _, _, sa in socket.getaddrinfo("localhost", port, socket.AF_UNSPEC, socket.SOCK_DGRAM, socket.IPPROTO_UDP)]
    show("localhost resolves to", cands)
    c = dual()
    i = 0
    c.connect(mapped(*cands[i]))
    heard = False
    for n in range(40):
        err = c.getsockopt(socket.SOL_SOCKET, socket.SO_ERROR)
        if err and i + 1 < len(cands):
            show("send %d: SO_ERROR" % n, errno.errorcode.get(err, err))
            show("  AF_UNSPEC", unspec(c.fileno()))
            i += 1
            try:
                c.connect(mapped(*cands[i]))
                show("  same socket moved", c.getsockname())
            except OSError as e:
                show("  same socket", e)
                f = dual()
                f.connect(mapped(*cands[i]))
                os.dup2(f.fileno(), c.fileno())
                f.close()
                show("  fresh socket dup2'd", c.getsockname())
        try:
            c.send(b"ping")
        except OSError as e:
            show("send %d raised" % n, e)
        try:
            data, addr = srv.recvfrom(64)
            show("server heard", (data, addr, n))
            heard = True
            break
        except socket.timeout:
            pass
    replied = False
    if heard:
        c.settimeout(0.5)
        for n in range(10):
            srv.sendto(b"pong", addr)
            try:
                show("client heard reply", c.recv(64))
                replied = True
                break
            except socket.timeout:
                pass
    show("RESULT", "heard=%s replied=%s" % (heard, replied))
    c.close()
    srv.close()
