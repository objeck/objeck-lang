"""Independent reference implementations for System.ML's outlier detectors.

scikit-learn is not installed on the development machine and nothing is
pip-installed for these tests, so the references are written here in numpy and
pure Python, from the published algorithms and scikit-learn's documented
behaviour, without looking at the Objeck code paths they check:

* z-scores (scipy.stats.zscore semantics, ddof 0)
* LOF by brute force (sklearn's reachability / lrd / 1e-10 rules)
* Isolation Forest with numpy's own generator (a different random stream, so
  only the score distribution is comparable, not individual values)
* the MCD by many random starts run to convergence (an approximation of the
  global optimum that FastMCD searches for), then sklearn's consistency
  correction and reweighting
* chi-square quantiles from the closed-form CDFs for 1 and even degrees of freedom

The datasets are generated from System.ML.Random's Lehmer generator using only
+, -, * and /, so the Objeck tests rebuild bit-identical data without
transcendental functions whose last bit could differ between runtimes.
"""
import math

import numpy as np


class Lehmer:
    """System.ML.Random: x <- 16807 x mod (2^31 - 1)."""

    def __init__(self, seed):
        s = seed % 2147483647
        if s <= 0:
            s += 2147483646
        self.state = s

    def next_int(self):
        self.state = (16807 * self.state) % 2147483647
        return self.state

    def next_int_bound(self, bound):
        return self.next_int() % bound

    def next_real(self):
        return self.next_int() / 2147483647.0


def gauss(rng):
    """Irwin-Hall approximation: the sum of 12 uniforms minus 6."""
    acc = 0.0
    for _ in range(12):
        acc += rng.next_real()
    return acc - 6.0


def make_data(seed, n_in, n_out, p, spread=8.0):
    """n_in correlated near-Gaussian inliers, then n_out uniform outliers in
    [-spread, spread]^p. x_j = z_j + 0.5 z_(j-1) for the inliers."""
    rng = Lehmer(seed)
    X = np.zeros((n_in + n_out, p))
    for i in range(n_in):
        z = [gauss(rng) for _ in range(p)]
        for j in range(p):
            v = z[j]
            if j > 0:
                v = v + 0.5 * z[j - 1]
            X[i, j] = v
    for i in range(n_in, n_in + n_out):
        for j in range(p):
            X[i, j] = rng.next_real() * (2.0 * spread) - spread
    y = np.array([False] * n_in + [True] * n_out)
    return X, y


# ---------------------------------------------------------------- z-score
def zscore_scores(X, rule="max"):
    mean = X.mean(axis=0)
    sd = X.std(axis=0)
    z = (X - mean) / sd
    a = np.abs(z)
    if rule == "max":
        return a.max(axis=1)
    if rule == "mean":
        return a.mean(axis=1)
    return np.sqrt((z * z).mean(axis=1))


# ---------------------------------------------------------------- percentile
def contamination_threshold(scores, c):
    """-np.percentile(-scores, 100 c): sklearn's -offset_."""
    return -np.percentile(-np.asarray(scores), 100.0 * c)


# ---------------------------------------------------------------- LOF
def _dists(A, B):
    return np.sqrt(((A[:, None, :] - B[None, :, :]) ** 2).sum(axis=2))


def lof_fit(X, k):
    n = X.shape[0]
    k = min(k, n - 1)
    D = _dists(X, X)
    np.fill_diagonal(D, np.inf)
    idx = np.argsort(D, axis=1, kind="stable")[:, :k]
    dist = np.take_along_axis(D, idx, axis=1)
    kdist = dist[:, k - 1]
    reach = np.maximum(dist, kdist[idx])
    lrd = 1.0 / (reach.mean(axis=1) + 1e-10)
    lof = (lrd[idx] / lrd[:, None]).mean(axis=1)
    return lof, lrd, kdist, k


def lof_novel(X, Q, k):
    lof, lrd, kdist, k = lof_fit(X, k)
    D = _dists(Q, X)
    idx = np.argsort(D, axis=1, kind="stable")[:, :k]
    dist = np.take_along_axis(D, idx, axis=1)
    reach = np.maximum(dist, kdist[idx])
    q_lrd = 1.0 / (reach.mean(axis=1) + 1e-10)
    return (lrd[idx] / q_lrd[:, None]).mean(axis=1)


# ---------------------------------------------------------------- isolation forest
def c_factor(n):
    if n <= 1:
        return 0.0
    if n == 2:
        return 1.0
    return 2.0 * (math.log(n - 1.0) + 0.5772156649015329) - 2.0 * (n - 1.0) / n


def iforest_scores(X, n_trees, psi, seed, Q=None):
    rng = np.random.default_rng(seed)
    n, p = X.shape
    psi = min(psi, n)
    max_depth = int(math.ceil(math.log2(max(psi, 2))))
    Q = X if Q is None else Q
    total = np.zeros(Q.shape[0])

    def grow(rows, depth):
        if depth >= max_depth or len(rows) <= 1:
            return ("leaf", len(rows), depth)
        feats = list(rng.permutation(p))
        for f in feats:
            col = X[rows, f]
            lo, hi = col.min(), col.max()
            if hi > lo:
                s = rng.uniform(lo, hi)
                if s >= hi:
                    s = lo
                left = rows[col <= s]
                right = rows[col > s]
                return ("node", f, s, grow(left, depth + 1), grow(right, depth + 1))
        return ("leaf", len(rows), depth)

    def path(node, x):
        while node[0] == "node":
            node = node[3] if x[node[1]] <= node[2] else node[4]
        return node[2] + c_factor(node[1])

    for _ in range(n_trees):
        rows = rng.choice(n, psi, replace=False)
        tree = grow(rows, 0)
        for i in range(Q.shape[0]):
            total[i] += path(tree, Q[i])
    return 2.0 ** (-(total / n_trees) / c_factor(psi))


# ---------------------------------------------------------------- MCD
def _logdet(C):
    sign, ld = np.linalg.slogdet(C)
    return ld if sign > 0 else -np.inf


def _estimate(X, support):
    S = X[support]
    loc = S.mean(axis=0)
    cov = np.cov(S.T, bias=True)
    return loc, np.atleast_2d(cov)


def _mahal(X, loc, cov):
    P = np.linalg.inv(cov)
    Xc = X - loc
    return (Xc @ P * Xc).sum(axis=1)


def _csteps(X, h, support, max_iter=200):
    loc, cov = _estimate(X, support)
    det = _logdet(cov)
    for _ in range(max_iter):
        d = _mahal(X, loc, cov)
        new_support = np.zeros(X.shape[0], dtype=bool)
        new_support[np.argsort(d, kind="stable")[:h]] = True
        nloc, ncov = _estimate(X, new_support)
        ndet = _logdet(ncov)
        if ndet >= det - 1e-14:
            if ndet < det:
                loc, cov, det, support = nloc, ncov, ndet, new_support
            break
        loc, cov, det, support = nloc, ncov, ndet, new_support
    return loc, cov, det, support


def chi2_ppf(dof, prob):
    """Closed-form chi-square CDF inverted by bisection."""
    def cdf(x):
        if x <= 0:
            return 0.0
        if dof % 2 == 1:
            # 2 Phi(t) - 1 - 2 phi(t) sum_k t^(2k-1) / (1*3*...*(2k-1)), t = sqrt(x)
            t = math.sqrt(x)
            phi = math.exp(-x / 2.0) / math.sqrt(2.0 * math.pi)
            total, term = 0.0, 1.0 / t
            for k in range(1, (dof - 1) // 2 + 1):
                term *= x / (2 * k - 1)
                total += term
            return math.erf(t / math.sqrt(2.0)) - 2.0 * phi * total
        term, total = 1.0, 1.0
        for k in range(1, dof // 2):
            term *= (x / 2.0) / k
            total += term
        return 1.0 - math.exp(-x / 2.0) * total
    lo, hi = 0.0, dof + 100.0
    for _ in range(300):
        mid = 0.5 * (lo + hi)
        if cdf(mid) < prob:
            lo = mid
        else:
            hi = mid
    return 0.5 * (lo + hi)


def mcd_reference(X, starts=500, seed=0):
    """Approximate global MCD: `starts` random h-subsets and `starts` random
    (p+1)-subsets, each run by C-steps to convergence, keeping the lowest
    determinant; then sklearn's consistency correction and reweighting. Returns
    raw and final (location, covariance), the final squared distances, and h."""
    n, p = X.shape
    h = int(math.ceil(0.5 * (n + p + 1)))
    rng = np.random.default_rng(seed)
    best = None
    for kind in ("h", "p1"):
        for _ in range(starts):
            support = np.zeros(n, dtype=bool)
            if kind == "h":
                support[rng.permutation(n)[:h]] = True
            else:
                order = rng.permutation(n)
                m = p + 1
                while np.linalg.matrix_rank(np.cov(X[order[:m]].T, bias=True)) < p:
                    m += 1
                loc, cov = X[order[:m]].mean(0), np.cov(X[order[:m]].T, bias=True)
                support[np.argsort(_mahal(X, loc, cov), kind="stable")[:h]] = True
            res = _csteps(X, h, support)
            if best is None or res[2] < best[2] - 1e-12:
                best = res
    raw_loc, raw_cov, _, raw_support = best
    raw_dist = _mahal(X, raw_loc, raw_cov)
    correction = np.median(raw_dist) / chi2_ppf(p, 0.5)
    dist = raw_dist / correction
    mask = dist < chi2_ppf(p, 0.975)
    loc, cov = _estimate(X, mask)
    final_dist = _mahal(X, loc, cov)
    return raw_loc, raw_cov, raw_support, loc, cov, mask, final_dist, h
