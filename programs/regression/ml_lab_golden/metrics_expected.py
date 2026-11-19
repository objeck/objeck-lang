"""Independent expected values for ml_lab_metrics.obs and ml_lab_metrics_ols.obs.

Nothing here calls Objeck. numpy fits the regressions (least squares) and scipy
gives the t quantile, mean, standard deviation and confidence interval, so the
Objeck library is checked against a second implementation, not against itself.

    python -I metrics_expected.py            # prints the tables the tests hold as literals

The data and the folds are plain integer formulas so both sides can build them
without sharing a random generator.
"""
import sys
import numpy as np
from scipy import stats

N = 60
SEEDS = [1, 2, 3, 4, 5]
MULT = [7, 11, 13, 17, 19]        # fold of sample i for seed s: ((i * MULT[s - 1] + 5 * s) % 60) // 12
LEVEL = 0.95


def data():
    x = np.zeros((N, 4))
    y = np.zeros(N)
    for i in range(N):
        for j in range(4):
            x[i, j] = ((i * (j + 3) * 17 + j * 31) % 97) / 97.0 - 0.5
        noise = (((i * 53 + 7) % 29) / 29.0 - 0.5) * 0.4
        y[i] = 1.0 + 2.0 * x[i, 0] - x[i, 1] + 0.5 * x[i, 2] + noise
    return x, y


def cv_metrics(x, y, k, seed):
    """Mean over the 5 folds of the held-out R-squared and RMSE, k leading features."""
    mult = MULT[seed - 1]
    fold = np.array([((i * mult + 5 * seed) % 60) // 12 for i in range(N)])
    r2s, rmses = [], []
    for f in range(5):
        tr, te = fold != f, fold == f
        a = np.hstack([np.ones((tr.sum(), 1)), x[tr][:, :k]])
        beta, *_ = np.linalg.lstsq(a, y[tr], rcond=None)
        pred = np.hstack([np.ones((te.sum(), 1)), x[te][:, :k]]) @ beta
        sse = float(((y[te] - pred) ** 2).sum())
        sst = float(((y[te] - y[te].mean()) ** 2).sum())
        r2s.append(1.0 - sse / sst)
        rmses.append(float(np.sqrt(sse / te.sum())))
    return float(np.mean(r2s)), float(np.mean(rmses))


def summary(values, level=LEVEL):
    v = np.array([a for a in values if np.isfinite(a)], dtype=float)
    n = len(v)
    if n == 0:
        return dict(n=0)
    out = dict(n=n, mean=float(v.mean()), min=float(v.min()), max=float(v.max()))
    if n > 1:
        sd = float(v.std(ddof=1))
        half = float(stats.t.ppf(1 - (1 - level) / 2, n - 1)) * sd / np.sqrt(n)
        out.update(std=sd, ci_low=out["mean"] - half, ci_high=out["mean"] + half)
    return out


def ols_tables():
    x, y = data()
    rows = {}
    for k in (1, 2, 3, 4):
        per = [cv_metrics(x, y, k, s) for s in SEEDS]
        rows[k] = dict(r2=[p[0] for p in per], rmse=[p[1] for p in per])
    return rows


def lit(values):
    return "[" + ", ".join(repr(float(v)) for v in values) + "]"


def main():
    rows = ols_tables()
    print("# per-seed CV R-squared and RMSE of the first k features, seeds 1..5")
    for k, d in rows.items():
        print(f"k={k}")
        print("  r2   ", lit(d["r2"]))
        print("  rmse ", lit(d["rmse"]))
        s = summary(d["r2"])
        print("  r2 summary", {a: (round(b, 12) if isinstance(b, float) else b) for a, b in s.items()})
    print("# t critical values (two-sided, level) for df")
    for df, level in [(1, 0.95), (2, 0.95), (4, 0.95), (9, 0.95), (29, 0.95), (4, 0.99), (4, 0.9), (120, 0.95)]:
        print(f"  df={df} level={level} t={stats.t.ppf(1 - (1 - level) / 2, df)!r}")
    print("# fixed arrays")
    for arr in ([0.91, 0.93, 0.92, 0.95, 0.90], [1.0, 2.0, 4.0, 8.0], [3.0, 3.0, 3.0], [5.0, 7.0]):
        print(" ", arr, summary(arr))


if __name__ == "__main__":
    main()
