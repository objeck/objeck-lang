#!/usr/bin/env python3
"""Minimal Python equivalent of fault_agents.obs (threads, a scripted or HTTP backend, real tool
functions), for a coarse containment/hang baseline. Same arguments and the same output lines.

  python baseline_agents.py [--config naive|guarded] seed agents runs fault rate backend tool_limit_ms deadline_s [outdir]

naive    what a first version of a hand-written loop looks like: no HTTP timeout, tools called inline
         (a tool's time is measured after it returns, as API.Agent does).
guarded  what a careful engineer adds: a 3 s HTTP timeout and each tool run in a worker thread whose
         result is abandoned after the tool limit (the thread itself cannot be stopped).
The agent loop semantics (a failed tool becomes an error result and the loop goes on, results over
20000 characters are cut, a failed model call ends the run) copy API.Agent.
"""
import argparse, http.client, json, os, sys, threading, time, urllib.parse, concurrent.futures

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import faultlib

PRINT = threading.Lock()


def line(text):
    with PRINT:
        sys.stdout.write(text + "\n")
        sys.stdout.flush()


class CancelFlag:
    def __init__(self):
        self.ev = threading.Event()
        self.since = None

    def fire(self):
        self.since = time.monotonic()
        self.ev.set()

    def latency(self):
        return -1 if self.since is None else int((time.monotonic() - self.since) * 1000)


def work_tool(cfg, seed, agent, run, rate, flag, state, fault):
    """one tool call; state['calls'] counts calls in this unit"""
    def tool(n):
        call = state["calls"]
        state["calls"] += 1
        if call == 0 and fault != "none" and faultlib.faulted(seed, agent, run, rate):
            state["injected"] = True
            if fault == "tool_exception":
                raise RuntimeError("injected tool failure")
            if fault == "tool_fault":
                return str(10 // 0)
            if fault == "tool_timeout":
                time.sleep(0.25)
            elif fault == "tool_huge":
                s = "0123456789abcdef"
                for _ in range(20):
                    s = s + s
                return s
            elif fault == "tool_hang":
                while True:
                    time.sleep(1)
            elif fault == "cancel_coop":
                threading.Timer(0.1, flag.fire).start()
                for _ in range(100):
                    if flag.ev.is_set():
                        raise RuntimeError("cancelled")
                    time.sleep(0.01)
            elif fault == "cancel_uncoop":
                threading.Timer(0.1, flag.fire).start()
                time.sleep(1.0)
        return "R:%d" % (n * n + 7)
    return tool


def chat_http(base, conv, timeout):
    u = urllib.parse.urlparse(base)
    conn = http.client.HTTPConnection(u.hostname, u.port, timeout=timeout)
    try:
        body = json.dumps({"model": "fake", "stream": False, "messages": conv}).encode()
        conn.request("POST", "/api/chat", body, {"Content-Type": "application/json"})
        resp = conn.getresponse()
        data = resp.read()
        if resp.status < 200 or resp.status > 299:
            return None, "HTTP %d" % resp.status
        try:
            obj = json.loads(data)
        except Exception:
            return None, "the response was not JSON"
        if not isinstance(obj, dict) or "message" not in obj:
            return None, "the response did not contain a message"
        return obj["message"], None
    except (OSError, http.client.HTTPException) as e:
        return None, "no response: %s" % type(e).__name__
    finally:
        conn.close()


def run_unit(cfg, a, r, seed, fault, rate, backend, limit_ms, pool):
    flag = CancelFlag()
    state = {"calls": 0, "injected": False}
    tool = work_tool(cfg, seed, a, r, rate, flag, state, fault)
    conv = [{"role": "user", "content": "UNIT %d %d" % (a, r)}]
    detected = silent = 0
    out = "error"
    err = ""
    t0 = time.monotonic()
    tool_calls = 0
    final = None
    for step in range(10):
        if flag.ev.is_set():
            err = "cancelled before the model call"
            detected = 1
            break
        if backend == "scripted":
            if step < 2:
                n = (a * 31 + r * 7 + step) % 97
                msg = {"role": "assistant", "content": "", "tool_calls": [{"function": {"name": "work", "arguments": {"n": n}}}]}
            else:
                msg = {"role": "assistant", "content": faultlib.expected_answer(a, r)}
        else:
            msg, e = chat_http(backend, conv, 3.0 if cfg == "guarded" else None)
            if msg is None:
                err = e
                detected = 1
                break
        conv.append(msg)
        calls = msg.get("tool_calls") or []
        if not calls:
            final = msg.get("content", "")
            break
        for c in calls:
            n = c["function"]["arguments"]["n"]
            t1 = time.monotonic()
            status = "ok"
            try:
                if cfg == "guarded" and limit_ms > 0:
                    box = {}

                    def call():
                        try:
                            box["res"] = tool(n)
                        except Exception as ex2:
                            box["exc"] = ex2

                    th = threading.Thread(target=call, daemon=True)
                    th.start()
                    th.join(limit_ms / 1000.0)
                    if th.is_alive():
                        res = "Error: tool timed out"
                        status = "timeout"  # the thread is abandoned, not stopped
                    elif "exc" in box:
                        raise box["exc"]
                    else:
                        res = box["res"]
                else:
                    res = tool(n)
                    if limit_ms > 0 and (time.monotonic() - t1) * 1000 > limit_ms:
                        res = "Error: tool took too long, result discarded"
                        status = "timeout"
            except Exception as ex:
                res = "Error: the tool failed: %s" % ex
                status = "error"
            if status == "ok" and len(res) > 20000:
                res = res[:20000] + "... [truncated]"
            if status != "ok":
                detected = 1
            else:
                want = faultlib.expected_result(a, r, tool_calls)[1]
                if res != want and not res.endswith("... [truncated]"):
                    silent = 1
            tool_calls += 1
            conv.append({"role": "tool", "content": res})
    ms = int((time.monotonic() - t0) * 1000)
    if final is not None:
        if final == faultlib.expected_answer(a, r):
            out = "ok"
        else:
            out = "wrong"
            silent = 1
    else:
        detected = 1
    inj = 1 if state["injected"] else 0
    line("U a=%d r=%d inj=%d out=%s det=%d silent=%d ms=%d cancel_ms=%d err=%s"
         % (a, r, inj, out, detected, silent, ms, flag.latency(), err[:60].replace(" ", "_")))


def worker(cfg, a, runs, seed, fault, rate, backend, limit_ms, outdir, done, stuck):
    pool = concurrent.futures.ThreadPoolExecutor(max_workers=2, thread_name_prefix="tool%d" % a) if cfg == "guarded" else None
    f = None
    short = 0
    if outdir:
        f = open(os.path.join(outdir, "agent_%d.log" % a), "w")
    for r in range(runs):
        stuck[a] = r
        run_unit(cfg, a, r, seed, fault, rate, backend, limit_ms, pool)
        if f:
            try:
                f.write("a=%d r=%d\n" % (a, r))
                f.flush()
            except OSError:
                short += 1
    if f:
        try:
            f.close()
        except OSError:
            short += 1
        line("DISK a=%d short=%d runs=%d" % (a, short, runs))
    stuck[a] = -1
    done.append(a)


def saboteur(kind):
    time.sleep(0.15)
    line("SABOTEUR firing %s" % kind)
    if kind == "thread_fault":
        print(10 // 0)
    else:
        os._exit(7)


def stdin_mode():
    served = 0
    for text in sys.stdin:
        if text.strip():
            served += 1
            line("SERVED %d" % served)
    line("STDIN_END served=%d empty_reads=0 nil=True" % served)


def main():
    p = argparse.ArgumentParser()
    p.add_argument("--config", default="naive")
    p.add_argument("pos", nargs="+")
    ns = p.parse_args()
    a = ns.pos
    seed, agents, runs, fault, rate, backend, limit, deadline = int(a[0]), int(a[1]), int(a[2]), a[3], float(a[4]), a[5], int(a[6]), int(a[7])
    outdir = a[8] if len(a) > 8 else ""
    if fault == "stdin":
        stdin_mode()
        return
    done, stuck = [], [0] * agents
    t0 = time.monotonic()
    for i in range(agents):
        threading.Thread(target=worker, daemon=True, name="agent%d" % i,
                         args=(ns.config, i, runs, seed, fault, rate, backend, limit, outdir, done, stuck)).start()
    if fault.startswith("thread_"):
        threading.Thread(target=saboteur, args=(fault,), daemon=True).start()
    while len(done) < agents and time.monotonic() - t0 < deadline:
        time.sleep(0.01)
    n_stuck = 0
    for i in range(agents):
        if stuck[i] >= 0:
            n_stuck += 1
            line("HANG a=%d r=%d" % (i, stuck[i]))
    line("SUMMARY agents=%d finished=%d stuck=%d wall_ms=%d" % (agents, len(done), n_stuck, int((time.monotonic() - t0) * 1000)))
    sys.stdout.flush()
    os._exit(0)  # leave without joining threads, as the Objeck harness does when any are stuck


if __name__ == "__main__":
    main()
