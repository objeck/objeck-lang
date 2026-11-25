#!/usr/bin/env python3
"""Python + onnxruntime equivalent of fault_infer.obs (threads sharing one InferenceSession, even
workers call sess.run, odd workers use IO binding). Same arguments and output lines.

  python baseline_infer.py seed workers iters fault rate ep deadline_s model [k=v ...]

close_inflight has no equivalent primitive: onnxruntime's Python session has no close(). The analogue
used here drops the harness's reference and runs the garbage collector while workers use theirs, which
tests that nothing is freed under them (CPython reference counting makes that so).
"""
import gc, os, resource, sys, threading, time
import numpy as np
import onnxruntime as ort

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import faultlib

PRINT = threading.Lock()


def line(t):
    with PRINT:
        sys.stdout.write(t + "\n")
        sys.stdout.flush()


def ids_for(shift):
    return np.array([((i * 7 + shift * 3) % 25) for i in range(12)], dtype=np.int64).reshape(2, 6)


def feeds(ids, bad=False):
    if bad:
        ids = np.ones((2, 5), dtype=np.int64)
    return {"input_ids": ids, "attention_mask": np.ones((2, 6), dtype=np.int64),
            "token_type_ids": np.zeros((2, 6), dtype=np.int64)}


class W(threading.Thread):
    def __init__(self, holder, w, iters, seed, fault, rate, expected, state):
        super().__init__(daemon=True)
        self.holder, self.w, self.iters, self.seed, self.fault, self.rate = holder, w, iters, seed, fault, rate
        self.expected, self.state = expected, state

    def run(self):
        sess = self.holder["sess"]
        ok = refused = wrong = injected = inj_clean = maxms = 0
        bound_mode = self.w % 2 == 1
        if bound_mode:
            in_ids = ort.OrtValue.ortvalue_from_numpy(np.zeros((2, 6), dtype=np.int64))
            mask = ort.OrtValue.ortvalue_from_numpy(np.ones((2, 6), dtype=np.int64))
            types = ort.OrtValue.ortvalue_from_numpy(np.zeros((2, 6), dtype=np.int64))
            out = ort.OrtValue.ortvalue_from_numpy(np.zeros((2, 1), dtype=np.float32))
            io = sess.io_binding()
            io.bind_ortvalue_input("input_ids", in_ids)
            io.bind_ortvalue_input("attention_mask", mask)
            io.bind_ortvalue_input("token_type_ids", types)
            io.bind_ortvalue_output("logits", out)
        for i in range(self.iters):
            self.state[self.w] = i
            shift = (self.w * 13 + i) % 25
            want = self.expected[shift]
            bad = self.fault == "bad_shape" and faultlib.faulted(self.seed, self.w, i, self.rate)
            if bad:
                injected += 1
            t0 = time.monotonic()
            got = None
            try:
                if bound_mode:
                    if bad:
                        in_ids.update_inplace(np.ones((2, 5), dtype=np.int64))  # wrong shape for the bound buffer
                    else:
                        in_ids.update_inplace(ids_for(shift))
                    sess.run_with_iobinding(io)
                    got = out.numpy().copy()
                else:
                    got = sess.run(None, feeds(ids_for(shift), bad))[0]
            except Exception:
                got = None
            ms = int((time.monotonic() - t0) * 1000)
            maxms = max(maxms, ms)
            if got is None:
                refused += 1
                inj_clean += 1 if bad else 0
            elif np.allclose(got, want, atol=1e-3):
                ok += 1
            else:
                wrong += 1
        line("W id=%d bound=%s ok=%d refused=%d wrong=%d injected=%d inj_clean=%d maxms=%d"
             % (self.w, str(bound_mode).lower(), ok, refused, wrong, injected, inj_clean, maxms))
        self.state[self.w] = -1


def hog_cpu(cap):
    time.sleep(0.03)
    held = []
    try:
        for i in range(cap):
            held.append(np.ones(8388608, dtype=np.float64))
            if i % 10 == 0:
                line("HOG cpu blocks=%d (64 MB each)" % (i + 1))
        line("HOG cpu finished cap %d blocks without refusal" % cap)
    except MemoryError:
        line("HOG cpu MemoryError after %d blocks (caught, process continues)" % len(held))


def saboteur(kind):
    time.sleep(0.04)
    line("SABOTEUR firing %s" % kind)
    if kind == "thread_fault":
        print(10 // 0)
    else:
        os._exit(7)


def main():
    a = sys.argv[1:]
    seed, workers, iters, fault, rate, ep, deadline, model = int(a[0]), int(a[1]), int(a[2]), a[3], float(a[4]), a[5], int(a[6]), a[7]
    prov = {"cpu": "CPUExecutionProvider", "cuda": "CUDAExecutionProvider"}[ep]
    if prov not in ort.get_available_providers():
        line("SKIP provider %s not available in this onnxruntime build" % prov)
        sys.exit(3)
    opts = {}
    for kv in a[8:]:
        k, _, v = kv.partition("=")
        opts[k] = v
    so = ort.SessionOptions()
    so.log_severity_level = 3
    sess = ort.InferenceSession(model, so, providers=[(prov, opts)] if opts else [prov])
    expected = [sess.run(None, feeds(ids_for(s)))[0] for s in range(25)]
    holder = {"sess": sess}
    state = [0] * workers
    t0 = time.monotonic()
    ths = [W(holder, w, iters, seed, fault, rate, expected, state) for w in range(workers)]
    for t in ths:
        t.start()
    if fault == "close_inflight":
        def closer():
            time.sleep(0.025)
            holder["sess"] = None
            gc.collect()
            line("CLOSED (reference dropped, gc run) while workers run")
        threading.Thread(target=closer, daemon=True).start()
    elif fault == "oom_burst":
        threading.Thread(target=hog_cpu, args=(400,), daemon=True).start()
    elif fault.startswith("thread_"):
        threading.Thread(target=saboteur, args=(fault,), daemon=True).start()
    del sess
    while any(t.is_alive() for t in ths) and time.monotonic() - t0 < deadline:
        time.sleep(0.01)
    stuck = 0
    for w, t in enumerate(ths):
        if t.is_alive():
            stuck += 1
            line("HANG w=%d iter=%d" % (w, state[w]))
    line("SUMMARY workers=%d finished=%d stuck=%d wall_ms=%d" % (workers, workers - stuck, stuck, int((time.monotonic() - t0) * 1000)))
    sys.stdout.flush()
    os._exit(0)  # no interpreter shutdown: daemon threads still printing abort it (seen once in 10 runs)


if __name__ == "__main__":
    main()
