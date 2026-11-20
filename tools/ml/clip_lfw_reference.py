#!/usr/bin/env python3
"""Independent FAR / FRR / EER on LFW pairs, to check System.ML.Clip and System.ML.Verification end to end.

    python tools/ml/clip_lfw_reference.py PAIRS_FILE IMAGES_DIR SCORES_CSV STEP [CLIP_CHECKPOINT_DIR]

PAIRS_FILE   pairs.txt, pairsDevTest.txt or pairsDevTrain.txt of LFW (parsed here, not by Objeck)
IMAGES_DIR   lfw_funneled
SCORES_CSV   what programs/examples/ml/clip_face_verification.obs wrote ("pair,score,label"), for the pairs
             that example used (every STEP-th pair)
STEP         the same STEP given to the example
CLIP_CHECKPOINT_DIR  optional: a Hugging Face CLIP checkpoint (for example clip-vit-base-patch32). With it the
             same pairs are also scored by a pure-Python CLIP run (CLIPImageProcessor + PyTorch CLIPModel).

Prints, for the Objeck scores and (when a checkpoint is given) for the Python scores: FAR, FRR and accuracy at
the EER threshold, the EER, the threshold of the EER and the best accuracy, from scikit-learn's roc_curve with
the interpolation rule of System.ML.Verification. It also checks that the labels in SCORES_CSV are the labels of
the pair file, and prints the maximum absolute difference between Objeck's and Python's pair scores.

scikit-learn's fetch_lfw_pairs returns the same pair lists but as 62 x 47 cropped greyscale arrays, which are not
what CLIP is trained on, so the pair files are parsed here and the original images are read. (Run with
EXTRA_SITE=<site-packages with Pillow> when the interpreter has torch but not Pillow.)
"""
import os
import sys

if os.environ.get("EXTRA_SITE"):
    sys.path.append(os.environ["EXTRA_SITE"])

import numpy as np
from sklearn.metrics import roc_curve


def read_pairs(path, images):
    lines = open(path).read().replace("\r", "").split("\n")
    out = []
    for line in lines[1:]:
        if not line:
            continue
        f = line.split("\t")
        if len(f) == 3:
            out.append(("%s/%s/%s_%04d.jpg" % (images, f[0], f[0], int(f[1])), "%s/%s/%s_%04d.jpg" % (images, f[0], f[0], int(f[2])), 1))
        else:
            out.append(("%s/%s/%s_%04d.jpg" % (images, f[0], f[0], int(f[1])), "%s/%s/%s_%04d.jpg" % (images, f[2], f[2], int(f[3])), 0))
    return out


def metrics(scores, y):
    y = np.asarray(y, bool)
    scores = np.asarray(scores, float)
    fpr, tpr, thr = roc_curve(y, scores, drop_intermediate=False)
    far, frr, t = fpr[::-1], 1.0 - tpr[::-1], thr[::-1]
    k = len(t) - 1
    d = far - frr
    j = int(np.argmax(d <= 0))
    if d[j] == 0:
        eer, eer_t = far[j], t[j]
    else:
        lam = d[j - 1] / (d[j - 1] - d[j])
        eer = far[j - 1] + lam * (far[j] - far[j - 1])
        eer_t = t[k - 1] if j == k else t[j - 1] + lam * (t[j] - t[j - 1])
    cands = list(np.unique(scores)) + [float("inf")]
    accs = [(((scores >= c) & y).sum() + ((scores < c) & ~y).sum()) / len(scores) for c in cands]
    b = int(np.argmax(accs))
    out = {"EER": eer, "EER threshold": eer_t, "best accuracy": accs[b], "best accuracy threshold": cands[b]}
    for bud in (0.01, 0.001):
        jj = int(np.argmax(far <= bud))
        out["FRR at FAR<=%g" % bud] = frr[jj]
    # FAR and FRR at the threshold nearest the interpolated EER threshold, for the report
    acc = scores >= eer_t
    out["FAR at EER threshold"] = acc[~y].mean()
    out["FRR at EER threshold"] = (~acc[y]).mean()
    return out


def show(title, m):
    print(title)
    for k, v in m.items():
        print("  %-26s %.6f" % (k, v))


def python_scores(pairs, checkpoint):
    import torch
    from PIL import Image
    from transformers import CLIPImageProcessor, CLIPModel

    torch.set_num_threads(8)
    proc = CLIPImageProcessor.from_pretrained(checkpoint)
    model = CLIPModel.from_pretrained(checkpoint).eval()
    images = []
    for a, b, _ in pairs:
        for p in (a, b):
            if p not in images:
                images.append(p)
    index = {p: i for i, p in enumerate(images)}
    emb = []
    with torch.no_grad():
        for s in range(0, len(images), 32):
            batch = [Image.open(p).convert("RGB") for p in images[s : s + 32]]
            pix = proc(images=batch, return_tensors="pt")["pixel_values"]
            f = model.get_image_features(pixel_values=pix)
            f = getattr(f, "pooler_output", f) if not torch.is_tensor(f) else f
            emb.append(torch.nn.functional.normalize(f.double(), dim=-1).numpy())
            if (s // 32) % 40 == 0:
                print("  python: %d of %d images" % (s, len(images)), file=sys.stderr)
    emb = np.concatenate(emb)
    return np.array([float(emb[index[a]] @ emb[index[b]]) for a, b, _ in pairs])


def main():
    pairs_file, images, csv, step = sys.argv[1], sys.argv[2], sys.argv[3], int(sys.argv[4])
    pairs = read_pairs(pairs_file, images)[::step]
    y = [p[2] for p in pairs]
    rows = [l.split(",") for l in open(csv).read().split("\n")[1:] if l]
    obj = np.array([float(r[1]) for r in rows])
    obj_y = [int(r[2]) for r in rows]
    assert len(obj) == len(pairs), (len(obj), len(pairs))
    assert obj_y == y, "the labels in the scores file are not the labels of the pair file"
    print("%d pairs (%d genuine, %d impostor)" % (len(pairs), sum(y), len(y) - sum(y)))
    show("Objeck scores, metrics computed in Python (scikit-learn):", metrics(obj, y))
    if len(sys.argv) > 5:
        py = python_scores(pairs, sys.argv[5])
        show("Pure-Python CLIP scores (CLIPImageProcessor + PyTorch), same pairs:", metrics(py, y))
        print("max |Objeck score - Python score| over the pairs: %.3g" % np.abs(obj - py).max())
        print("mean |difference|: %.3g" % np.abs(obj - py).mean())


if __name__ == "__main__":
    main()
