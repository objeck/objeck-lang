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


# ---------------------------------------------------------------- dense network
ACT = {"linear": (lambda z: z, lambda a: np.ones_like(a)),
       "relu": (lambda z: np.maximum(z, 0.0), lambda a: (a > 0.0).astype(float)),
       "sigmoid": (lambda z: 1.0 / (1.0 + np.exp(-z)), lambda a: a * (1.0 - a)),
       "tanh": (np.tanh, lambda a: 1.0 - a * a)}


class DenseRef:
    """A dense network trained by minibatch Adam on MSE, written from the
    textbook equations with numpy matrix products. The Glorot-uniform weights and
    the epoch shuffles are drawn from the same Lehmer stream System.ML.DenseNetwork
    uses, so a run can be replayed step for step; Adam is PyTorch's."""

    def __init__(self, sizes, hidden, output, seed, lr=0.001, batch=32, b1=0.9, b2=0.999, eps=1e-8):
        self.rng = Lehmer(seed)
        self.hidden, self.output = hidden, output
        self.lr, self.batch, self.b1, self.b2, self.eps = lr, batch, b1, b2, eps
        self.W, self.b = [], []
        for l in range(len(sizes) - 1):
            fi, fo = sizes[l], sizes[l + 1]
            limit = math.sqrt(6.0 / (fi + fo))
            w = np.zeros((fi, fo))
            for i in range(fi):
                for j in range(fo):
                    w[i, j] = (2.0 * self.rng.next_real() - 1.0) * limit
            self.W.append(w)
            self.b.append(np.zeros((1, fo)))
        self.mW = [np.zeros_like(w) for w in self.W]
        self.vW = [np.zeros_like(w) for w in self.W]
        self.mb = [np.zeros_like(b) for b in self.b]
        self.vb = [np.zeros_like(b) for b in self.b]
        self.t = 0

    def kind(self, l):
        return self.output if l == len(self.W) - 1 else self.hidden

    def forward(self, X):
        acts = [X]
        for l in range(len(self.W)):
            acts.append(ACT[self.kind(l)][0](acts[-1] @ self.W[l] + self.b[l]))
        return acts

    def predict(self, X):
        return self.forward(X)[-1]

    def train_batch(self, Xb, Yb):
        acts = self.forward(Xb)
        diff = acts[-1] - Yb
        delta = 2.0 * diff / diff.size
        gW, gb = [None] * len(self.W), [None] * len(self.W)
        for l in range(len(self.W) - 1, -1, -1):
            delta = delta * ACT[self.kind(l)][1](acts[l + 1])
            gW[l] = acts[l].T @ delta
            gb[l] = delta.sum(axis=0, keepdims=True)
            if l > 0:
                delta = delta @ self.W[l].T
        self.t += 1
        bias1 = 1.0 - self.b1 ** self.t
        bias2 = math.sqrt(1.0 - self.b2 ** self.t)
        for P, G, M, V in ((self.W, gW, self.mW, self.vW), (self.b, gb, self.mb, self.vb)):
            for l in range(len(P)):
                M[l] = self.b1 * M[l] + (1.0 - self.b1) * G[l]
                V[l] = self.b2 * V[l] + (1.0 - self.b2) * G[l] * G[l]
                P[l] = P[l] - (self.lr / bias1) * M[l] / (np.sqrt(V[l]) / bias2 + self.eps)
        return (diff * diff).sum()

    def fit(self, X, Y, epochs):
        n = X.shape[0]
        order = list(range(n))
        loss = 0.0
        for _ in range(epochs):
            for i in range(n - 1, 0, -1):
                k = self.rng.next_int() % (i + 1)
                order[i], order[k] = order[k], order[i]
            total, start = 0.0, 0
            while start < n:
                idx = order[start:start + self.batch]
                total += self.train_batch(X[idx], Y[idx])
                start += len(idx)
            loss = total / (n * Y.shape[1])
        return loss


def autoencoder_ref(X, hidden, epochs, seed, lr=0.001, batch=32):
    """Standardize (population sd; a constant feature keeps scale 1), train
    p -> hidden -> p with ReLU hidden and linear output, return the network, the
    last epoch's loss and the per-row mean squared reconstruction errors."""
    mean = X.mean(axis=0)
    sd = X.std(axis=0)
    sd[sd == 0.0] = 1.0
    Z = (X - mean) / sd
    p = X.shape[1]
    net = DenseRef([p] + list(hidden) + [p], "relu", "linear", seed, lr=lr, batch=batch)
    loss = net.fit(Z, Z, epochs)
    errors = ((net.predict(Z) - Z) ** 2).mean(axis=1)
    return net, loss, errors
