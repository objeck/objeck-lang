"""Time a BERT-architecture encoder's forward pass on the CPU with ONNX Runtime.

Builds the graph with random weights (no torch, no transformers): embeddings,
L x (self-attention + GELU FFN, post-LayerNorm), CLS -> 1 logit.  Used to
calibrate a fine-tuning estimate: a training step costs about 3x a forward pass
(forward + backward of activations + backward of weights)."""
import sys, time
import numpy as np
import onnx
from onnx import helper, TensorProto, numpy_helper
import onnxruntime as ort

rng = np.random.default_rng(0)
inits = []
nodes = []
cnt = [0]

def name(p):
    cnt[0] += 1
    return f"{p}_{cnt[0]}"

def W(shape):
    n = name("w")
    inits.append(numpy_helper.from_array((rng.standard_normal(shape) * 0.02).astype(np.float32), n))
    return n

def C(arr, dt=np.int64):
    n = name("c")
    inits.append(numpy_helper.from_array(np.array(arr, dtype=dt), n))
    return n

def op(t, ins, **kw):
    o = name(t)
    nodes.append(helper.make_node(t, ins, [o], **kw))
    return o

def ln(x, d):
    return op("LayerNormalization", [x, W([d]), W([d])], axis=-1, epsilon=1e-12)

def build(layers, d, heads, ff, vocab=30522):
    hd = d // heads
    ids = "input_ids"; mask = "mask_add"
    x = op("Add", [op("Gather", [W([vocab, d]), ids]), W([128, d])])   # word + position
    x = ln(x, d)
    for _ in range(layers):
        qkv = op("Add", [op("MatMul", [x, W([d, 3 * d])]), W([3 * d])])
        q, k, v = [name("s") for _ in range(3)]
        nodes.append(helper.make_node("Split", [qkv, C([d, d, d])], [q, k, v], axis=-1))
        def heads_of(t):
            return op("Transpose", [op("Reshape", [t, C([0, 0, heads, hd])])], perm=[0, 2, 1, 3])
        qh, kh, vh = heads_of(q), heads_of(k), heads_of(v)
        kt = op("Transpose", [kh], perm=[0, 1, 3, 2])
        s = op("Mul", [op("MatMul", [qh, kt]), C(1.0 / np.sqrt(hd), np.float32)])
        p = op("Softmax", [op("Add", [s, mask])], axis=-1)
        a = op("Transpose", [op("MatMul", [p, vh])], perm=[0, 2, 1, 3])
        a = op("Reshape", [a, C([0, 0, d])])
        a = op("Add", [op("MatMul", [a, W([d, d])]), W([d])])
        x = ln(op("Add", [a, x]), d)
        h = op("Gelu", [op("Add", [op("MatMul", [x, W([d, ff])]), W([ff])])])
        h = op("Add", [op("MatMul", [h, W([ff, d])]), W([d])])
        x = ln(op("Add", [h, x]), d)
    cls = op("Gather", [x, C(0)], axis=1)
    logit = op("MatMul", [cls, W([d, 1])])
    nodes[-1].output[0] = "logit"
    g = helper.make_graph(nodes, "bert", [
        helper.make_tensor_value_info("input_ids", TensorProto.INT64, ["b", "s"]),
        helper.make_tensor_value_info("mask_add", TensorProto.FLOAT, ["b", 1, 1, "s"])],
        [helper.make_tensor_value_info("logit", TensorProto.FLOAT, ["b", 1])], inits)
    return helper.make_model(g, opset_imports=[helper.make_opsetid("", 20)], ir_version=9)

CONFIGS = {
    "bert-base": (12, 768, 12, 3072),
    "minilm-l12-h384": (12, 384, 12, 1536),
    "minilm-l6-h384": (6, 384, 12, 1536),
}

def params(layers, d, ff, vocab=30522):
    enc = layers * (4 * d * d + 2 * d * ff + 9 * d + ff)
    return enc, vocab * d + 128 * d

if __name__ == "__main__":
    which = sys.argv[1]; batch = int(sys.argv[2]); seq = 128
    layers, d, heads, ff = CONFIGS[which]
    model = build(layers, d, heads, ff).SerializeToString()
    enc, emb = params(layers, d, ff)
    ids = rng.integers(0, 30522, size=(batch, seq), dtype=np.int64)
    mask = np.zeros((batch, 1, 1, seq), dtype=np.float32)
    for level, lname in ((ort.GraphOptimizationLevel.ORT_ENABLE_BASIC, "basic"),
                         (ort.GraphOptimizationLevel.ORT_ENABLE_ALL, "all")):
        so = ort.SessionOptions(); so.graph_optimization_level = level
        sess = ort.InferenceSession(model, so, providers=["CPUExecutionProvider"])
        feed = {"input_ids": ids, "mask_add": mask}
        for _ in range(2):
            sess.run(None, feed)
        n = 5; t0 = time.perf_counter()
        for _ in range(n):
            sess.run(None, feed)
        dt = (time.perf_counter() - t0) / n
        gflop = 2 * enc * batch * seq / 1e9 + 4 * layers * batch * seq * seq * d / 1e9
        print(f"{which:16s} enc_params={enc/1e6:6.1f}M emb={emb/1e6:5.1f}M batch={batch:3d} opt={lname:5s} "
              f"forward={dt*1000:8.1f} ms/batch  {dt/batch*1000:7.2f} ms/seq  ~{gflop/dt:6.0f} GFLOP/s")
