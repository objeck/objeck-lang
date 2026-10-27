#!/usr/bin/env python3
"""Why OneClassSVM flags more than nu * n training rows at the default tolerance.

Rebuilds the data of programs/regression/ml_one_class_svm_tolerance.obs (230
rows, 5 dimensions, System.ML.Random seed 3 -- the same MINSTD generator) and,
for nu = 0.01, 0.05 and 0.1 with an RBF kernel at gamma 'scale', reports:

  * the optimum, from the first-order SMO in svm_reference.py run to 1e-12:
    rho, objective, support vectors, and rows with decision < 0 and < -1e-9;
  * an independent numpy transcription of LIBSVM's solver (WSS3 working-set
    selection, the clipped update, rho as the mean over free alphas; no
    shrinking, no cache) stopped at tol = 1e-3, scikit-learn's default.

Compare with OneClassSVM: at tol 1e-3 it flags the same rows, with the same rho
and iteration count, and at tol 1e-10 it reaches the optimum's rho. The rows
flagged beyond nu * n are boundary rows within tol of 0, not a rho error.

Usage: python oneclass_tolerance.py
"""
import os
import sys

import numpy as np

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import svm_reference as R  # noqa: E402


class Minstd:
    """System.ML.Random: MINSTD, state = 16807 * state mod (2^31 - 1)."""

    def __init__(self, seed):
        s = seed % 2147483647
        if s <= 0:
            s += 2147483646
        self.state = s

    def real(self):
        self.state = (16807 * self.state) % 2147483647
        return self.state / 2147483647.0

    def real_range(self, lo, hi):
        return lo + (hi - lo) * self.real()


def data():
    rand = Minstd(3)
    n, d = 230, 5
    x = np.zeros((n, d))
    for i in range(n):
        for k in range(d):
            x[i, k] = rand.real() + rand.real() + rand.real() - 1.5
            if i >= 220:
                x[i, k] = rand.real_range(-4.0, 4.0)
    return x


def libsvm_one_class(K, nu, tol):
    """LIBSVM's Solver with y = +1, p = 0, C = 1 and its one-class start."""
    n = len(K)
    a = R.libsvm_one_class_start(n, nu)
    G = K @ a
    QD = np.diag(K).copy()
    iters = 0
    while True:
        up = a < 1.0
        low = a > 0.0
        mg = np.where(up, -G, -np.inf)
        i = int(np.argmax(mg))
        gmax = mg[i]
        gmax2 = np.max(np.where(low, G, -np.inf))
        best, j = np.inf, -1
        for t in range(n):
            if low[t]:
                b = gmax + G[t]
                if b > 0:
                    q = QD[i] + QD[t] - 2.0 * K[i, t]
                    v = -(b * b) / (q if q > 0 else 1e-12)
                    if v <= best:
                        best, j = v, t
        if gmax + gmax2 < tol or j < 0:
            break
        iters += 1
        q = QD[i] + QD[j] - 2.0 * K[i, j]
        q = q if q > 0 else 1e-12
        delta = (G[i] - G[j]) / q
        s = a[i] + a[j]
        ai, aj = a[i] - delta, a[j] + delta
        if s > 1.0:
            if ai > 1.0:
                ai, aj = 1.0, s - 1.0
        elif aj < 0.0:
            aj, ai = 0.0, s
        if s > 1.0:
            if aj > 1.0:
                aj, ai = 1.0, s - 1.0
        elif ai < 0.0:
            ai, aj = 0.0, s
        da, db = ai - a[i], aj - a[j]
        a[i], a[j] = ai, aj
        G += K[:, i] * da + K[:, j] * db
    return a, G, iters


def main():
    x = data()
    n, d = x.shape
    gamma = 1.0 / (d * x.var())
    K = R.kernel_matrix(x, x, 1, gamma, 3, 0.0)
    y, C, p = np.ones(n), np.ones(n), np.zeros(n)
    print("230 rows x 5, RBF, gamma 'scale' = %.6f" % gamma)
    for nu in (0.01, 0.05, 0.1):
        a, _ = R.smo_reference(K, y, C, p, tol=1e-12, a0=R.libsvm_one_class_start(n, nu))
        G = K @ a
        rho = R.rho_of(a, G, y, C)
        f = G - rho
        free = int(((a > 0) & (a < 1)).sum())
        bound = int((a >= 1).sum())
        print("nu=%.2f (nu*n=%.1f) optimum: rho %.7f obj %.7f, SV %d (free %d, bound %d), "
              "decision<0 %d, decision<-1e-9 %d" % (nu, nu * n, rho, 0.5 * a @ K @ a, free + bound,
                                                     free, bound, (f < 0).sum(), (f < -1e-9).sum()))
        a3, G3, it = libsvm_one_class(K, nu, 1e-3)
        rho3 = R.rho_of(a3, G3, y, C)
        f3 = G3 - rho3
        print("          LIBSVM solver at tol 1e-3: %d iterations, rho %.7f, decision<0 %d "
              "(most negative %.3g), at bound %d" % (it, rho3, (f3 < 0).sum(), f3.min(),
                                                    int((a3 >= 1).sum())))


if __name__ == "__main__":
    main()
