#!/usr/bin/env python3
"""Compare System.ML's KernelSVM against two independent references.

scikit-learn is not assumed. The references here share no code with the Objeck
solver and use different algorithms:

  exact  -- for n <= 7: enumerate every assignment of each alpha to
            {at 0, free, at C}, solve the KKT linear system for the free set,
            keep the feasible points, take the smallest objective. No SMO at all.
  smo    -- a plain first-order SMO (maximal violating pair; no second-order
            selection, no shrinking, no cache), run to tol 1e-10.

For every random problem the Objeck fit (at a tight tolerance) must give:
  * the same dual objective to 1e-6 (relative, floor 1);
  * the same support set, compared per group of identical (x, y) rows, since
    duplicates may split their alpha any way (a mismatch is one side at 0 and
    the other above 1e-6 * C);
  * the same predictions: the sign of the decision value on training and test
    rows, except where the reference's |f| < 1e-6, and values within 1e-5;
  * KKT conditions, recomputed here from Objeck's alphas: max violation <= its tol
    (times 1 + 1e-6 for rounding).

The sigmoid kernel is not positive semi-definite in general, which makes the dual
non-convex; problems whose Q has a negative eigenvalue are counted separately and
checked for KKT only, since two correct solvers may stop at different stationary
points.

Usage:
  python svm_reference.py --obr PATH/obr.exe --driver PATH/svm_driver.obe
      [--per-kernel 60] [--large 8] [--seed 20261026] [--work DIR]
Exit 0 when every comparison passes, 1 otherwise.
"""
import argparse
import collections
import itertools
import os
import subprocess
import sys

import numpy as np

KERNELS = ["linear", "rbf", "poly", "sigmoid"]


def kernel_matrix(a, b, code, gamma, degree, coef0):
    if code == 1:
        d2 = ((a[:, None, :] - b[None, :, :]) ** 2).sum(axis=2)
        return np.exp(-gamma * d2)
    dot = a @ b.T
    if code == 0:
        return dot
    if code == 2:
        return (gamma * dot + coef0) ** degree
    return np.tanh(gamma * dot + coef0)


def smo_reference(Q, y, C, p, tol=1e-10, max_iter=5_000_000):
    """First-order SMO: the maximal violating pair, an exact clipped step."""
    n = len(y)
    a = np.zeros(n)
    G = p.astype(float).copy()
    for _ in range(max_iter):
        v = -y * G
        up = ((y > 0) & (a < C)) | ((y < 0) & (a > 0))
        low = ((y > 0) & (a > 0)) | ((y < 0) & (a < C))
        vi = np.where(up, v, -np.inf)
        vj = np.where(low, v, np.inf)
        i = int(np.argmax(vi))
        j = int(np.argmin(vj))
        if vi[i] - vj[j] < tol:
            break
        # direction: a_i += t y_i, a_j -= t y_j keeps y'a fixed
        curv = Q[i, i] + Q[j, j] - 2.0 * y[i] * y[j] * Q[i, j]
        lim_i = (C[i] - a[i]) if y[i] > 0 else a[i]
        lim_j = a[j] if y[j] > 0 else (C[j] - a[j])
        lim = min(lim_i, lim_j)
        t = lim if curv <= 1e-15 else min((vi[i] - vj[j]) / curv, lim)
        a[i] += t * y[i]
        a[j] -= t * y[j]
        # snap a variable that the step took to its bound: a + (C - a) need not
        # round to C, and a variable left a hair inside stays selectable forever
        if t == lim_i:
            a[i] = C[i] if y[i] > 0 else 0.0
        if t == lim_j:
            a[j] = 0.0 if y[j] > 0 else C[j]
        a[i] = min(max(a[i], 0.0), C[i])
        a[j] = min(max(a[j], 0.0), C[j])
        G += t * (y[i] * Q[:, i] - y[j] * Q[:, j])
    else:
        raise RuntimeError("reference SMO did not converge")
    G = Q @ a + p  # recompute to remove drift
    return a, G


def exact_reference(Q, y, C, p, delta=0.0):
    """Exhaustive KKT enumeration; returns (alpha, objective) of the best point."""
    n = len(y)
    best = None
    for pattern in itertools.product((0, 1, 2), repeat=n):  # 0 lower, 1 upper, 2 free
        pat = np.array(pattern)
        a = np.where(pat == 1, C, 0.0).astype(float)
        free = np.where(pat == 2)[0]
        fixed = np.where(pat != 2)[0]
        if len(free) == 0:
            if abs(y @ a - delta) > 1e-9:
                continue
        else:
            k = len(free)
            A = np.zeros((k + 1, k + 1))
            A[:k, :k] = Q[np.ix_(free, free)]
            A[:k, k] = y[free]
            A[k, :k] = y[free]
            rhs = np.zeros(k + 1)
            rhs[:k] = -p[free] - Q[np.ix_(free, fixed)] @ a[fixed]
            rhs[k] = delta - y[fixed] @ a[fixed]
            sol, *_ = np.linalg.lstsq(A, rhs, rcond=None)
            if np.abs(A @ sol - rhs).max() > 1e-8:
                continue
            af = sol[:k]
            if (af < -1e-12).any() or (af > C[free] + 1e-12).any():
                continue
            a[free] = np.clip(af, 0.0, C[free])
        obj = 0.5 * a @ Q @ a + p @ a
        if best is None or obj < best[1] - 1e-15:
            best = (a, obj)
    return best


def rho_of(a, G, y, C):
    """LIBSVM's calculate_rho, which is the definition the decision value uses."""
    yG = y * G
    upper = a >= C
    lower = a <= 0
    free = ~upper & ~lower
    if free.any():
        return yG[free].mean()
    ub = np.inf
    lb = -np.inf
    for i in range(len(y)):
        if upper[i]:
            if y[i] < 0:
                ub = min(ub, yG[i])
            else:
                lb = max(lb, yG[i])
        elif lower[i]:
            if y[i] > 0:
                ub = min(ub, yG[i])
            else:
                lb = max(lb, yG[i])
    return (ub + lb) / 2.0


def max_violation(a, G, y, C):
    v = -y * G
    up = ((y > 0) & (a < C)) | ((y < 0) & (a > 0))
    low = ((y > 0) & (a > 0)) | ((y < 0) & (a < C))
    if not up.any() or not low.any():
        return 0.0
    return v[up].max() - v[low].min()


def make_problem(rng, pid, code, n, m, d, tiny, large):
    # two overlapping blobs, varied separation and balance
    pos_frac = rng.uniform(0.2, 0.8)
    yb = rng.random(n) < pos_frac
    yb[0] = True
    yb[1] = False
    sep = rng.uniform(0.0, 2.5)
    x = rng.normal(size=(n, d))
    x[yb, 0] += sep
    if rng.random() < 0.3 and n >= 6:
        # duplicate rows, sometimes with the opposite label
        for _ in range(rng.integers(1, 3)):
            i, j = rng.integers(0, n, size=2)
            x[j] = x[i]
            if rng.random() < 0.5:
                yb[j] = yb[i]
    yb[0] = True   # again: a copied label may have removed a class
    yb[1] = False
    if (x[0] == x[1]).all():
        x[1] += 1.0
    t = rng.normal(size=(m, d))
    C = float(rng.choice([0.01, 0.1, 1.0, 10.0, 100.0]))
    gamma = float(10.0 ** rng.uniform(-2, 0.5)) / d
    degree = int(rng.choice([1, 2, 3, 4]))
    coef0 = float(rng.choice([0.0, 0.5, 1.0]))
    if code == 3:
        gamma = float(10.0 ** rng.uniform(-2.5, -0.5)) / d
        coef0 = float(rng.choice([0.0, -0.5, -1.0]))
    cache = 1
    shrink = int(rng.random() < 0.8)
    return dict(id=pid, code=code, n=n, m=m, d=d, x=x, y=yb, t=t, C=C, gamma=gamma,
                degree=degree, coef0=coef0, tol=1e-9, cache=cache, shrink=shrink,
                tiny=tiny, large=large)


def fmt(v):
    return repr(float(v))


def write_problems(path, problems):
    with open(path, "w", newline="\n") as fh:
        for pr in problems:
            fh.write("P %d %d %d %d %d %s %s %d %s %s %d %d\n" % (
                pr["id"], pr["n"], pr["m"], pr["d"], pr["code"], fmt(pr["C"]),
                fmt(pr["gamma"]), pr["degree"], fmt(pr["coef0"]), fmt(pr["tol"]),
                pr["cache"], pr["shrink"]))
            for i in range(pr["n"]):
                fh.write(("1" if pr["y"][i] else "0") + " " +
                         " ".join(fmt(v) for v in pr["x"][i]) + "\n")
            for i in range(pr["m"]):
                fh.write(" ".join(fmt(v) for v in pr["t"][i]) + "\n")


def read_results(text):
    out = {}
    cur = None
    for line in text.splitlines():
        parts = line.split()
        if not parts:
            continue
        if parts[0] == "R":
            cur = int(parts[1])
            out[cur] = dict(obj=float(parts[2]), b=float(parts[3]), iters=int(parts[4]),
                            conv=int(parts[5]))
        elif parts[0] == "E":
            out[int(parts[1])] = dict(error=" ".join(parts[2:]))
            cur = None
        elif parts[0] in ("A", "F", "T") and cur is not None:
            out[cur][parts[0]] = np.array([float(v) for v in parts[1:]])
    return out


def compare(pr, res, stats, failures, notes):
    code = pr["code"]
    x, t = pr["x"], pr["t"]
    y = np.where(pr["y"], 1.0, -1.0)
    n = pr["n"]
    C = np.full(n, pr["C"])
    K = kernel_matrix(x, x, code, pr["gamma"], pr["degree"], pr["coef0"])
    Q = (y[:, None] * y[None, :]) * K
    p = -np.ones(n)
    tag = "%s#%d(n=%d,d=%d,C=%g)" % (KERNELS[code], pr["id"], n, pr["d"], pr["C"])

    if "error" in res:
        failures.append(tag + ": Objeck Fit failed: " + res["error"])
        return

    psd = np.linalg.eigvalsh(K).min() >= -1e-9 * max(1.0, np.abs(K).max())
    if res["conv"] != 1:
        # LIBSVM's iteration cap: allowed, and reported, only where the dual is
        # non-convex and SMO can cycle between stationary regions.
        if psd:
            failures.append(tag + ": did not converge in %d iterations" % res["iters"])
        else:
            stats["nc_capped"] += 1
            notes.append(tag + ": reached the iteration cap (%d), IsConverged false" % res["iters"])
        return
    a_o = res["A"]
    G_o = Q @ a_o + p
    viol = max_violation(a_o, G_o, y, C)
    if viol > pr["tol"] * (1 + 1e-6) + 1e-12:
        failures.append(tag + ": Objeck KKT violation %.3g > tol" % viol)
    if abs(y @ a_o) > 1e-9 * max(1.0, a_o.sum()):
        failures.append(tag + ": Objeck sum(alpha y) = %.3g" % (y @ a_o))
    if (a_o < 0).any() or (a_o > C * (1 + 1e-15)).any():
        failures.append(tag + ": Objeck alpha outside [0, C]")
    obj_o = 0.5 * a_o @ Q @ a_o + p @ a_o
    if abs(obj_o - res["obj"]) > 1e-9 * max(1.0, abs(obj_o)):
        failures.append(tag + ": reported objective %.12g != recomputed %.12g" % (res["obj"], obj_o))
    stats["kkt"] += 1

    prefix = ""
    if not psd:
        # Still compared, but a difference is reported as a note: a non-convex
        # dual may have several stationary points and both solvers are right.
        stats["nonconvex"] += 1
        prefix = "nc_"
        failures = notes

    refs = []
    if pr["tiny"]:
        best = exact_reference(Q, y, C, p)
        if best is None:
            failures.append(tag + ": exact enumeration found no feasible point")
            return
        refs.append(("exact", best[0]))
    try:
        a_r, _ = smo_reference(Q, y, C, p)
        refs.append(("smo", a_r))
    except RuntimeError as exc:
        failures.append(tag + ": " + str(exc))

    for name, a_r in refs:
        G_r = Q @ a_r + p
        obj_r = 0.5 * a_r @ Q @ a_r + p @ a_r
        if abs(obj_o - obj_r) > 1e-6 * max(1.0, abs(obj_r)):
            failures.append(tag + ": objective %.12g vs %s %.12g" % (obj_o, name, obj_r))
            continue
        stats[prefix + "obj_" + name] += 1

        rho_r = rho_of(a_r, G_r, y, C)
        coef = a_r * y
        f_train = K @ coef - rho_r

        # Support sets, per group of identical (x, y) rows (duplicates may split
        # their alpha any way). When K is positive definite and no row repeats,
        # the dual optimum is unique and the sets must be equal. Otherwise the
        # optimal alphas form a face: a row strictly outside the margin must
        # have alpha 0 and a row strictly inside it alpha C in every optimal
        # solution, but a row ON the margin may take any value in [0, C].
        groups = {}
        for i in range(n):
            groups.setdefault((tuple(x[i]), y[i]), []).append(i)
        unique = (len(groups) == n and
                  np.linalg.eigvalsh(K).min() > 1e-7 * max(1.0, np.abs(K).max()))
        mismatch = 0
        for idx in groups.values():
            so = a_o[idx].sum()
            sr = a_r[idx].sum()
            if unique:
                thr = 1e-6 * pr["C"]
                if (so == 0.0 and sr > thr) or (sr == 0.0 and so > thr):
                    mismatch += 1
            else:
                yf = y[idx[0]] * f_train[idx[0]]
                if yf > 1.0 + 1e-6 and so != 0.0:
                    mismatch += 1
                elif yf < 1.0 - 1e-6 and abs(so - pr["C"] * len(idx)) > 1e-9 * pr["C"] * len(idx):
                    mismatch += 1
        kind = "sv_unique_" if unique else "sv_face_"
        if mismatch:
            failures.append(tag + ": %d support-vector groups differ from %s (%s)" % (
                mismatch, name, "unique optimum" if unique else "off-margin rows"))
        else:
            stats[prefix + kind + name] += 1
        Kt = kernel_matrix(t, x, code, pr["gamma"], pr["degree"], pr["coef0"])
        f_test = Kt @ coef - rho_r
        bad = 0
        for f_r, f_o in ((f_train, res["F"]), (f_test, res["T"])):
            for fr, fo in zip(f_r, f_o):
                if abs(fr) >= 1e-6 and (fr > 0) != (fo > 0):
                    bad += 1
                elif abs(fr - fo) > 1e-5 * (1 + abs(fr)):
                    bad += 1
        if bad:
            failures.append(tag + ": %d decision values differ from %s" % (bad, name))
        else:
            stats[prefix + "pred_" + name] += 1


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--obr", required=True)
    ap.add_argument("--driver", required=True)
    ap.add_argument("--per-kernel", type=int, default=60)
    ap.add_argument("--large", type=int, default=8)
    ap.add_argument("--seed", type=int, default=20261026)
    ap.add_argument("--work", default=".")
    ap.add_argument("--obr-args", default="")
    args = ap.parse_args()

    rng = np.random.default_rng(args.seed)
    problems = []
    pid = 0
    for code in range(4):
        for k in range(args.per_kernel):
            tiny = k % 3 == 0
            if tiny:
                n = int(rng.integers(3, 8))
                d = int(rng.integers(1, 5))
            else:
                n = int(rng.integers(10, 61))
                d = int(rng.integers(1, 12))   # includes n < d when n is small
            pr = make_problem(rng, pid, code, n, 20, d, tiny, False)
            if code == 3 and k % 4 != 0:
                # tanh(gamma x.z) is positive semi-definite only near its
                # linear regime with a Gram matrix of full rank: give three in
                # four sigmoid problems more features than rows, so the convex case
                # is compared strictly too
                n = int(rng.integers(5, 16))
                pr = make_problem(rng, pid, code, n, 20, n + 4, n <= 7, False)
                pr["coef0"] = 0.0
                pr["gamma"] = 0.02 / pr["d"]
                pr["C"] = min(pr["C"], 10.0)
            problems.append(pr)
            pid += 1
        for k in range(args.large):
            # big enough that a 1 MB cache cannot hold every row: evictions and
            # swaps of cached columns under shrinking are exercised
            n = int(rng.integers(400, 700))
            d = int(rng.integers(2, 10))
            problems.append(make_problem(rng, pid, code, n, 50, d, False, True))
            pid += 1

    os.makedirs(args.work, exist_ok=True)
    path = os.path.join(args.work, "svm_problems.txt")
    write_problems(path, problems)
    cmd = [args.obr] + args.obr_args.split() + [args.driver, path]
    proc = subprocess.run(cmd, capture_output=True, text=True)
    if proc.returncode != 0:
        print(proc.stdout[-2000:])
        print(proc.stderr[-2000:])
        print("FAIL: driver exited %d" % proc.returncode)
        return 1
    results = read_results(proc.stdout)

    stats = {k: collections.Counter() for k in KERNELS}
    failures = []
    notes = []
    for pr in problems:
        res = results.get(pr["id"])
        if res is None:
            failures.append("problem %d: no result" % pr["id"])
            continue
        st = stats[KERNELS[pr["code"]]]
        st["problems"] += 1
        compare(pr, res, st, failures, notes)

    print("problems: %d (tiny exact-enumerated: %d, large n>=400: %d)" % (
        len(problems), sum(p["tiny"] for p in problems), sum(p["large"] for p in problems)))
    for k in KERNELS:
        print("  %-8s %s" % (k, ", ".join("%s=%d" % kv for kv in sorted(stats[k].items()))))
    for f in notes:
        print("NOTE (non-convex): " + f)
    for f in failures:
        print("FAIL: " + f)
    if failures:
        print("FAILED: %d comparisons" % len(failures))
        return 1
    print("PASS: every comparison agreed")
    return 0


if __name__ == "__main__":
    sys.exit(main())
