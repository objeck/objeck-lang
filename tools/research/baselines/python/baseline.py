"""Cross-runtime baseline (Python host). Workload definition: tools/research/baselines/README.md

    python baseline.py <model.onnx> <ep> <batch> <warmup_s> <measure_s>

Two threads in one process: ORT releases the GIL inside Run, the allocator thread spins on
perf_counter_ns. Every live/dropped block is a bytearray inside a one-element list so the cyclic
collector (which only tracks containers) sees the same population as a managed heap would.
"""
import gc
import json
import sys
import threading
import time

import numpy as np
import onnxruntime as ort

import os

if os.environ.get("PY_SWITCH"):
    sys.setswitchinterval(float(os.environ["PY_SWITCH"]))  # tuned configuration: GIL hand-off interval
model, ep = sys.argv[1], sys.argv[2]
batch, warmup, measure = int(sys.argv[3]), int(sys.argv[4]), int(sys.argv[5])

providers = ["CUDAExecutionProvider"] if ep == "cuda" else ["CPUExecutionProvider"]
session = ort.InferenceSession(model, providers=providers)
in_name = session.get_inputs()[0].name

count = batch * 3 * 224 * 224
idx = np.arange(count, dtype=np.int64)
x = ((idx * 7919) % 1000).astype(np.float64) / 1000.0
x = x.astype(np.float32).reshape(batch, 3, 224, 224)

phase = 0  # 0 warm-up, 1 measuring, 2 stop
seed = 12345
stalls = []
lat = []
first = []


def nxt():
    global seed
    seed = (seed * 1103515245 + 12345) % 2147483648
    return seed


def size():
    return 1024 + (nxt() // 256) % 64513


# gc pause accounting (cyclic collector, stop-the-world by construction: the GIL is held)
gc_pauses = []
_gc_t0 = [0]


def gc_cb(stage, info):
    if stage == "start":
        _gc_t0[0] = time.perf_counter_ns()
    elif phase == 1:
        gc_pauses.append((time.perf_counter_ns() - _gc_t0[0]) / 1e6)


gc.callbacks.append(gc_cb)

pns = time.perf_counter_ns


def allocator():
    global stalls
    live = [[bytearray(size())] for _ in range(8192)]
    prev_end = pns()
    k = 0
    ph = 0
    while ph < 2:
        target = prev_end + 100_000
        while pns() < target:
            pass
        sz = size()
        data = bytearray(sz)
        data[0] = k % 251
        data[sz - 1] = k % 251
        if k % 16 == 0:
            live[nxt() % 8192] = [data]
        end = pns()
        if ph == 1 and len(stalls) < 1_200_000:
            stalls.append((end - target) / 1000.0)
        prev_end = end
        k += 1
        if (k & 63) == 0:
            p = phase
            if p != ph:
                ph = p
                if p == 1:
                    stalls = []


def inference():
    global lat
    ph = 0
    while ph < 2:
        t0 = pns()
        out = session.run(None, {in_name: x})[0]
        t1 = pns()
        if not first:
            first.append(out.reshape(-1).copy())
        ph = phase
        if ph == 1 and len(lat) < 400_000:
            lat.append((t1 - t0) / 1e6)
        if ph == 0:
            lat = []


ta = threading.Thread(target=allocator, name="allocator")
ti = threading.Thread(target=inference, name="inference")
ta.start()
ti.start()
time.sleep(warmup)
g0 = [s["collections"] for s in gc.get_stats()]
w0 = time.perf_counter()
phase = 1
time.sleep(measure)
elapsed = time.perf_counter() - w0
g1 = [s["collections"] for s in gc.get_stats()]
phase = 2
ti.join()
ta.join()

st = np.sort(np.array(stalls))
it = np.sort(np.array(lat))
n, runs = len(st), len(it)


def pct(a, m, p):
    return float(a[int((m - 1) * p)])


f = first[0]
res = {
    "stall_us": {"n": n, "p50": float(st[n // 2]), "p95": pct(st, n, .95), "p99": pct(st, n, .99),
                 "p999": pct(st, n, .999), "max": float(st[-1])},
    "infer_ms": {"n": runs, "p50": float(it[runs // 2]), "p95": pct(it, runs, .95), "p99": pct(it, runs, .99),
                 "max": float(it[-1])},
    "img_s": runs * batch / elapsed,
    "gc": {"gen_collections": [b - a for a, b in zip(g0, g1)], "pause_total_ms": float(sum(gc_pauses)),
           "pause_max_ms": float(max(gc_pauses)) if gc_pauses else 0.0, "switchinterval_s": sys.getswitchinterval()},
    "checksum": {"sum": float(f.sum(dtype=np.float64)), "argmax0": int(np.argmax(f[:1000])), "v0": float(f[0]),
                 "v1": float(f[1]), "v2": float(f[2])},
}
print("RESULT " + json.dumps(res))
sys.stdout.flush()
import os
os._exit(0)
