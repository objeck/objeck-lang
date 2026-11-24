#!/usr/bin/env python3
"""Loopback fake LLM (Ollama /api/chat shape) with seeded network and protocol faults.

The unit under test is identified by the first user message "UNIT <agent> <run>".
Per unit the server answers a 3-call script: tool call, tool call, final answer.
A unit is faulted with probability --rate (faultlib.faulted), and the fault is applied
to the unit's FIRST model call only. Neither client under test retries.

Fault modes (--mode):
  none       no faults
  http500    HTTP 500 with a JSON error body
  garbage    200 with a body that is not JSON
  truncjson  200 with the first half of a valid JSON body, then a clean close
  wrongshape 200 with valid JSON that lacks "message"
  delay      the correct answer after --delay seconds
  hold       accept, read the request, never answer (until --hold seconds, then close)
  drop       close the connection without writing anything
  truncate   write the headers and half the body (Content-Length promises more), then close
  rst        send a TCP RST (SO_LINGER 0) instead of an answer
  slowloris  the correct answer, one byte every --trickle seconds
"""
import argparse, json, os, socket, struct, sys, threading, time
from socketserver import ThreadingMixIn, TCPServer, BaseRequestHandler

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import faultlib

ARGS = None


def read_request(sock):
    data = b""
    while b"\r\n\r\n" not in data:
        chunk = sock.recv(65536)
        if not chunk:
            return None, None
        data += chunk
    head, _, rest = data.partition(b"\r\n\r\n")
    length = 0
    for line in head.split(b"\r\n")[1:]:
        k, _, v = line.partition(b":")
        if k.strip().lower() == b"content-length":
            length = int(v.strip())
    while len(rest) < length:
        chunk = sock.recv(65536)
        if not chunk:
            break
        rest += chunk
    return head, rest


def reply_for(body):
    try:
        req = json.loads(body.decode("utf-8", "replace"))
    except Exception:
        return None, None, None, 0
    msgs = req.get("messages", [])
    a = r = None
    for m in msgs:
        if m.get("role") == "user" and str(m.get("content", "")).startswith("UNIT "):
            parts = m["content"].split()
            a, r = int(parts[1]), int(parts[2])
            break
    calls_done = sum(1 for m in msgs if m.get("role") == "tool")
    if a is None:
        return None, None, None, 0
    if calls_done < 2:
        n, _ = faultlib.expected_result(a, r, calls_done)
        msg = {"role": "assistant", "content": "", "tool_calls": [
            {"function": {"name": "work", "arguments": {"n": n}}}]}
    else:
        msg = {"role": "assistant", "content": faultlib.expected_answer(a, r)}
    out = {"model": "fake", "message": msg, "done": True, "done_reason": "stop",
           "prompt_eval_count": 10, "eval_count": 5}
    return a, r, json.dumps(out).encode(), calls_done


def http(code, body, ctype=b"application/json"):
    return (b"HTTP/1.1 %d X\r\nContent-Type: %s\r\nContent-Length: %d\r\nConnection: close\r\n\r\n"
            % (code, ctype, len(body))) + body


class Handler(BaseRequestHandler):
    def handle(self):
        s = self.request
        try:
            s.settimeout(ARGS.hold + 30)
            head, body = read_request(s)
            if head is None:
                return
            a, r, good, calls_done = reply_for(body)
            if good is None:
                s.sendall(http(400, b'{"error":"bad request"}'))
                return
            mode = ARGS.mode
            hit = calls_done == 0 and mode != "none" and faultlib.faulted(ARGS.seed, a, r, ARGS.rate)
            if not hit:
                s.sendall(http(200, good))
                return
            if mode == "http500":
                s.sendall(http(500, b'{"error":"injected internal error"}'))
            elif mode == "garbage":
                s.sendall(http(200, b"<html>not json at all \x00\xff</html>", b"text/html"))
            elif mode == "truncjson":
                s.sendall(http(200, good[: len(good) // 2]))
            elif mode == "wrongshape":
                s.sendall(http(200, b'{"unexpected":true}'))
            elif mode == "delay":
                time.sleep(ARGS.delay)
                s.sendall(http(200, good))
            elif mode == "hold":
                time.sleep(ARGS.hold)
            elif mode == "drop":
                pass
            elif mode == "truncate":
                s.sendall(http(200, good)[: len(good) // 2 + 80])
            elif mode == "rst":
                s.setsockopt(socket.SOL_SOCKET, socket.SO_LINGER, struct.pack("ii", 1, 0))
            elif mode == "slowloris":
                for b in http(200, good):
                    s.sendall(bytes([b]))
                    time.sleep(ARGS.trickle)
        except (OSError, ConnectionError):
            pass
        finally:
            try:
                s.close()
            except OSError:
                pass


class Server(ThreadingMixIn, TCPServer):
    allow_reuse_address = True
    daemon_threads = True
    request_queue_size = 256


def main():
    global ARGS
    p = argparse.ArgumentParser()
    p.add_argument("--port", type=int, default=0)
    p.add_argument("--mode", default="none")
    p.add_argument("--seed", type=int, default=1)
    p.add_argument("--rate", type=float, default=0.25)
    p.add_argument("--delay", type=float, default=3.0)
    p.add_argument("--hold", type=float, default=600.0)
    p.add_argument("--trickle", type=float, default=0.05)
    ARGS = p.parse_args()
    srv = Server(("127.0.0.1", ARGS.port), Handler)
    print("PORT %d" % srv.server_address[1], flush=True)
    try:
        srv.serve_forever()
    except KeyboardInterrupt:
        pass


if __name__ == "__main__":
    main()
