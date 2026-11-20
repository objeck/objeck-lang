#!/usr/bin/env python3
"""
Regenerates programs/regression/ml_report_replicates.obs from numpy, scipy and statsmodels.

The expected numbers in that test are NOT computed by System.ML.Report or System.Stats. For
each sample size and confidence level they come from:

  mean, standard deviation (ddof = 1), min, max   numpy
  standard error and the t interval of the mean   scipy.stats.t.interval(level, n - 1, mean, sem),
                                                  cross-checked against statsmodels DescrStatsW.tconfint_mean
  paired t, df, p and the interval of the mean    scipy.stats.ttest_rel (statistic, pvalue and
  difference                                      confidence_interval), cross-checked against DescrStatsW
  Cohen's d_z and Hedges' g (paired)              closed forms: d_z = mean(d) / sd(d), g = d_z * J(df),
                                                  J(df) = Gamma(df/2) / (sqrt(df/2) * Gamma((df-1)/2))
  Welch t, df, p, difference and interval         scipy.stats.ttest_ind(equal_var=False) with its
                                                  confidence_interval

The datasets are integer formulas, so Objeck and Python build the very same doubles. Run it with
an interpreter that has numpy, scipy and statsmodels:

    python tools/report/gen_report_reference.py
"""
import math
import os
import numpy as np
from scipy import stats as st
from scipy import special as sp
from statsmodels.stats import weightstats as sw

ROOT = os.path.abspath(os.path.join(os.path.dirname(__file__), "..", ".."))
OUT = os.path.join(ROOT, "programs", "regression", "ml_report_replicates.obs")
TEMPLATE = os.path.join(os.path.dirname(os.path.abspath(__file__)), "ml_report_replicates.obs.in")


def num(v):
    """A float literal Objeck accepts."""
    text = repr(float(v))
    if "e" in text:
        mant, exp = text.split("e")
        if "." not in mant:
            mant += ".0"
        sign = "-" if exp.startswith("-") else ""
        text = "%se%s%d" % (mant, sign, int(exp.lstrip("+-")))
    elif "." not in text:
        text += ".0"
    return text


def hedges_j(df):
    return math.exp(sp.gammaln(df / 2.0) - sp.gammaln((df - 1) / 2.0)) / math.sqrt(df / 2.0)


def agree(a, b, what, tol=1e-9):
    """Two independent references must agree before either is trusted."""
    assert abs(a - b) <= tol * max(1.0, abs(b)), "%s: %r vs %r" % (what, a, b)


def va(i):
    return ((i * 37 + 11) % 101) / 100.0


def vb(i):
    return ((i * 53 + 7) % 89) / 100.0 + 0.05


def summary(x, level):
    x = np.array(x, dtype=float)
    n = len(x)
    mean = float(np.mean(x))
    sd = float(np.std(x, ddof=1))
    sem = float(st.sem(x))
    lo, hi = st.t.interval(level, n - 1, loc=mean, scale=sem)
    slo, shi = sw.DescrStatsW(x).tconfint_mean(alpha=1 - level)
    agree(lo, slo, "ci low")
    agree(hi, shi, "ci high")
    return [mean, sd, float(np.min(x)), float(np.max(x)), sem, float(lo), float(hi)]


def paired(x, y, level):
    x = np.array(x, dtype=float)
    y = np.array(y, dtype=float)
    r = st.ttest_rel(x, y)
    ci = r.confidence_interval(level)
    d = x - y
    n = len(d)
    slo, shi = sw.DescrStatsW(d).tconfint_mean(alpha=1 - level)
    agree(float(ci.low), slo, "paired ci low")
    agree(float(ci.high), shi, "paired ci high")
    dz = float(np.mean(d) / np.std(d, ddof=1))
    return [float(np.mean(d)), float(r.statistic), float(n - 1), float(r.pvalue), float(ci.low), float(ci.high), dz,
            dz * hedges_j(n - 1)]


def welch(x, y, level):
    r = st.ttest_ind(x, y, equal_var=False)
    ci = r.confidence_interval(level)
    return [float(np.mean(x) - np.mean(y)), float(r.statistic), float(r.df), float(r.pvalue), float(ci.low),
            float(ci.high)]


def arr(vals):
    return "[" + ", ".join(num(v) for v in vals) + "]"


def main():
    cases = [("n=2 level 0.95", 2, 0.95), ("n=3 level 0.90", 3, 0.90), ("n=5 level 0.95", 5, 0.95),
             ("n=12 level 0.99", 12, 0.99), ("n=30 level 0.95", 30, 0.95), ("n=100 level 0.80", 100, 0.80)]
    body = []
    for name, n, level in cases:
        a = [va(i) for i in range(n)]
        b = [vb(i) for i in range(n)]
        body.append('\t\tok := Case("%s", %d, %s,\n\t\t\t%s,\n\t\t\t%s,\n\t\t\t%s) & ok;' % (
            name, n, num(level), arr(summary(a, level)), arr(paired(a, b, level)), arr(welch(a, b, level))))
    text = open(TEMPLATE, encoding="utf-8").read()
    text = text.replace("@@CASES@@", "\n".join(body))
    for k, v in enumerate(summary([0.5, 0.7, 0.9], 0.95)):
        text = text.replace("@@NF%d@@" % k, num(v))
    for k, v in enumerate(summary([0.8, 0.6, 0.7], 0.95)):
        text = text.replace("@@SE%d@@" % k, num(v))
    with open(OUT, "w", newline="\n") as f:
        f.write(text)
    print("wrote", OUT)


if __name__ == "__main__":
    main()
