#!/usr/bin/env python3
"""
Reference numbers for System.ML.Boost (ml_boost.obs) from scikit-learn and XGBoost.

The datasets are generated here with the same recipe the regression tests use: the
MINSTD generator of System.ML.Random (state = 16807 * state mod 2147483647, a draw is
state / 2147483647), so the Objeck test and this script see the same numbers bit for
bit. Nothing is read from disk.

Run in a scratch virtual environment (nothing is installed into the repository):

    python3 -m venv /tmp/boostvenv
    /tmp/boostvenv/bin/pip install numpy scikit-learn xgboost
    /tmp/boostvenv/bin/python boost_ref.py

It prints the numbers pasted into the REF_* constants of ml_boost_accuracy.obs. Versions
used for the committed numbers: scikit-learn 1.9.1, xgboost 3.4.1, numpy 2.5.5.

Reading the output: XGBoost is run with tree_method="exact", min_child_weight 0, and with
the SAME starting scores as the Objeck model (base_score / base_margin), so its
predictions are expected to agree with ml_boost almost exactly (float32 leaf values).
scikit-learn grows trees on the first derivative only and fits leaves by line search, so
it is expected to agree on the loss / accuracy within a couple of percent, not on the
predictions.
"""
import math
import time
import numpy as np


class Lcg:
    def __init__(self, seed):
        s = seed % 2147483647
        if s <= 0:
            s += 2147483646
        self.s = s

    def real(self):
        self.s = (16807 * self.s) % 2147483647
        return self.s / 2147483647.0


def make_regression(seed=11, n=700, p=6):
    r = Lcg(seed)
    X = np.zeros((n, p))
    y = np.zeros(n)
    for i in range(n):
        for j in range(p):
            X[i, j] = 2.0 * r.real() - 1.0
        noise = r.real() - 0.5
        y[i] = (math.sin(3.0 * X[i, 0]) + X[i, 1] * X[i, 1] + 0.5 * X[i, 2] * X[i, 3]
                + 0.3 * X[i, 4] + 0.2 * noise)
    return X, y


def make_binary(seed=12, n=700, p=6):
    r = Lcg(seed)
    X = np.zeros((n, p))
    y = np.zeros(n, dtype=int)
    for i in range(n):
        for j in range(p):
            X[i, j] = 2.0 * r.real() - 1.0
        noise = r.real() - 0.5
        z = 1.5 * X[i, 0] + 2.0 * X[i, 1] * X[i, 2] - X[i, 3] + 1.2 * noise
        y[i] = 1 if z > 0.0 else 0
    return X, y


def make_multiclass(seed=13, n=700, p=5):
    r = Lcg(seed)
    X = np.zeros((n, p))
    y = np.zeros(n, dtype=int)
    for i in range(n):
        for j in range(p):
            X[i, j] = 2.0 * r.real() - 1.0
        s = [X[i, 0] + X[i, 1], X[i, 2] - X[i, 0], X[i, 3] + X[i, 4] * X[i, 1]]
        best, bk = -1e300, 0
        for k in range(3):
            v = s[k] + 0.6 * (r.real() - 0.5)
            if v > best:
                best, bk = v, k
        y[i] = bk
    return X, y


def make_large(seed=14, n=10000, p=20):
    r = Lcg(seed)
    X = np.zeros((n, p))
    y = np.zeros(n)
    for i in range(n):
        for j in range(p):
            X[i, j] = 2.0 * r.real() - 1.0
        y[i] = (math.sin(2.0 * X[i, 0]) + X[i, 1] * X[i, 2] + 0.5 * X[i, 3] - X[i, 4] * X[i, 4]
                + 0.3 * (r.real() - 0.5))
    return X, y


NTRAIN = 500


def mse(a, b):
    return float(np.mean((a - b) ** 2))


def logloss(y, p):
    p = np.clip(p, 1e-15, 1 - 1e-15)
    return float(-np.mean(y * np.log(p) + (1 - y) * np.log(1 - p)))


def mlogloss(y, P):
    P = np.clip(P, 1e-15, 1.0)
    return float(-np.mean(np.log(P[np.arange(len(y)), y])))


def main():
    from sklearn.ensemble import GradientBoostingRegressor, GradientBoostingClassifier
    import xgboost as xgb

    out = {}
    ncfg = dict(n_estimators=100, learning_rate=0.1, max_depth=3)

    # ---- regression, squared error
    X, y = make_regression()
    Xtr, ytr, Xte, yte = X[:NTRAIN], y[:NTRAIN], X[NTRAIN:], y[NTRAIN:]
    sk = GradientBoostingRegressor(random_state=0, **ncfg).fit(Xtr, ytr)
    out["REG sklearn test_mse"] = mse(sk.predict(Xte), yte)
    out["REG sklearn train_mse"] = mse(sk.predict(Xtr), ytr)
    for lam in (0.0, 1.0):
        m = xgb.XGBRegressor(n_estimators=100, learning_rate=0.1, max_depth=3, tree_method="exact",
                             reg_lambda=lam, min_child_weight=0, base_score=float(np.mean(ytr)),
                             n_jobs=1, random_state=0)
        m.fit(Xtr, ytr)
        pr = m.predict(Xte)
        out["REG xgboost lambda=%g test_mse" % lam] = mse(pr, yte)
        out["REG xgboost lambda=%g train_mse" % lam] = mse(m.predict(Xtr), ytr)
        out["REG xgboost lambda=%g first5 test predictions" % lam] = [round(float(v), 6) for v in pr[:5]]
        out["REG xgboost lambda=%g mean test prediction" % lam] = float(np.mean(pr))

    # ---- regression, absolute error and huber (sklearn only; xgboost's leaf rule differs)
    for loss in ("absolute_error", "huber"):
        sk = GradientBoostingRegressor(loss=loss, random_state=0, **ncfg).fit(Xtr, ytr)
        out["REG sklearn %s test_mae" % loss] = float(np.mean(np.abs(sk.predict(Xte) - yte)))
        out["REG sklearn %s test_mse" % loss] = mse(sk.predict(Xte), yte)

    # ---- binary
    X, y = make_binary()
    Xtr, ytr, Xte, yte = X[:NTRAIN], y[:NTRAIN], X[NTRAIN:], y[NTRAIN:]
    sk = GradientBoostingClassifier(random_state=0, **ncfg).fit(Xtr, ytr)
    P = sk.predict_proba(Xte)[:, 1]
    out["BIN sklearn test_accuracy"] = float(np.mean((P > 0.5) == yte))
    out["BIN sklearn test_logloss"] = logloss(yte, P)
    pos = float(np.mean(ytr))
    for lam in (0.0, 1.0):
        m = xgb.XGBClassifier(n_estimators=100, learning_rate=0.1, max_depth=3, tree_method="exact",
                              reg_lambda=lam, min_child_weight=0, base_score=pos, n_jobs=1, random_state=0)
        m.fit(Xtr, ytr)
        P = m.predict_proba(Xte)[:, 1]
        out["BIN xgboost lambda=%g test_accuracy" % lam] = float(np.mean((P > 0.5) == yte))
        out["BIN xgboost lambda=%g test_logloss" % lam] = logloss(yte, P)
        out["BIN xgboost lambda=%g first5 test probabilities" % lam] = [round(float(v), 6) for v in P[:5]]

    # ---- multiclass
    X, y = make_multiclass()
    Xtr, ytr, Xte, yte = X[:NTRAIN], y[:NTRAIN], X[NTRAIN:], y[NTRAIN:]
    sk = GradientBoostingClassifier(random_state=0, **ncfg).fit(Xtr, ytr)
    P = sk.predict_proba(Xte)
    out["MULTI sklearn test_accuracy"] = float(np.mean(P.argmax(1) == yte))
    out["MULTI sklearn test_logloss"] = mlogloss(yte, P)
    priors = np.array([np.mean(ytr == k) for k in range(3)])
    bm_tr = np.tile(np.log(priors), (len(ytr), 1))
    bm_te = np.tile(np.log(priors), (len(yte), 1))
    for lam in (0.0, 1.0):
        params = dict(objective="multi:softprob", num_class=3, eta=0.1, max_depth=3, tree_method="exact",
                      reg_lambda=lam, min_child_weight=0, nthread=1, seed=0)
        dtr = xgb.DMatrix(Xtr, label=ytr, base_margin=bm_tr)
        dte = xgb.DMatrix(Xte, base_margin=bm_te)
        bst = xgb.train(params, dtr, num_boost_round=100)
        P = bst.predict(dte)
        out["MULTI xgboost lambda=%g test_accuracy" % lam] = float(np.mean(P.argmax(1) == yte))
        out["MULTI xgboost lambda=%g test_logloss" % lam] = mlogloss(yte, P)
        out["MULTI xgboost lambda=%g first3 rows of probabilities" % lam] = [[round(float(v), 6) for v in row] for row in P[:3]]

    # ---- the performance dataset
    X, y = make_large()
    sk = GradientBoostingRegressor(random_state=0, n_estimators=15, learning_rate=0.1, max_depth=4)
    t = time.time()
    sk.fit(X[:8000], y[:8000])
    out["LARGE sklearn 15 stages depth 4 on 8000 rows: seconds"] = time.time() - t
    out["LARGE sklearn 15 stages test_mse"] = mse(sk.predict(X[8000:]), y[8000:])
    m = xgb.XGBRegressor(n_estimators=15, learning_rate=0.1, max_depth=4, tree_method="exact",
                         reg_lambda=0.0, min_child_weight=0, base_score=float(np.mean(y[:8000])), n_jobs=1)
    m.fit(X[:8000], y[:8000])
    out["LARGE xgboost exact 15 stages lambda=0 test_mse"] = mse(m.predict(X[8000:]), y[8000:])
    m = xgb.XGBRegressor(n_estimators=50, learning_rate=0.1, max_depth=4, tree_method="hist", max_bin=255,
                         reg_lambda=0.0, min_child_weight=0, base_score=float(np.mean(y[:8000])), n_jobs=1)
    m.fit(X[:8000], y[:8000])
    out["LARGE xgboost hist 50 stages max_bin=255 lambda=0 test_mse"] = mse(m.predict(X[8000:]), y[8000:])

    for k, v in out.items():
        print("%-62s %s" % (k, v))


if __name__ == "__main__":
    main()
