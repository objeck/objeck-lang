#!/usr/bin/env python3
# PROBE (do not merge): what does this OS do with the system calls UdpOpen /
# UdpAdvance / UdpSend make (core/vm/common.cpp)? A dual-stack UDP socket,
# connected to each address "localhost" resolves to in turn, moving on when
# SO_ERROR reports a refusal; one server bound to 127.0.0.1 alone, one to ::1.
import errno, select, socket, time

def show(label, value):
    print("%-34s %s" % (label, value), flush=True)

def resolve(port):
    out = socket.getaddrinfo("localhost", port, socket.AF_UNSPEC, socket.SOCK_DGRAM, socket.IPPROTO_UDP)
    return [(fam, sa) for fam, _, _, _, sa in out]

def mapped(fam, sa):
    if fam == socket.AF_INET6:
        return sa
    return ("::ffff:" + sa[0], sa[1], 0, 0)

def run(server_host, use_unspec):
    fam = socket.AF_INET6 if ":" in server_host else socket.AF_INET
    srv = socket.socket(fam, socket.SOCK_DGRAM)
    srv.bind((server_host, 0))
    srv.settimeout(0.05)
    port = srv.getsockname()[1]
    print("=== server %s:%d, AF_UNSPEC disconnect: %s" % (server_host, port, use_unspec), flush=True)
    cands = resolve(port)
    show("localhost resolves to", cands)
    c = socket.socket(socket.AF_INET6, socket.SOCK_DGRAM)
    c.setsockopt(socket.IPPROTO_IPV6, socket.IPV6_V6ONLY, 0)
    i = 0
    c.connect(mapped(*cands[i]))
    show("connected to", (cands[i], c.getsockname()))
    heard = False
    for n in range(40):
        err = c.getsockopt(socket.SOL_SOCKET, socket.SO_ERROR)
        if err:
            show("send %d: SO_ERROR" % n, "%d %s" % (err, errno.errorcode.get(err, "?")))
            if i + 1 < len(cands):
                i += 1
                try:
                    if use_unspec:
                        import ctypes, ctypes.util
                        libc = ctypes.CDLL(ctypes.util.find_library("c"), use_errno=True)
                        sa = (ctypes.c_ubyte * 16)()
                        sa[1] = 0  # sa_family AF_UNSPEC (BSD: sa_len, sa_family)
                        sa[0] = 16
                        rc = libc.connect(c.fileno(), ctypes.byref(sa), 16)
                        show("  AF_UNSPEC connect", "rc=%d errno=%d" % (rc, ctypes.get_errno()))
                    c.connect(mapped(*cands[i]))
                    show("  moved to", (cands[i], c.getsockname()))
                except OSError as e:
                    show("  move failed", e)
        try:
            c.send(b"ping")
        except OSError as e:
            show("send %d raised" % n, e)
        try:
            data, addr = srv.recvfrom(64)
            show("server heard", (data, addr, "after send", n))
            heard = True
            break
        except socket.timeout:
            pass
    show("RESULT heard", heard)
    c.close()
    srv.close()

for host in ("127.0.0.1", "::1"):
    for unspec in (False, True):
        try:
            run(host, unspec)
        except Exception as e:
            show("probe raised", repr(e))
