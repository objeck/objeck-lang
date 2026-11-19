#!/usr/bin/env python3
"""float64 PyTorch loss curve for programs/regression/ml_cnn_parity64.obs.

The baseline (3 blocks 16/32/64, Dense 16, Dense 1) at 64 x 64, batches of 4, 20 Adam
steps at 1e-3. Weights, inputs and labels come from a small integer generator that the
Objeck test implements too, so no data file is needed: both sides draw the same doubles.
Prints the 20 losses to paste into the test. Run with a PyTorch environment:

    python tools/ml/cnn_f64_reference.py
"""
import math
import torch
import torch.nn as nn

state = 4242


def draw():
    global state
    state = (state * 1103515245 + 12345) % 2147483648
    return state / 2147483648.0


# (weight count, fan_in, fan_out, bias count), in layer order
GROUPS = [(16 * 27, 27, 144, 16), (32 * 144, 144, 288, 32), (64 * 288, 288, 576, 64), (16 * 2304, 2304, 16, 16), (16, 16, 1, 1)]
weights = []
for count, fi, fo, bias in GROUPS:
    scale = 2.0 * math.sqrt(6.0 / (fi + fo))
    weights.append([(draw() - 0.5) * scale for _ in range(count)])
    weights.append([(draw() - 0.5) * 0.2 for _ in range(bias)])

BATCH, NB, SIZE = 4, 2, 64
xs, ys = [], []
for _ in range(NB):
    xb, yb = [], []
    for _ in range(BATCH):
        shift = (draw() - 0.5) * 0.5
        yb.append(1.0 if shift > 0.0 else 0.0)
        xb.append([min(max(draw() + shift, 0.0), 1.0) for _ in range(3 * SIZE * SIZE)])
    xs.append(torch.tensor(xb, dtype=torch.float64).reshape(BATCH, 3, SIZE, SIZE))
    ys.append(torch.tensor(yb, dtype=torch.float64))

m = nn.Sequential(nn.Conv2d(3, 16, 3), nn.ReLU(), nn.MaxPool2d(2), nn.Conv2d(16, 32, 3), nn.ReLU(), nn.MaxPool2d(2),
                  nn.Conv2d(32, 64, 3), nn.ReLU(), nn.MaxPool2d(2), nn.Flatten(), nn.Linear(2304, 16), nn.ReLU(), nn.Linear(16, 1)).double()
layers = [l for l in m if isinstance(l, (nn.Conv2d, nn.Linear))]
with torch.no_grad():
    for i, l in enumerate(layers):
        l.weight.copy_(torch.tensor(weights[2 * i], dtype=torch.float64).reshape(l.weight.shape))
        l.bias.copy_(torch.tensor(weights[2 * i + 1], dtype=torch.float64))
opt = torch.optim.Adam(m.parameters(), lr=1e-3)
losses = []
for s in range(20):
    opt.zero_grad()
    loss = nn.functional.binary_cross_entropy_with_logits(m(xs[s % NB]).reshape(-1), ys[s % NB])
    loss.backward()
    opt.step()
    losses.append(loss.item())
print("[" + ", ".join(repr(v) for v in losses) + "]")
