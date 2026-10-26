"""Regenerates the reference fixtures embedded in programs/regression/ml_outlier_*.obs.

Each test holds a block between
    # BEGIN GENERATED FIXTURE
    # END GENERATED FIXTURE
that this script rewrites in place from outlier_ref.py's independent references.

Run from anywhere:  python programs/regression/ml_outlier_ref/gen_fixtures.py
"""
import math
import os
import sys
from decimal import Decimal

import numpy as np

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
import outlier_ref as ref  # noqa: E402

REG = os.path.dirname(HERE)
BEGIN = "\t# BEGIN GENERATED FIXTURE"
END = "\t# END GENERATED FIXTURE"


def lit(v):
    """A float literal Objeck parses exactly: the shortest round-trip digits,
    written without an exponent."""
    v = float(v)
    if math.isinf(v):
        return "Float->Inf()"
    s = format(Decimal(repr(v)), "f")
    if "." not in s:
        s += ".0"
    return s


def floats(name, values):
    values = list(np.ravel(values))
    lines = []
    for i in range(0, len(values), 6):
        lines.append("\t\t\t" + ", ".join(lit(v) for v in values[i:i + 6]))
    body = ",\n".join(lines)
    return f"\tfunction : {name}() ~ Float[] {{\n\t\treturn [\n{body}];\n\t}}\n"


def ints(name, values):
    values = [int(v) for v in values]
    if not values:
        return f"\tfunction : {name}() ~ Int[] {{\n\t\treturn Int->New[0];\n\t}}\n"
    lines = []
    for i in range(0, len(values), 16):
        lines.append("\t\t\t" + ", ".join(str(v) for v in values[i:i + 16]))
    body = ",\n".join(lines)
    return f"\tfunction : {name}() ~ Int[] {{\n\t\treturn [\n{body}];\n\t}}\n"


def scalar(name, v):
    return f"\tfunction : {name}() ~ Float {{\n\t\treturn {lit(v)};\n\t}}\n"


def rewrite(test, blocks):
    path = os.path.join(REG, test)
    with open(path, encoding="utf-8") as fh:
        text = fh.read()
    start = text.index(BEGIN)
    stop = text.index(END)
    generated = BEGIN + "\n\t# by ml_outlier_ref/gen_fixtures.py; do not edit by hand\n" + "\n".join(blocks) + END
    text = text[:start] + generated + text[stop + len(END):]
    with open(path, "w", encoding="utf-8", newline="\n") as fh:
        fh.write(text)
    print("wrote", test)


def zscore():
    X, _ = ref.make_data(7, 60, 6, 4)
    blocks = [floats("ExpectedMax", ref.zscore_scores(X, "max")),
              floats("ExpectedMean", ref.zscore_scores(X, "mean")),
              floats("ExpectedRms", ref.zscore_scores(X, "rms")),
              floats("ExpectedMeans", X.mean(axis=0)),
              floats("ExpectedSds", X.std(axis=0))]
    rewrite("ml_outlier_zscore.obs", blocks)


def lof():
    X, _ = ref.make_data(11, 150, 10, 3)
    Q, _ = ref.make_data(12, 15, 5, 3)
    lof20, _, _, _ = ref.lof_fit(X, 20)
    lof5, _, _, _ = ref.lof_fit(X, 5)
    novel = ref.lof_novel(X, Q, 20)
    t05 = ref.contamination_threshold(lof20, 0.05)
    flagged = np.nonzero(lof20 > t05)[0]
    blocks = [floats("ExpectedLof20", lof20), floats("ExpectedLof5", lof5),
              floats("ExpectedNovel20", novel), scalar("ExpectedThreshold05", t05),
              ints("ExpectedFlagged05", flagged)]
    rewrite("ml_outlier_lof.obs", blocks)


def iforest():
    X, _ = ref.make_data(21, 300, 15, 4)
    scores = ref.iforest_scores(X, 2000, 256, 5)
    blocks = [floats("ReferenceScores", scores),
              floats("ExpectedC", [ref.c_factor(n) for n in (1, 2, 3, 4, 5, 10, 100, 256, 1000, 100000)])]
    rewrite("ml_outlier_iforest.obs", blocks)


def sklearn_univariate(x, h):
    """sklearn fast_mcd's n_features == 1 branch, transcribed."""
    n = len(x)
    xs = np.sort(x)
    diff = xs[h:] - xs[:n - h]
    starts = np.where(diff == np.min(diff))[0]
    location = 0.5 * (xs[h + starts] + xs[starts]).mean()
    support = np.zeros(n, dtype=bool)
    support[np.argsort(np.abs(x - location), kind="stable")[:h]] = True
    var = np.var(x[support])
    return location, var, support


def mcd_blocks(cases):
    raw_loc, raw_cov, fin_loc, fin_cov, flags, counts, logdets = [], [], [], [], [], [], []
    for seed, n_in, n_out, p in cases:
        X, _ = ref.make_data(seed, n_in, n_out, p)
        starts = 300 if n_in + n_out <= 1000 else 60
        rl, rc, _, fl, fc, _, dist, _ = ref.mcd_reference(X, starts=starts, seed=seed)
        raw_loc += list(rl)
        logdets.append(np.linalg.slogdet(rc)[1])
        raw_cov += list(np.ravel(rc))
        fin_loc += list(fl)
        fin_cov += list(np.ravel(fc))
        t = ref.contamination_threshold(dist, 0.1)
        f = list(np.nonzero(dist > t)[0])
        counts.append(len(f))
        flags += f
        print("  mcd seed", seed, "flagged", len(f))
    return [ints("CaseSeeds", [c[0] for c in cases]), ints("CaseInliers", [c[1] for c in cases]),
            ints("CaseOutliers", [c[2] for c in cases]), ints("CaseFeatures", [c[3] for c in cases]),
            floats("RawLogDets", logdets), floats("RawLocations", raw_loc),
            floats("FinalLocations", fin_loc), floats("FinalCovariances", fin_cov),
            ints("FlaggedCounts", counts), ints("FlaggedRows", flags)]


def envelope():
    blocks = []
    q = []
    for dof in (1, 2, 3, 4, 6, 20):
        q += [ref.chi2_ppf(dof, 0.5), ref.chi2_ppf(dof, 0.975)]
    blocks.append(floats("ExpectedChi2", q))

    rewrite("ml_outlier_mcd.obs", mcd_blocks([(s, 180, 20, 3) for s in range(101, 131)]))
    blocks += mcd_blocks([(201, 720, 80, 3), (301, 1800, 200, 4), (401, 540, 60, 8)])

    # p = 1: sklearn's exact univariate algorithm
    X, _ = ref.make_data(77, 90, 10, 1)
    x = X[:, 0]
    n = len(x)
    h = int(math.ceil(0.5 * (n + 1 + 1)))
    loc, var, support = sklearn_univariate(x, h)
    raw_dist = (x - loc) ** 2 / var
    correction = np.median(raw_dist) / ref.chi2_ppf(1, 0.5)
    mask = raw_dist / correction < ref.chi2_ppf(1, 0.975)
    floc = x[mask].mean()
    fvar = x[mask].var()
    blocks += [scalar("UniRawLocation", loc), scalar("UniRawVariance", var),
               scalar("UniLocation", floc), scalar("UniVariance", fvar),
               floats("UniScores", (x - floc) ** 2 / fvar)]
    rewrite("ml_outlier_envelope.obs", blocks)


if __name__ == "__main__":
    which = sys.argv[1:] or ["zscore", "lof", "iforest", "envelope"]
    for name in which:
        globals()[name]()
