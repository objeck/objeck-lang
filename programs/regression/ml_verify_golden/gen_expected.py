#!/usr/bin/env python3
"""
Regenerates cases.txt and expected.txt for programs/regression/ml_verify_metrics.obs.

The expected numbers are NOT computed by System.ML.Verification. They come from
scikit-learn's roc_curve (drop_intermediate=False: one point per distinct score), from brute-force
counting at each threshold with numpy, and from the EER rule written out again here in numpy
(and, for the continuous case, cross-checked against scipy's interp1d + brentq on the ROC polyline).

Definitions (the same as the bundle description of System.ML.Verification):
  a pair is accepted when score >= threshold; FAR = impostors accepted / impostors;
  FRR = genuine rejected / genuine; accuracy = correct / all.
  curve = one point per distinct score plus a final point at +inf (FAR 0, FRR 1), thresholds ascending.
  EER = value at the first point where FAR - FRR <= 0, linearly interpolated between that point and
  the one before it (EER and threshold), the threshold clamped to the highest score on the last segment.

    python programs/regression/ml_verify_golden/gen_expected.py

Needs numpy, scipy and scikit-learn. cases.txt lines are name|scores|labels (labels 1 = genuine pair);
expected.txt lines are name|key|value[,value...]; "inf" is +infinity.
"""
import os
import numpy as np
from scipy.interpolate import interp1d
from scipy.optimize import brentq
from sklearn.metrics import roc_curve

HERE = os.path.dirname(os.path.abspath(__file__))


def fmt(v):
    v = float(v)
    if v == float("inf"):
        return "inf"
    return repr(v)


def arr(a):
    return ",".join(fmt(x) for x in a)


def make_cases():
    rng = np.random.default_rng(20261120)
    cases = []
    # continuous scores like cosine similarities of embeddings: 2000 pairs, half genuine
    g = np.round(rng.normal(0.62, 0.14, 1000), 6)
    i = np.round(rng.normal(0.28, 0.12, 1000), 6)
    cases.append(("random_2000", np.concatenate([g, i]), np.concatenate([np.ones(1000), np.zeros(1000)])))
    # heavy ties: scores rounded to 2 decimals, unbalanced classes
    g = np.round(rng.normal(0.55, 0.2, 400), 2)
    i = np.round(rng.normal(0.30, 0.2, 1600), 2)
    cases.append(("ties_2dp", np.concatenate([g, i]), np.concatenate([np.ones(400), np.zeros(1600)])))
    cases.append(("all_same_score", np.full(10, 0.5), np.array([1, 0] * 5, float)))
    cases.append(("perfect", np.array([0.9, 0.8, 0.7, 0.3, 0.2, 0.1]), np.array([1, 1, 1, 0, 0, 0], float)))
    cases.append(("inverted", np.array([0.1, 0.2, 0.3, 0.7, 0.8, 0.9]), np.array([1, 1, 1, 0, 0, 0], float)))
    cases.append(("tiny_two", np.array([0.9, 0.1]), np.array([1, 0], float)))
    cases.append(("tiny_three", np.array([0.2, 0.9, 0.4]), np.array([0, 1, 1], float)))
    cases.append(("cross_class_tie", np.array([0.5, 0.5, 0.7, 0.3, 0.5]), np.array([1, 0, 1, 0, 1], float)))
    cases.append(("negative_scores", np.array([-0.2, -0.5, 0.1, -0.9, 0.3, -0.4]), np.array([1, 0, 1, 0, 1, 0], float)))
    cases.append(("last_segment", np.array([0.4, 0.4, 0.4, 0.9]), np.array([0, 0, 1, 0], float)))
    cases.append(("last_segment_top", np.array([0.9, 0.9, 0.9, 0.9, 0.2]), np.array([1, 1, 0, 0, 0], float)))
    # invalid input
    cases.append(("only_genuine", np.array([0.1, 0.2]), np.array([1, 1], float)))
    cases.append(("only_impostor", np.array([0.1, 0.2]), np.array([0, 0], float)))
    cases.append(("one_pair", np.array([0.1]), np.array([1], float)))
    return cases


def expected(name, s, y):
    y = y.astype(bool)
    G, I = int(y.sum()), int((~y).sum())
    out = []
    if G == 0 or I == 0:
        return [(name, "valid", "0")]
    out.append((name, "valid", "1"))

    fpr, tpr, thr = roc_curve(y, s, drop_intermediate=False)  # thr[0] = inf, FAR ascending
    far = fpr[::-1]
    frr = 1.0 - tpr[::-1]
    t = thr[::-1].copy()  # ascending, last = inf
    # brute-force check of every point against direct counting
    for j in range(len(t) - 1):
        acc = s >= t[j]
        assert abs(acc[~y].mean() - far[j]) < 1e-15 and abs((~acc[y]).mean() - frr[j]) < 1e-15
    assert far[-1] == 0.0 and frr[-1] == 1.0 and far[0] == 1.0 and frr[0] == 0.0
    k = len(t) - 1  # index of the +inf point
    out.append((name, "curve_thr", arr(t)))
    out.append((name, "curve_far", arr(far)))
    out.append((name, "curve_frr", arr(frr)))

    # EER by the rule
    d = far - frr
    j = int(np.argmax(d <= 0))
    if d[j] == 0:
        eer, eer_t = far[j], t[j]
    else:
        lam = d[j - 1] / (d[j - 1] - d[j])
        eer = far[j - 1] + lam * (far[j] - far[j - 1])
        eer_t = t[k - 1] if j == k else t[j - 1] + lam * (t[j] - t[j - 1])
    out.append((name, "eer", fmt(eer)))
    out.append((name, "eer_thr", fmt(eer_t)))

    if name == "random_2000":
        # the same EER from scipy: invert the ROC polyline where FRR = FAR
        f = interp1d(fpr, tpr)
        e = brentq(lambda x: 1.0 - x - f(x), 0.0, 1.0)
        assert abs(e - eer) < 1e-9, (e, eer)

    # best accuracy over the distinct scores and +inf, ties to the lowest threshold
    cands = list(np.unique(s)) + [float("inf")]
    accs = [(((s >= c) & y).sum() + ((s < c) & ~y).sum()) / len(s) for c in cands]
    b = int(np.argmax(accs))  # argmax returns the first maximum
    out.append((name, "best_acc", fmt(accs[b])))
    out.append((name, "best_thr", fmt(cands[b])))

    # probes: below everything, every observed score (up to 12), midpoints, above everything
    u = np.unique(s)
    probes = [u[0] - 1.0, u[0], u[-1], u[-1] + 1.0, (u[0] + u[-1]) / 2.0]
    probes += list(u[:: max(1, len(u) // 12)])
    probes += [(u[a] + u[a + 1]) / 2.0 for a in range(0, len(u) - 1, max(1, len(u) // 7))]
    pf, pr, pa = [], [], []
    for p in probes:
        acc = s >= p
        pf.append(acc[~y].mean())
        pr.append((~acc[y]).mean())
        pa.append((acc == y).mean())
    out.append((name, "probe_thr", arr(probes)))
    out.append((name, "probe_far", arr(pf)))
    out.append((name, "probe_frr", arr(pr)))
    out.append((name, "probe_acc", arr(pa)))

    # FRR at a FAR budget: the lowest threshold whose FAR <= budget
    budgets = [0.0, 0.001, 0.01, 0.1, 0.5, 1.0]
    bt, bf = [], []
    for bud in budgets:
        jj = int(np.argmax(far <= bud))
        bt.append(t[jj])
        bf.append(frr[jj])
    out.append((name, "budgets", arr(budgets)))
    out.append((name, "budget_thr", arr(bt)))
    out.append((name, "budget_frr", arr(bf)))

    # ROC rows as Axes->RocCurve takes them: sklearn's own fpr and tpr, thresholds with the first
    # (inf) replaced by the highest score
    rt = thr.copy()
    rt[0] = s.max()
    out.append((name, "roc_thr", arr(rt)))
    out.append((name, "roc_far", arr(fpr)))
    out.append((name, "roc_tpr", arr(tpr)))
    return out


def main():
    cases = make_cases()
    with open(os.path.join(HERE, "cases.txt"), "w", newline="\n") as f:
        for name, s, y in cases:
            f.write("%s|%s|%s\n" % (name, arr(s), ",".join("1" if v else "0" for v in y)))
    with open(os.path.join(HERE, "expected.txt"), "w", newline="\n") as f:
        for name, s, y in cases:
            for n, k, v in expected(name, s, y):
                f.write("%s|%s|%s\n" % (n, k, v))
    print("wrote", len(cases), "cases")


if __name__ == "__main__":
    main()
