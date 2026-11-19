#!/usr/bin/env python3
"""
Regenerates the reference values in programs/regression/stats_*.obs from scipy and numpy.

The expected numbers in those tests are NOT computed by System.Stats: they come from
scipy.stats (t, norm, chi2, f, ttest_ind, ttest_rel, ttest_1samp, pearsonr and their
confidence_interval methods), scipy.special (betainc, gammainc, gammaln) and numpy.linalg
(lstsq, inv). The OLS summary formulas (R2, adjusted R2, F, AIC, BIC) are written out
here from the definitions statsmodels' RegressionResults uses. statsmodels itself was not
installed when this was written, so the OLS expectations are numpy/scipy, not a
statsmodels run.

    python tools/stats/gen_stats_reference.py          # rewrites the three test files

Needs numpy and scipy. The datasets are integer formulas, so Objeck and Python build the
very same numbers.
"""
import math
import os
import numpy as np
from scipy import stats as st
from scipy import special as sp

ROOT = os.path.abspath(os.path.join(os.path.dirname(__file__), "..", ".."))
OUT = os.path.join(ROOT, "programs", "regression")


def num(v):
    """A float literal Objeck accepts: a decimal point in the mantissa, no leading zero in the exponent."""
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


def seq(n, a, b, m, div):
    return np.array([((i * a + b) % m) / div for i in range(n)])


HEADER = """# EXTRA_LIBS: stats,gen_collect
#~
{doc}
Expected values were produced independently of System.Stats by tools/stats/gen_stats_reference.py
(scipy.stats / scipy.special / numpy). Do not edit the numbers by hand: regenerate them.
~#
use System.Stats;
"""

FOOT = """
	function : Report(name : String, passed : Bool) ~ Bool {
		if(passed) {
			"  ok: {$name}"->PrintLine();
		}
		else {
			"  FAIL: {$name}"->PrintLine();
		};
		return passed;
	}

	# relative tolerance, with a floor so an expected 0 can be matched
	function : Near(name : String, actual : Float, expected : Float, tol : Float) ~ Bool {
		diff := Float->Abs(actual - expected);
		limit := tol * Float->Abs(expected);
		if(limit < 1.0e-14) {
			limit := 1.0e-14;
		};
		good := diff <= limit;
		if(good = false) {
			"    {$name}: got {$actual}, expected {$expected}"->PrintLine();
		};
		return Report(name, good);
	}

	function : Seq(n : Int, a : Int, b : Int, m : Int, div : Float) ~ Float[] {
		out := Float->New[n];
		each(i : n) {
			out[i] := ((i * a + b) % m)->As(Float) / div;
		};
		return out;
	}
}
"""


class Test:
    def __init__(self, cls, doc):
        self.cls = cls
        self.lines = []
        self.doc = doc

    def add(self, line):
        self.lines.append("\t\t" + line)

    def near(self, name, expr, expected, tol="1.0e-9"):
        self.add('ok := Near("%s", %s, %s, %s) & ok;' % (name, expr, num(expected), tol))

    def write(self, fname):
        body = HEADER.format(doc=self.doc)
        body += "\nclass %s {\n\tfunction : Main(args : String[]) ~ Nil {\n\t\tok := true;\n" % self.cls
        body += "\n".join(self.lines)
        body += '\n\n\t\tif(ok = false) {\n\t\t\tRuntime->Exit(1);\n\t\t};\n\t\t"%s: ok"->PrintLine();\n\t}\n' % fname[:-4]
        body += FOOT
        with open(os.path.join(OUT, fname), "w", newline="\n") as f:
            f.write(body)


def dist_test():
    t = Test("StatsDist", "System.Stats distributions against scipy: Student t (p-value, CDF, quantile) at small and\nlarge degrees of freedom, normal, chi-square, F, the incomplete beta and log-gamma.")
    for x, df in [(2.0, 1), (0.5, 3), (-1.7, 5), (2.5, 10), (3.0, 30), (1.96, 1000), (4.5, 4000), (10.0, 3), (0.0, 7), (-8.0, 2.5), (1.2, 0.5), (40.0, 12), (0.01, 50)]:
        t.near("t p two-sided t=%s df=%s" % (x, df), "Dist->StudentTwoSidedP(%s, %s)" % (num(x), num(df)), 2 * st.t.sf(abs(x), df))
        t.near("t cdf t=%s df=%s" % (x, df), "Dist->StudentCdf(%s, %s)" % (num(x), num(df)), st.t.cdf(x, df))
    for p, df in [(0.975, 1), (0.975, 2), (0.975, 5), (0.975, 10), (0.975, 30), (0.975, 100), (0.975, 4000), (0.995, 8), (0.9, 3), (0.001, 7), (0.999999, 5), (0.6, 2.5), (0.975, 18.37)]:
        t.near("t quantile p=%s df=%s" % (p, df), "Dist->StudentQuantile(%s, %s)" % (num(p), num(df)), st.t.ppf(p, df))
    t.add('ok := Near("t quantile 0.5 is 0", Dist->StudentQuantile(0.5, 10.0), 0.0, 1.0e-12) & ok;')
    for x in [-3.0, -1.0, 0.0, 0.5, 1.96, 5.0, -8.0]:
        t.near("normal cdf %s" % x, "Dist->NormalCdf(%s)" % num(x), st.norm.cdf(x))
        t.near("normal two-sided p %s" % x, "Dist->NormalTwoSidedP(%s)" % num(x), 2 * st.norm.sf(abs(x)))
    for p in [0.975, 0.9, 0.5001, 1e-8, 0.3, 0.999]:
        t.near("normal quantile %s" % p, "Dist->NormalQuantile(%s)" % num(p), st.norm.ppf(p))
    for x, df in [(3.84, 1), (10.0, 5), (50.0, 20), (0.5, 3), (200.0, 10), (0.0, 4)]:
        t.near("chi2 cdf x=%s df=%s" % (x, df), "Dist->ChiSquareCdf(%s, %s)" % (num(x), num(df)), st.chi2.cdf(x, df))
        t.near("chi2 sf x=%s df=%s" % (x, df), "Dist->ChiSquareSf(%s, %s)" % (num(x), num(df)), st.chi2.sf(x, df))
    for f, d1, d2 in [(4.0, 2, 30), (1.5, 5, 100), (20.0, 3, 10), (2.5, 1, 4000), (0.3, 4, 7), (300.0, 6, 500)]:
        t.near("F cdf f=%s d1=%s d2=%s" % (f, d1, d2), "Dist->FCdf(%s, %s, %s)" % (num(f), num(d1), num(d2)), st.f.cdf(f, d1, d2))
        t.near("F sf f=%s d1=%s d2=%s" % (f, d1, d2), "Dist->FSf(%s, %s, %s)" % (num(f), num(d1), num(d2)), st.f.sf(f, d1, d2))
    for x, a, b in [(0.3, 2.0, 5.0), (0.9, 0.5, 0.5), (0.5, 100.0, 100.0), (0.01, 1.5, 300.0), (0.999, 2000.0, 0.5)]:
        t.near("betainc x=%s a=%s b=%s" % (x, a, b), "Dist->BetaInc(%s, %s, %s)" % (num(x), num(a), num(b)), sp.betainc(a, b, x))
    for a, x in [(0.5, 0.5), (3.0, 2.0), (20.0, 30.0), (2.0, 100.0)]:
        t.near("gammainc a=%s x=%s" % (a, x), "Dist->GammaInc(%s, %s)" % (num(a), num(x)), sp.gammainc(a, x))
    for x in [0.1, 0.5, 1.0, 2.5, 10.0, 100.5, 1000.0, 1.0e5]:
        t.near("lgamma %s" % x, "Dist->LogGamma(%s)" % num(x), sp.gammaln(x), "1.0e-12")
    # domain errors are NaN, not crashes
    for expr in ["Dist->StudentTwoSidedP(1.0, 0.0)", "Dist->StudentQuantile(0.0, 5.0)", "Dist->StudentQuantile(1.0, 5.0)",
                 "Dist->NormalQuantile(1.5)", "Dist->BetaInc(1.5, 1.0, 1.0)", "Dist->FSf(-1.0, 2.0, 2.0)",
                 "Dist->ChiSquareCdf(1.0, -1.0)", "Dist->LogGamma(-2.0)", "Dist->StudentCdf(Float->NaN(), 5.0)"]:
        t.add('ok := Report("%s is NaN", %s <> %s) & ok;' % (expr, expr, expr))
    t.write("stats_dist.obs")


GENS = {
    "small": ("Seq(8, 37, 11, 23, 4.0)", "Seq(12, 29, 5, 31, 5.0)"),
    "tiny": ("[5.1, 4.9, 6.3]", "[7.2, 6.8, 8.1]"),
    "big": ("Seq(2000, 37, 11, 101, 10.0)", "Seq(2000, 53, 7, 97, 9.0)"),
    "unequal": ("Seq(30, 31, 3, 17, 3.0)", "Seq(200, 47, 19, 89, 6.0)"),
}


def ttest_data():
    return {
        "small": (seq(8, 37, 11, 23, 4.0), seq(12, 29, 5, 31, 5.0)),
        "tiny": (np.array([5.1, 4.9, 6.3]), np.array([7.2, 6.8, 8.1])),
        "big": (seq(2000, 37, 11, 101, 10.0), seq(2000, 53, 7, 97, 9.0)),
        "unequal": (seq(30, 31, 3, 17, 3.0), seq(200, 47, 19, 89, 6.0)),
    }


def ttest_test():
    t = Test("StatsTTest", "System.Stats t-tests against scipy.stats.ttest_ind (equal and unequal variance), ttest_rel and\nttest_1samp with their confidence intervals, plus Cohen's d and Hedges' g from their definitions.")
    data = ttest_data()
    for key, (a, b) in data.items():
        ea, eb = GENS[key]
        t.add('a := %s;' % ea)
        t.add('b := %s;' % eb)
        n1, n2 = len(a), len(b)
        m1, m2 = a.mean(), b.mean()
        v1, v2 = a.var(ddof=1), b.var(ddof=1)
        sp_ = math.sqrt(((n1 - 1) * v1 + (n2 - 1) * v2) / (n1 + n2 - 2))
        d = (m1 - m2) / sp_
        g = d * (1 - 3 / (4 * (n1 + n2 - 2) - 1))
        for mode, ev, cls in [("student", True, "TwoSample"), ("welch", False, "Welch")]:
            for lvl in (0.95, 0.99):
                r = st.ttest_ind(a, b, equal_var=ev)
                ci = r.confidence_interval(lvl)
                nm = "%s %s level %s" % (key, mode, lvl)
                t.add('r := TTest->%s(a, b, %s);' % (cls, num(lvl)))
                t.add('ok := Report("%s: valid", r->IsValid()) & ok;' % nm)
                t.near(nm + " t", "r->GetT()", r.statistic)
                t.near(nm + " df", "r->GetDf()", r.df)
                t.near(nm + " p", "r->GetP()", r.pvalue)
                t.near(nm + " diff", "r->GetMeanDiff()", m1 - m2)
                t.near(nm + " ci low", "r->GetCiLow()", ci.low)
                t.near(nm + " ci high", "r->GetCiHigh()", ci.high)
                t.near(nm + " cohen d", "r->GetCohenD()", d)
                t.near(nm + " hedges g", "r->GetHedgesG()", g)
        r = st.ttest_ind(a, b, equal_var=True)
        t.add('r := TTest->TwoSample(a, b);')
        t.near("%s student default level ci low" % key, "r->GetCiLow()", r.confidence_interval(0.95).low)
        t.add('ok := Report("%s default level is 0.95 and sizes are kept", r->GetLevel() = 0.95 & r->GetN1() = %d & r->GetN2() = %d) & ok;' % (key, n1, n2))
        for mu in (0.0, round(float(m1), 1) + 0.3):
            r = st.ttest_1samp(a, mu)
            ci = r.confidence_interval(0.95)
            nm = "%s one-sample mu=%s" % (key, mu)
            sd = a.std(ddof=1)
            t.add('r := TTest->OneSample(a, %s);' % num(mu))
            t.near(nm + " t", "r->GetT()", r.statistic)
            t.near(nm + " df", "r->GetDf()", r.df)
            t.near(nm + " p", "r->GetP()", r.pvalue)
            t.near(nm + " diff", "r->GetMeanDiff()", m1 - mu)
            t.near(nm + " ci low", "r->GetCiLow()", ci.low - mu)
            t.near(nm + " ci high", "r->GetCiHigh()", ci.high - mu)
            t.near(nm + " d", "r->GetCohenD()", (m1 - mu) / sd)
            t.near(nm + " g", "r->GetHedgesG()", (m1 - mu) / sd * (1 - 3 / (4 * (n1 - 1) - 1)))
        k = min(n1, n2)
        pa, pb = a[:k], b[:k]
        r = st.ttest_rel(pa, pb)
        ci = r.confidence_interval(0.95)
        dd = pa - pb
        nm = "%s paired" % key
        t.add('pa := Float->New[%d];' % k)
        t.add('pb := Float->New[%d];' % k)
        t.add('each(i : %d) { pa[i] := a[i]; pb[i] := b[i]; };' % k)
        t.add('r := TTest->Paired(pa, pb);')
        t.near(nm + " t", "r->GetT()", r.statistic)
        t.near(nm + " df", "r->GetDf()", r.df)
        t.near(nm + " p", "r->GetP()", r.pvalue)
        t.near(nm + " diff", "r->GetMeanDiff()", dd.mean())
        t.near(nm + " ci low", "r->GetCiLow()", ci.low)
        t.near(nm + " ci high", "r->GetCiHigh()", ci.high)
        t.near(nm + " d_z", "r->GetCohenD()", dd.mean() / dd.std(ddof=1))
        t.near(nm + " mean1", "r->GetMean1()", pa.mean())
    a, b = data["small"]
    t.add('a := Seq(8, 37, 11, 23, 4.0);')
    t.near("mean", "Stats->Mean(a)", a.mean())
    t.near("variance", "Stats->Variance(a)", a.var(ddof=1))
    t.near("population variance", "Stats->PopulationVariance(a)", a.var())
    t.near("std dev", "Stats->StdDev(a)", a.std(ddof=1))
    t.near("std error", "Stats->StdError(a)", st.sem(a))
    t.near("median", "Stats->Median(a)", float(np.median(a)))
    t.add('b := Seq(12, 29, 5, 31, 5.0);')
    t.add('cb := Float->New[8];')
    t.add('each(i : 8) { cb[i] := b[i]; };')
    t.near("covariance", "Stats->Covariance(a, cb)", float(np.cov(a, b[:8])[0, 1]))
    t.near("sum", "Stats->Sum(a)", a.sum())
    t.write("stats_ttest.obs")


def corr_ols_test():
    t = Test("StatsCorrOls", "System.Stats Pearson correlation (p value and Fisher interval from scipy.stats.pearsonr) and the OLS\nsummary (coefficients, standard errors, t, p, intervals, R2, adjusted R2, F, AIC, BIC) against numpy and scipy.")
    n = 40
    x1 = seq(n, 37, 11, 23, 2.0)
    x2 = seq(n, 17, 3, 19, 3.0)
    grp = ["A" if (i * 7 + 2) % 3 == 0 else ("B" if (i * 5) % 3 == 1 else "C") for i in range(n)]
    noise = np.array([((i * 53 + 7) % 29 - 14) / 7.0 for i in range(n)])
    dB = np.array([1.0 if g == "B" else 0.0 for g in grp])
    dC = np.array([1.0 if g == "C" else 0.0 for g in grp])
    y = 2.0 + 0.5 * x1 - 0.3 * x2 + 1.5 * dB - 0.8 * dC + noise
    t.add('n := %d;' % n)
    t.add('x1 := Seq(n, 37, 11, 23, 2.0);')
    t.add('x2 := Seq(n, 17, 3, 19, 3.0);')
    t.add('noise := Float->New[n];')
    t.add('grp := String->New[n];')
    t.add('each(i : n) { noise[i] := ((i * 53 + 7) % 29 - 14)->As(Float) / 7.0; if((i * 7 + 2) % 3 = 0) { grp[i] := "A"; } else if((i * 5) % 3 = 1) { grp[i] := "B"; } else { grp[i] := "C"; }; };')
    t.add('dummies := Design->Dummies(grp);')
    t.add('lv := Design->Levels(grp);')
    t.add('ok := Report("dummies: two columns for three levels, sorted, first is the reference", dummies <> Nil & dummies->Size()[1] = 2 & lv->Size() = 3 & lv[0]->Equals("A") & lv[2]->Equals("C")) & ok;')
    t.add('Xc := Design->HStack(Design->HStack(Design->Column(x1), Design->Column(x2)), dummies);')
    t.add('y := Float->New[n];')
    t.add('each(i : n) { y[i] := 2.0 + 0.5 * x1[i] - 0.3 * x2[i] + noise[i]; if(grp[i]->Equals("B")) { y[i] += 1.5; } else if(grp[i]->Equals("C")) { y[i] -= 0.8; }; };')
    r, p = st.pearsonr(x1, y)
    ci = st.pearsonr(x1, y).confidence_interval(0.95)
    t.add('c := Correlation->Pearson(x1, y);')
    t.near("pearson r", "c->GetR()", r)
    t.near("pearson p", "c->GetP()", p)
    t.near("pearson t", "c->GetT()", r * math.sqrt((n - 2) / (1 - r * r)))
    t.near("pearson ci low", "c->GetCiLow()", ci.low)
    t.near("pearson ci high", "c->GetCiHigh()", ci.high)
    t.add('ok := Report("pearson n and df", c->GetN() = %d & c->GetDf() = %s) & ok;' % (n, num(n - 2)))
    r2_, p2_ = st.pearsonr(x2, y)
    ci2 = st.pearsonr(x2, y).confidence_interval(0.99)
    t.add('c := Correlation->Pearson(x2, y, 0.99);')
    t.near("pearson 2 r", "c->GetR()", r2_)
    t.near("pearson 2 p", "c->GetP()", p2_)
    t.near("pearson 2 ci low 99", "c->GetCiLow()", ci2.low)
    t.near("pearson 2 ci high 99", "c->GetCiHigh()", ci2.high)
    xs = np.arange(30.0)
    ys = xs * 2 + np.array([((i * 13) % 7) - 3 for i in range(30)])
    rr = st.pearsonr(xs, ys)
    t.add('xs := Float->New[30];')
    t.add('ys := Float->New[30];')
    t.add('each(i : 30) { xs[i] := i->As(Float); ys[i] := 2.0 * i->As(Float) + (((i * 13) % 7) - 3)->As(Float); };')
    t.add('c := Correlation->Pearson(xs, ys);')
    t.near("strong pearson r", "c->GetR()", rr.statistic)
    t.near("strong pearson p", "c->GetP()", rr.pvalue)
    t.near("strong pearson ci low", "c->GetCiLow()", rr.confidence_interval(0.95).low)

    def ols(X, yv, const, label, expr_fit, names):
        k = X.shape[1]
        nn = len(yv)
        beta, *_ = np.linalg.lstsq(X, yv, rcond=None)
        res = yv - X @ beta
        ssr = float(res @ res)
        dfr = nn - k
        s2 = ssr / dfr
        cov = s2 * np.linalg.inv(X.T @ X)
        se = np.sqrt(np.diag(cov))
        tt = beta / se
        pv = 2 * st.t.sf(abs(tt), dfr)
        crit = st.t.ppf(0.975, dfr)
        tss = float(((yv - yv.mean()) ** 2).sum()) if const else float((yv ** 2).sum())
        r2 = 1 - ssr / tss
        dfm = k - (1 if const else 0)
        adj = 1 - (nn - (1 if const else 0)) / dfr * (1 - r2)
        F = ((tss - ssr) / dfm) / (ssr / dfr)
        fp = st.f.sf(F, dfm, dfr)
        llf = -nn / 2 * math.log(2 * math.pi) - nn / 2 * math.log(ssr / nn) - nn / 2
        aic = -2 * llf + 2 * (dfm + 1)
        bic = -2 * llf + (dfm + 1) * math.log(nn)
        t.add('f := %s;' % expr_fit)
        t.add('ok := Report("%s: valid", f->IsValid()) & ok;' % label)
        t.add('ci := f->GetConfInt(0.95);')
        t.add('co := f->GetCoefficients();')
        t.add('se := f->GetStdErrors();')
        t.add('ts := f->GetTStats();')
        t.add('ps := f->GetPValues();')
        t.add('nm := f->GetNames();')
        for j in range(k):
            t.near("%s coef %s" % (label, names[j]), "co[%d]" % j, beta[j])
            t.near("%s se %s" % (label, names[j]), "se[%d]" % j, se[j])
            t.near("%s t %s" % (label, names[j]), "ts[%d]" % j, tt[j])
            t.near("%s p %s" % (label, names[j]), "ps[%d]" % j, pv[j], "1.0e-8")
            t.near("%s ci low %s" % (label, names[j]), "ci[%d, 0]" % j, beta[j] - crit * se[j])
            t.near("%s ci high %s" % (label, names[j]), "ci[%d, 1]" % j, beta[j] + crit * se[j])
            t.add('ok := Report("%s name %d is %s", nm[%d]->Equals("%s")) & ok;' % (label, j, names[j], j, names[j]))
        t.near(label + " R2", "f->GetRSquared()", r2)
        t.near(label + " adj R2", "f->GetAdjustedRSquared()", adj)
        t.near(label + " resid std error", "f->GetResidualStdError()", math.sqrt(s2))
        t.near(label + " F", "f->GetFStat()", F)
        t.near(label + " F p", "f->GetFPValue()", fp, "1.0e-8")
        t.near(label + " AIC", "f->GetAIC()", aic)
        t.near(label + " BIC", "f->GetBIC()", bic)
        t.near(label + " log-likelihood", "f->GetLogLikelihood()", llf)
        t.near(label + " SSR", "f->GetSSR()", ssr)
        t.add('ok := Report("%s: dims", f->GetNObs() = %d & f->GetNParams() = %d & f->GetDfResid() = %s & f->GetDfModel() = %s & f->HasIntercept() = %s) & ok;' % (label, nn, k, num(dfr), num(dfm), "true" if const else "false"))
        t.add('cv := f->GetCovariance();')
        t.near(label + " cov[0,0]", "cv[0, 0]", cov[0, 0])
        t.near(label + " cov[k-1,k-1]", "cv[%d, %d]" % (k - 1, k - 1), cov[k - 1, k - 1])
        return beta

    X = np.column_stack([np.ones(n), x1, x2, dB, dC])
    beta = ols(X, y, True, "ols intercept+dummies", 'OLS->Fit(Xc, y, true, ["x1", "x2", "B", "C"])', ["const", "x1", "x2", "B", "C"])
    t.add('pr := f->Predict([[3.0, 4.0, 1.0, 0.0], [0.0, 0.0, 0.0, 1.0]]);')
    t.near("predict row 1", "pr[0]", float(beta @ [1, 3.0, 4.0, 1.0, 0.0]))
    t.near("predict row 2", "pr[1]", float(beta @ [1, 0, 0, 0, 1.0]))
    t.add('ok := Report("predict with the wrong column count is Nil", f->Predict([[1.0, 2.0]]) = Nil) & ok;')
    t.add('ok := Report("residuals + fitted = y", Float->Abs(f->GetResiduals()[3] + f->GetFitted()[3] - y[3]) < 1.0e-12) & ok;')
    t.add('Xi := Design->AddIntercept(Xc);')
    t.add('g := OLS->Fit(Xi, y, false);')
    t.add('ok := Report("AddIntercept + Fit(..., false) agrees on the coefficients", g->IsValid() & Float->Abs(g->GetCoefficients()[1] - f->GetCoefficients()[1]) < 1.0e-12) & ok;')
    X2 = np.column_stack([x1, x2])
    ols(X2, y, False, "ols no intercept", 'OLS->Fit(Design->HStack(Design->Column(x1), Design->Column(x2)), y, false)', ["x1", "x2"])
    xs_ = np.array([1.0, 2.0, 3.0, 4.0, 5.0, 6.0])
    ys_ = np.array([2.1, 3.9, 6.2, 7.8, 10.1, 11.7])
    ols(np.column_stack([np.ones(6), xs_]), ys_, True, "ols simple", 'OLS->Fit([[1.0], [2.0], [3.0], [4.0], [5.0], [6.0]], [2.1, 3.9, 6.2, 7.8, 10.1, 11.7], true)', ["const", "x1"])
    t.add('s := f->ToString();')
    t.add('ok := Report("summary mentions R-squared, F-statistic, const and x1", s->Has("R-squared") & s->Has("F-statistic") & s->Has("const") & s->Has("x1")) & ok;')
    t.write("stats_corr_ols.obs")


if __name__ == "__main__":
    dist_test()
    ttest_test()
    corr_ols_test()
    print("wrote stats_dist.obs, stats_ttest.obs, stats_corr_ols.obs")
