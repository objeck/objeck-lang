#!/usr/bin/env python3
"""Reference data for System.ML.Cnn, computed by PyTorch.

Writes one text file per case into programs/regression/data/cnn_parity/ (or --out).
The Objeck regression test programs/regression/ml_cnn_parity.obs reads them, builds
the same network from the case's `arch` line, loads the same weights and inputs,
runs forward and backward, and compares the output, the loss, every parameter
gradient and the input gradient. It needs neither Python nor PyTorch: the files are
committed, and this script only regenerates them.

Run it with a PyTorch environment (CPU is enough and is what is used, so the
numbers are deterministic):

    python tools/ml/cnn_parity.py [--out DIR]

With --device cuda the same cases are computed by PyTorch ON THE GPU (a CUDA build of
PyTorch, TF32 switched off so it is full FP32, deterministic algorithms), to check the
optional CUDA backend of System.ML.Cnn against PyTorch-on-GPU. Write them to a scratch
directory, not over the committed CPU files, and point ml_cnn_cuda_parity.obs at it:

    python tools/ml/cnn_parity.py --device cuda --out /tmp/cnn_parity_cuda
    obr ml_cnn_cuda_parity.obe /tmp/cnn_parity_cuda

Case file format, text, one number per line after each header:

    arch <C> <H> <W> | conv F K S valid|same | relu | maxpool K S | flatten | dense U | dropout R
    tensor <name> <count>
    <count values, one per line>

Tensors: weights (all parameters, layer by layer, weight then bias, PyTorch layout),
x (batch of images, NCHW), y (labels), loss, logits, grad (every parameter gradient,
same order as weights), ginput (gradient of the loss w.r.t. x), act<i> (output of
layer i, for the layers the case lists). A training case (`train` line) holds
w0, batches of x and y, the loss before every step and the weights after the last.

Everything is float32. Weights, inputs and labels are drawn in numpy float64 from a
fixed seed, written as decimal text with 9 places, and read back from that text
before they are used, so PyTorch and Objeck start from the same bits.

Design notes (also in docs/AI.md):
  * loss = BCEWithLogitsLoss (mean), the same stable form System.ML.Cnn uses;
  * Adam is torch.optim.Adam with its defaults (betas 0.9/0.999, eps 1e-8);
  * dropout is compared in evaluation mode only: PyTorch's generator is not ours,
    so a training-mode mask cannot match. Dropout's mask, scaling and backward are
    checked by gradient check and exact-value tests in ml_cnn_edges.obs;
  * 'same' padding with stride 1 is torch's padding='same', which, like ours and
    TensorFlow's, puts the extra pixel of an even kernel at the bottom/right.
"""
import argparse
import os
import sys

import numpy as np
import torch
import torch.nn as nn


SIGNIFICANT = False


def fmt(v):
    # 9 places after the point, the format of the committed files. --large uses 9 significant digits instead:
    # at 224 x 224 a gradient entry is about 1e-6, which 9 places would round to a relative error of 1e-3
    return np.format_float_positional(float(v), precision=9, unique=False, trim="-", fractional=not SIGNIFICANT)


def text_roundtrip(a):
    """The array as the Objeck side will read it: 9-place decimals, then float32."""
    flat = np.asarray(a, dtype=np.float64).ravel()
    return np.array([float(fmt(v)) for v in flat], dtype=np.float32).reshape(np.shape(a))


class Case:
    def __init__(self, name, arch, spec_c, spec_h, spec_w):
        self.name = name
        self.arch = arch  # list of layer tuples
        self.c, self.h, self.w = spec_c, spec_h, spec_w
        self.lines = []

    def arch_line(self):
        parts = ["arch %d %d %d" % (self.c, self.h, self.w)]
        for layer in self.arch:
            parts.append(" ".join(str(t) for t in layer))
        return " | ".join(parts)

    def tensor(self, name, array):
        flat = np.asarray(array, dtype=np.float64).ravel()
        self.lines.append("tensor %s %d" % (name, flat.size))
        self.lines.extend(fmt(v) for v in flat)

    def write(self, directory):
        path = os.path.join(directory, self.name + ".txt")
        with open(path, "w", newline="\n") as fh:
            fh.write(self.arch_line() + "\n")
            fh.write("\n".join(self.lines) + "\n")
        return path


def build(case):
    layers = []
    c, h, w = case.c, case.h, case.w
    for spec in case.arch:
        kind = spec[0]
        if kind == "conv":
            _, f, k, s, pad = spec
            if pad == "same":
                assert s == 1, "torch 'same' padding needs stride 1"
                layers.append(nn.Conv2d(c, f, k, stride=s, padding="same"))
                oh, ow = h, w
            else:
                layers.append(nn.Conv2d(c, f, k, stride=s, padding=0))
                oh, ow = (h - k) // s + 1, (w - k) // s + 1
            c, h, w = f, oh, ow
        elif kind == "relu":
            layers.append(nn.ReLU())
        elif kind == "maxpool":
            _, k, s = spec
            layers.append(nn.MaxPool2d(k, stride=s))
            h, w = (h - k) // s + 1, (w - k) // s + 1
        elif kind == "flatten":
            layers.append(nn.Flatten())
            c, h, w = c * h * w, 1, 1
        elif kind == "dense":
            layers.append(nn.Linear(c * h * w, spec[1]))
            c, h, w = spec[1], 1, 1
        elif kind == "dropout":
            layers.append(nn.Dropout(float(spec[1])))
        else:
            raise ValueError(kind)
    return nn.Sequential(*layers)


def params_of(model):
    out = []
    for m in model:
        if isinstance(m, (nn.Conv2d, nn.Linear)):
            out.extend([m.weight, m.bias])
    return out


def set_weights(model, flat):
    flat = torch.from_numpy(flat.astype(np.float32))
    k = 0
    with torch.no_grad():
        for p in params_of(model):
            n = p.numel()
            p.copy_(flat[k:k + n].reshape(p.shape))
            k += n
    assert k == flat.numel()


def count_params(model):
    return sum(p.numel() for p in params_of(model))


def draw_weights(model, rng):
    """Glorot-like uniform weights, small random biases (a zero bias would hide a bias bug)."""
    chunks = []
    for m in model:
        if isinstance(m, nn.Conv2d):
            fan_in = m.in_channels * m.kernel_size[0] * m.kernel_size[1]
            fan_out = m.out_channels * m.kernel_size[0] * m.kernel_size[1]
            lim = np.sqrt(6.0 / (fan_in + fan_out))
        elif isinstance(m, nn.Linear):
            lim = np.sqrt(6.0 / (m.in_features + m.out_features))
        else:
            continue
        chunks.append(rng.uniform(-lim, lim, m.weight.numel()))
        chunks.append(rng.uniform(-0.1, 0.1, m.bias.numel()))
    return np.concatenate(chunks)


DEVICE = "cpu"
DTYPE = torch.float32


def forward_backward(model, x, y, keep):
    model.to(DEVICE, DTYPE)
    model.eval()
    xt = torch.from_numpy(x).clone().to(DEVICE, DTYPE).requires_grad_(True)
    acts = {}
    for i, m in enumerate(model):
        if i in keep:
            m.register_forward_hook(lambda mod, inp, out, i=i: acts.__setitem__(i, out.detach().cpu().numpy().copy()))
    logits = model(xt).reshape(-1)
    loss = nn.functional.binary_cross_entropy_with_logits(logits, torch.from_numpy(y).to(DEVICE, DTYPE))
    model.zero_grad()
    loss.backward()
    grad = np.concatenate([p.grad.detach().cpu().numpy().ravel() for p in params_of(model)])
    return loss.item(), logits.detach().cpu().numpy(), grad, xt.grad.cpu().numpy(), acts


def make_case(name, arch, c, h, w, batch, seed, keep=None, drop_hooks=False):
    rng = np.random.default_rng(seed)
    case = Case(name, arch, c, h, w)
    model = build(case)
    weights = text_roundtrip(draw_weights(model, rng))
    x = text_roundtrip(rng.uniform(0.0, 1.0, (batch, c, h, w)))
    y = text_roundtrip((rng.uniform(size=batch) > 0.5).astype(np.float32))
    set_weights(model, weights)
    keep = set(range(len(model))) if keep is None else set(keep)
    loss, logits, grad, ginput, acts = forward_backward(model, x, y, keep)
    case.tensor("weights", weights)
    case.tensor("x", x)
    case.tensor("y", y)
    case.tensor("loss", [loss])
    case.tensor("logits", logits)
    case.tensor("grad", grad)
    case.tensor("ginput", ginput)
    for i in sorted(acts):
        case.tensor("act%d" % i, acts[i])
    return case, count_params(model)


def make_train_case(name, arch, c, h, w, batch, nbatches, steps, lr, seed):
    rng = np.random.default_rng(seed)
    case = Case(name, arch, c, h, w)
    model = build(case)
    w0 = text_roundtrip(draw_weights(model, rng))
    xs = text_roundtrip(rng.uniform(0.0, 1.0, (nbatches, batch, c, h, w)))
    # a learnable rule, so the loss visibly falls: label 1 when the batch item's mean is above 0.5
    # of a per-image brightness shift
    shift = rng.uniform(-0.25, 0.25, (nbatches, batch)).astype(np.float64)
    xs = text_roundtrip(np.clip(xs.astype(np.float64) + shift[:, :, None, None, None], 0.0, 1.0))
    ys = text_roundtrip((shift > 0).astype(np.float32))
    set_weights(model, w0)
    model.to(DEVICE, DTYPE)
    model.train()
    opt = torch.optim.Adam(model.parameters(), lr=lr)
    losses = []
    for step in range(steps):
        xb = torch.from_numpy(xs[step % nbatches]).to(DEVICE, DTYPE)
        yb = torch.from_numpy(ys[step % nbatches]).to(DEVICE, DTYPE)
        opt.zero_grad()
        loss = nn.functional.binary_cross_entropy_with_logits(model(xb).reshape(-1), yb)
        loss.backward()
        opt.step()
        losses.append(loss.item())
    wfinal = np.concatenate([p.detach().cpu().numpy().ravel() for p in params_of(model)])
    case.lines.append("train %d %d %d %s" % (steps, nbatches, batch, fmt(lr)))
    case.tensor("w0", w0)
    case.tensor("xs", xs)
    case.tensor("ys", ys)
    case.tensor("losses", losses)
    case.tensor("wfinal", wfinal)
    return case, count_params(model)


def baseline(size, blocks=3, dense=128, dropout=0.1):
    arch = []
    f = 16
    for _ in range(blocks):
        arch += [("conv", f, 3, 1, "valid"), ("relu",), ("maxpool", 2, 2)]
        f *= 2
    arch += [("flatten",), ("dense", dense), ("relu",), ("dropout", dropout), ("dense", 1)]
    return arch


def main():
    here = os.path.dirname(os.path.abspath(__file__))
    default_out = os.path.normpath(os.path.join(here, "..", "..", "programs", "regression", "data", "cnn_parity"))
    ap = argparse.ArgumentParser()
    ap.add_argument("--out", default=default_out)
    ap.add_argument("--device", default="cpu", choices=["cpu", "cuda"])
    ap.add_argument("--float64", action="store_true",
                    help="compute the reference in double precision (the same float32 inputs): the 'exact' answer, "
                         "to measure how far each float32 implementation is from the truth, not from another float32 one")
    ap.add_argument("--large", action="store_true",
                    help="also write the baseline at 64 x 64 and 224 x 224 and a 20-step Adam run at 64 x 64 "
                         "(megabytes: for a scratch directory, never the committed fixtures)")
    args = ap.parse_args()
    global DEVICE, DTYPE, SIGNIFICANT
    DEVICE = args.device
    DTYPE = torch.float64 if args.float64 else torch.float32
    SIGNIFICANT = args.large or args.float64
    if DEVICE == "cuda":
        if not torch.cuda.is_available():
            sys.exit("this PyTorch has no CUDA device")
        os.environ.setdefault("CUBLAS_WORKSPACE_CONFIG", ":4096:8")
        # full FP32: no TF32 Tensor Cores in the matrix multiplies or in cuDNN
        torch.backends.cuda.matmul.allow_tf32 = False
        torch.backends.cudnn.allow_tf32 = False
        torch.backends.cudnn.benchmark = False
        torch.backends.cudnn.deterministic = True
    if DEVICE == "cuda" and os.path.abspath(args.out) == os.path.abspath(default_out):
        sys.exit("--device cuda must not overwrite the committed CPU fixtures: give --out a scratch directory")
    os.makedirs(args.out, exist_ok=True)
    torch.set_num_threads(1)
    torch.use_deterministic_algorithms(True)

    cases = [
        make_case("conv3_valid", [("conv", 4, 3, 1, "valid"), ("flatten",), ("dense", 1)], 2, 7, 6, 3, 11),
        make_case("conv3_same", [("conv", 3, 3, 1, "same"), ("flatten",), ("dense", 1)], 3, 6, 5, 2, 12),
        make_case("conv4_same_even", [("conv", 2, 4, 1, "same"), ("flatten",), ("dense", 1)], 1, 5, 6, 2, 13),
        make_case("conv5_stride2", [("conv", 3, 5, 2, "valid"), ("flatten",), ("dense", 1)], 3, 11, 10, 2, 14),
        make_case("conv1x1", [("conv", 4, 1, 1, "valid"), ("flatten",), ("dense", 1)], 3, 4, 5, 3, 15),
        make_case("relu", [("conv", 3, 3, 1, "valid"), ("relu",), ("flatten",), ("dense", 1)], 1, 6, 6, 4, 16),
        make_case("maxpool2_odd", [("conv", 2, 3, 1, "same"), ("relu",), ("maxpool", 2, 2), ("flatten",), ("dense", 1)], 1, 7, 7, 3, 17),
        make_case("maxpool3_stride2", [("maxpool", 3, 2), ("flatten",), ("dense", 1)], 2, 9, 8, 2, 18),
        make_case("dense_stack", [("flatten",), ("dense", 5), ("relu",), ("dense", 1)], 3, 4, 4, 5, 19),
        make_case("dropout_eval", [("flatten",), ("dense", 6), ("dropout", 0.5), ("dense", 1)], 1, 4, 4, 3, 20),
        # the baseline at 32 x 32: only the block outputs are kept, to bound the file size
        make_case("baseline32", baseline(32), 3, 32, 32, 2, 21, keep=[2, 5, 8, 11, 13]),
        make_train_case("train_baseline32", baseline(32, dense=32, dropout=0.0), 3, 32, 32, 2, 3, 20, 1e-3, 22),
    ]
    if args.float64 and os.path.abspath(args.out) == os.path.abspath(default_out):
        sys.exit("--float64 must not overwrite the committed fixtures: give --out a scratch directory")
    if args.large:
        if os.path.abspath(args.out) == os.path.abspath(default_out):
            sys.exit("--large must not write into the committed fixtures: give --out a scratch directory")
        cases += [
            make_case("baseline64", baseline(64), 3, 64, 64, 4, 31, keep=[2, 5, 8, 11, 13]),
            make_case("baseline224", baseline(224, dense=16), 3, 224, 224, 2, 32, keep=[2, 5, 8, 11, 13]),
            make_train_case("train_baseline64", baseline(64, dropout=0.0), 3, 64, 64, 8, 3, 20, 1e-3, 33),
        ]
    total = 0
    for case, params in cases:
        path = case.write(args.out)
        size = os.path.getsize(path)
        total += size
        print("%-18s params %6d  %8d bytes" % (case.name, params, size))
    print("total %d bytes" % total)
    print("torch", torch.__version__, "numpy", np.__version__, "device", DEVICE, "dtype", DTYPE)


if __name__ == "__main__":
    sys.exit(main())
