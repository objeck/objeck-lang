#!/usr/bin/env python3
"""
Reference numbers for the System.ML.TimeSeries regression tests (ml_timeseries_*.obs).

Writes expected.txt next to this file: one line per value, "name|key|v1,v2,...". Nothing here calls the
Objeck library. The numbers come from three independent sources:
  - statsmodels / scipy / numpy (versions are written in the header of expected.txt): autocorrelation,
    partial autocorrelation, Ljung-Box, the ADF statistic and critical values, seasonal_decompose, AutoReg,
    ExponentialSmoothing with fixed parameters and known initial states, ARIMA (state-space MLE, only as a
    loose cross-check), Box-Cox, special functions;
  - hand-written numpy code in this file for what those packages do not provide: the baseline forecasters
    and their intervals (Hyndman and Athanasopoulos, FPP3 section 5.5), the exponential smoothing
    recursions and their initial-state rule, the conditional-least-squares ARMA estimates (scipy
    least_squares on residuals written here), the psi-weight forecast variance, the metrics;
  - the same deterministic generator as the library (xorshift64 + Box-Muller), so the test series are
    reproduced exactly: the Objeck tests regenerate each series and compare its first values to the ones
    written here before using it.

Reproduce:
    python3 -m venv /tmp/tsvenv && /tmp/tsvenv/bin/pip install numpy scipy statsmodels pandas
    /tmp/tsvenv/bin/python gen_reference.py
"""
import math
import os
import sys
import warnings

import numpy as np
import pandas as pd
import scipy
import scipy.stats
import scipy.special
import scipy.optimize
import statsmodels
from statsmodels.tsa.stattools import acf, pacf, adfuller, acovf
from statsmodels.tsa.adfvalues import mackinnoncrit
from statsmodels.tsa.seasonal import seasonal_decompose
from statsmodels.tsa.ar_model import AutoReg
from statsmodels.tsa.holtwinters import ExponentialSmoothing
from statsmodels.tsa.arima.model import ARIMA
from statsmodels.stats.diagnostic import acorr_ljungbox

warnings.filterwarnings("ignore")

M64 = (1 << 64) - 1
OUT = []


def emit(name, key, vals):
    if isinstance(vals, (int, float, np.floating, np.integer)):
        vals = [vals]
    OUT.append("%s|%s|%s" % (name, key, ",".join(repr(float(v)) if v == v else "nan" for v in vals)))


# ---------------------------------------------------------------------------------------------- generator
class Rng:
    def __init__(self, seed):
        self.state = (seed * 2654435761 + 88172645463325252) & M64
        self.spare = 0.0
        self.has_spare = False
        for _ in range(20):
            self.next_bits()

    def next_bits(self):
        x = self.state
        x ^= (x << 13) & M64
        x ^= x >> 7
        x ^= (x << 17) & M64
        self.state = x
        return x

    def next_float(self):
        return ((self.next_bits() >> 11) & ((1 << 53) - 1)) / 9007199254740992.0

    def gauss(self):
        if self.has_spare:
            self.has_spare = False
            return self.spare
        u = self.next_float()
        while u <= 1e-300:
            u = self.next_float()
        v = self.next_float()
        r = math.sqrt(-2.0 * math.log(u))
        ang = 2.0 * math.pi * v
        self.spare = r * math.sin(ang)
        self.has_spare = True
        return r * math.cos(ang)


def sim_arma(phi, theta, c, sigma, n, seed):
    p, q = len(phi), len(theta)
    burn = 200
    total = n + burn
    rng = Rng(seed)
    x = [0.0] * total
    e = [0.0] * total
    for t in range(total):
        e[t] = sigma * rng.gauss()
        v = c + e[t]
        for i in range(1, p + 1):
            if t - i >= 0:
                v += phi[i - 1] * x[t - i]
        for j in range(1, q + 1):
            if t - j >= 0:
                v += theta[j - 1] * e[t - j]
        x[t] = v
    return np.array(x[burn:])


def trend_seasonal(n, level, slope, amp, period, sigma, seed):
    rng = Rng(seed)
    return np.array([level + slope * t + amp * math.sin(2.0 * math.pi * t / period) + sigma * rng.gauss() for t in range(n)])


def mult_seasonal(n, level, slope, amp, period, sigma, seed):
    rng = Rng(seed)
    return np.array([(level + slope * t) * (1.0 + amp * math.sin(2.0 * math.pi * t / period)) * (1.0 + sigma * rng.gauss()) for t in range(n)])


def random_walk(n, drift, sigma, seed):
    rng = Rng(seed)
    cur = 0.0
    out = []
    for t in range(n):
        cur += drift + sigma * rng.gauss()
        out.append(cur)
    return np.array(out)


S = {}
S["ar1"] = sim_arma([0.7], [], 0.0, 1.0, 400, 11)
S["ma1"] = sim_arma([], [0.6], 0.0, 1.0, 500, 12)
S["arma11"] = sim_arma([0.5], [0.4], 2.0, 1.0, 600, 13)
S["ar2"] = sim_arma([0.5, -0.3], [], 0.0, 1.0, 500, 14)
S["ts"] = trend_seasonal(120, 20.0, 0.3, 5.0, 12, 0.5, 15)
S["ms"] = mult_seasonal(120, 40.0, 0.2, 0.25, 12, 0.02, 16)
S["rw"] = random_walk(200, 0.3, 1.0, 17)

for k, v in S.items():
    emit(k, "head", v[:6])
    emit(k, "n", len(v))

r = Rng(42)
emit("rng", "uniform", [r.next_float() for _ in range(5)])
emit("rng", "gauss", [r.gauss() for _ in range(4)])

# ---------------------------------------------------------------------------------------------- special functions
emit("special", "lgamma", [math.lgamma(x) for x in (0.5, 1.5, 2.0, 5.5, 10.0, 0.1, 30.25)])
emit("special", "lgamma_x", (0.5, 1.5, 2.0, 5.5, 10.0, 0.1, 30.25))
ps = (0.5, 0.8, 0.9, 0.95, 0.975, 0.995, 0.999, 0.001, 0.2)
emit("special", "nq_p", ps)
emit("special", "nq", [scipy.stats.norm.ppf(p) for p in ps])
xs = (-3.0, -1.0, -0.2, 0.0, 0.7, 1.96, 4.0)
emit("special", "ncdf_x", xs)
emit("special", "ncdf", [scipy.stats.norm.cdf(x) for x in xs])
cs = ((3.0, 4.0), (0.5, 1.0), (10.0, 5.0), (25.0, 12.0), (1.0, 10.0), (60.0, 20.0))
emit("special", "chi_x", [c[0] for c in cs])
emit("special", "chi_k", [c[1] for c in cs])
emit("special", "chi", [scipy.stats.chi2.cdf(c[0], c[1]) for c in cs])

# ---------------------------------------------------------------------------------------------- acf / pacf / ljung-box
x = S["ar1"]
emit("acf", "acf", acf(x, nlags=10, fft=False))
emit("acf", "pacf", pacf(x, nlags=10, method="ywm"))
emit("acf", "acov", acovf(x, adjusted=False, fft=False)[:6])
lb = acorr_ljungbox(x, lags=[10], return_df=True)
emit("acf", "lb", [lb["lb_stat"].iloc[0], lb["lb_pvalue"].iloc[0]])
lb2 = acorr_ljungbox(S["ma1"], lags=[8], return_df=True, model_df=1)
emit("acf", "lb_ma1_df1", [lb2["lb_stat"].iloc[0], lb2["lb_pvalue"].iloc[0]])
emit("acf", "ma1_acf", acf(S["ma1"], nlags=4, fft=False))

# ---------------------------------------------------------------------------------------------- ADF
for name, reg, lags in (("rw", "c", 4), ("ar1", "c", 2), ("ar1", "ct", 3), ("ar1", "n", 1), ("rw", "ct", 0), ("ts", "c", 12)):
    res = adfuller(S[name], maxlag=lags, regression=reg, autolag=None)
    key = "%s_%s_%d" % (name, reg, lags)
    emit("adf", key + "_stat", res[0])
    emit("adf", key + "_nobs", res[3])
    emit("adf", key + "_crit", [res[4]["1%"], res[4]["5%"], res[4]["10%"]])

# automatic lag choice: statsmodels adfuller(autolag="AIC")
for name, reg in (("rw", "c"), ("ar1", "c"), ("ar1", "ct"), ("ts", "c"), ("ar1", "n"), ("ma1", "c"), ("ar2", "ct"), ("arma11", "c")):
    res = adfuller(S[name], regression=reg, autolag="AIC")
    key = "%s_%s_auto" % (name, reg)
    emit("adf", key + "_stat", res[0])
    emit("adf", key + "_lags", res[2])
    emit("adf", key + "_nobs", res[3])
    emit("adf", key + "_crit", [res[4]["1%"], res[4]["5%"], res[4]["10%"]])

# ---------------------------------------------------------------------------------------------- decomposition
for name, model in (("ts", "additive"), ("ms", "multiplicative")):
    d = seasonal_decompose(S[name], model=model, period=12)
    emit("dec_" + name, "trend", d.trend)
    emit("dec_" + name, "seasonal", d.seasonal[:12])
    emit("dec_" + name, "resid", d.resid)

# ---------------------------------------------------------------------------------------------- box-cox, rolling
x = S["ms"]
emit("boxcox", "lambda_mle", scipy.stats.boxcox_normmax(x, method="mle"))
emit("boxcox", "t05", scipy.stats.boxcox(x[:10], 0.5))
emit("boxcox", "tm1", scipy.stats.boxcox(x[:10], -1.0))
emit("boxcox", "t0", scipy.stats.boxcox(x[:10], 0.0))
ser = pd.Series(S["ar1"][:40])
emit("rolling", "mean5", ser.rolling(5).mean())
emit("rolling", "std5", ser.rolling(5).std())

# ---------------------------------------------------------------------------------------------- baselines (FPP3 5.5)
def baseline_ref(y, kind, param, h):
    n = len(y)
    if kind == "naive":
        fit = np.r_[np.nan, y[:-1]]
        K = 0
    elif kind == "snaive":
        fit = np.r_[[np.nan] * param, y[:-param]]
        K = 0
    elif kind == "drift":
        slope = (y[-1] - y[0]) / (n - 1)
        fit = np.r_[np.nan, y[:-1] + slope]
        K = 1
    elif kind == "mean":
        fit = np.full(n, y.mean())
        K = 1
    else:  # moving average of the previous `param` values
        fit = np.array([np.nan] * param + [y[t - param:t].mean() for t in range(param, n)])
        K = 0
    e = y - fit
    e = e[~np.isnan(e)]
    sigma = math.sqrt((e ** 2).sum() / (len(e) - K))
    steps = np.arange(1, h + 1)
    if kind == "naive":
        mean = np.full(h, y[-1])
        mult = np.sqrt(steps)
    elif kind == "snaive":
        mean = np.array([y[n - param + (s - 1) % param] for s in steps])
        mult = np.sqrt((steps - 1) // param + 1)
    elif kind == "drift":
        slope = (y[-1] - y[0]) / (n - 1)
        mean = y[-1] + steps * slope
        mult = np.sqrt(steps * (1 + steps / n))
    elif kind == "mean":
        mean = np.full(h, y.mean())
        mult = np.full(h, math.sqrt(1 + 1 / n))
    else:
        mean = np.full(h, y[-param:].mean())
        mult = np.full(h, math.sqrt(1 + 1 / param))
    return mean, sigma * mult, sigma


y = S["ts"]
for kind, param in (("naive", 1), ("snaive", 12), ("drift", 1), ("mean", 1), ("ma", 4)):
    mean, sd, sigma = baseline_ref(y, kind, param, 15)
    emit("base_" + kind, "mean", mean)
    emit("base_" + kind, "sd", sd)
    emit("base_" + kind, "sigma", sigma)
    emit("base_" + kind, "lower95", mean - scipy.stats.norm.ppf(0.975) * sd)

# ---------------------------------------------------------------------------------------------- exponential smoothing
def es_init(y, trend, season, m):
    n = len(y)
    if season != "none":
        m1 = y[:m].mean()
        m2 = y[m:2 * m].mean()
        tr = (m2 - m1) / m
        s0 = np.zeros(m)
        for i in range(m):
            lv1 = m1 + tr * (i - (m - 1) / 2.0)
            lv2 = lv1 + tr * m
            if season == "mul":
                s0[i] = 0.5 * (y[i] / lv1 + y[m + i] / lv2)
            else:
                s0[i] = 0.5 * ((y[i] - lv1) + (y[m + i] - lv2))
        avg = s0.mean()
        s0 = s0 / avg if season == "mul" else s0 - avg
        if trend:
            return m1 - tr * ((m - 1) / 2.0 + 1.0), tr, s0
        return m1, 0.0, s0
    if trend:
        k = min(n, 10)
        xs = np.arange(k, dtype=float)
        slope, icpt = np.polyfit(xs, y[:k], 1)
        return icpt - slope, slope, None
    return y[0], 0.0, None


def es_run(y, init, trend, damped, season, m, a, b, g, phi):
    l, bb, s0 = init
    s = None if s0 is None else s0.copy()
    n = len(y)
    fit = np.zeros(n)
    for t in range(n):
        base = l + phi * bb
        seas = 0.0
        f = base
        if season != "none":
            seas = s[t % m]
            f = base * seas if season == "mul" else base + seas
        fit[t] = f
        yt = y[t]
        if season == "mul":
            lnew = a * (yt / seas) + (1 - a) * base
            s[t % m] = g * (yt / base) + (1 - g) * seas
        elif season == "add":
            lnew = a * (yt - seas) + (1 - a) * base
            s[t % m] = g * (yt - base) + (1 - g) * seas
        else:
            lnew = a * yt + (1 - a) * base
        if trend:
            bb = b * (lnew - l) + (1 - b) * phi * bb
        l = lnew
    return fit, l, bb, s


def es_forecast(l, bb, s, n, trend, damped, season, m, phi, h):
    out = []
    ps = 0.0
    pp = 1.0
    for step in range(1, h + 1):
        pp *= phi
        ps += pp
        f = l + ps * bb
        if season == "add":
            f += s[(n + step - 1) % m]
        elif season == "mul":
            f *= s[(n + step - 1) % m]
        out.append(f)
    return np.array(out)


def es_sd(sigma, a, b, g, phi, trend, season, m, h, mult, fc):
    sds = []
    vs = 0.0
    for i in range(h):
        if i > 0:
            cj = a
            if trend:
                ps, pp = 0.0, 1.0
                for _ in range(i):
                    pp *= phi
                    ps += pp
                cj = a * (1 + b * ps)
            if season != "none" and i % m == 0:
                cj += g
            vs += cj * cj
        v = sigma * math.sqrt(1 + vs)
        if mult:
            v *= abs(fc[i])
        sds.append(v)
    return np.array(sds)


CONFIGS = [
    ("ses", False, False, "none", 1, "ts"),
    ("holt", True, False, "none", 1, "ts"),
    ("holtd", True, True, "none", 1, "ts"),
    ("hwa", True, False, "add", 12, "ts"),
    ("hwm", True, False, "mul", 12, "ms"),
    ("hwa_notrend", False, False, "add", 12, "ts"),
    ("hwad", True, True, "add", 12, "ts"),
]
FIXED = {"ses": (0.4, 0, 0, 1.0), "holt": (0.5, 0.2, 0, 1.0), "holtd": (0.5, 0.2, 0, 0.9),
         "hwa": (0.3, 0.1, 0.2, 1.0), "hwm": (0.3, 0.1, 0.2, 1.0), "hwa_notrend": (0.3, 0, 0.2, 1.0), "hwad": (0.3, 0.1, 0.2, 0.9)}

for name, trend, damped, season, m, sname in CONFIGS:
    y = S[sname]
    n = len(y)
    init = es_init(y, trend, season, m)
    emit("es_" + name, "init_level", init[0])
    emit("es_" + name, "init_trend", init[1])
    if init[2] is not None:
        emit("es_" + name, "init_seasonal", init[2])

    # fixed parameters: own recursion, then statsmodels with the same initial states
    a, b, g, phi = FIXED[name]
    fit, l, bb, s = es_run(y, init, trend, damped, season, m, a, b, g, phi)
    sse = float(((y - fit) ** 2).sum())
    fc = es_forecast(l, bb, s, n, trend, damped, season, m, phi, 14)
    emit("es_" + name, "fixed_params", (a, b, g, phi))
    emit("es_" + name, "fixed_sse", sse)
    emit("es_" + name, "fixed_fc", fc)
    emit("es_" + name, "fixed_fitted_head", fit[:8])

    kw = dict(trend="add" if trend else None, damped_trend=damped, seasonal=None if season == "none" else ("add" if season == "add" else "mul"),
              seasonal_periods=m if season != "none" else None, initialization_method="known", initial_level=init[0])
    if trend:
        kw["initial_trend"] = init[1]
    if season != "none":
        kw["initial_seasonal"] = init[2]
    fkw = dict(smoothing_level=a, optimized=False)
    if trend:
        fkw["smoothing_trend"] = b
    if season != "none":
        fkw["smoothing_seasonal"] = g
    if damped:
        fkw["damping_trend"] = phi
    try:
        sm = ExponentialSmoothing(y, **kw).fit(**fkw)
        emit("es_" + name, "sm_fixed_sse", float(sm.sse))
        emit("es_" + name, "sm_fixed_fc", sm.forecast(14))
    except Exception as ex:  # statsmodels' definition of the initial level differs for some cases
        print("statsmodels fixed fit failed for", name, ex, file=sys.stderr)

    # optimized: multi-start bounded minimization of the same SSE
    free = ["a"] + (["b"] if trend else []) + (["g"] if season != "none" else []) + (["phi"] if damped else [])

    def unpack(x):
        d = dict(zip(free, x))
        return d["a"], d.get("b", 0.0), d.get("g", 0.0), d.get("phi", 1.0)

    def obj(x):
        a_, b_, g_, p_ = unpack(x)
        f_, _, _, _ = es_run(y, init, trend, damped, season, m, a_, b_, g_, p_)
        v = float(((y - f_) ** 2).sum())
        return v if v == v else 1e300

    bounds = [(0.0001, 0.9999)] * (len(free) - (1 if damped else 0)) + ([(0.8, 0.98)] if damped else [])
    best = None
    rs = np.random.RandomState(5)
    starts = [[0.3, 0.1, 0.1, 0.95][:len(free)]] + [[rs.uniform(lo, hi) for lo, hi in bounds] for _ in range(24)]
    for st in starts:
        st = list(st)
        if damped:
            st[-1] = min(max(st[-1], 0.8), 0.98)
        res = scipy.optimize.minimize(obj, st, method="L-BFGS-B", bounds=bounds, options={"ftol": 1e-15, "gtol": 1e-10, "maxiter": 2000})
        if best is None or res.fun < best.fun:
            best = res
    a_, b_, g_, p_ = unpack(best.x)
    fit, l, bb, s = es_run(y, init, trend, damped, season, m, a_, b_, g_, p_)
    sse = float(((y - fit) ** 2).sum())
    fc = es_forecast(l, bb, s, n, trend, damped, season, m, p_, 14)
    emit("es_" + name, "opt_sse", sse)
    emit("es_" + name, "opt_params", (a_, b_, g_, p_))
    emit("es_" + name, "opt_fc", fc)
    mult = season == "mul"
    if mult:
        rel = (y - fit) / fit
        sigma = math.sqrt((rel ** 2).sum() / n)
    else:
        sigma = math.sqrt(sse / n)
    emit("es_" + name, "opt_sd", es_sd(sigma, a_, b_, g_, p_, trend, season, m, 14, mult, fc))
    k = len(free) + 2 + (1 if trend else 0) + (m - 1 if season != "none" else 0)
    ll = -0.5 * n * (math.log(2 * math.pi * sse / n) + 1)
    emit("es_" + name, "opt_k", k)
    emit("es_" + name, "opt_aic", -2 * ll + 2 * k)
    emit("es_" + name, "opt_aicc", -2 * ll + 2 * k + 2 * k * (k + 1) / (n - k - 1))
    emit("es_" + name, "opt_bic", -2 * ll + k * math.log(n))

    # statsmodels' own optimizer on the same initial states: our SSE must not be worse than its
    try:
        okw = dict(kw)
        sm2 = ExponentialSmoothing(y, **okw).fit(optimized=True)
        emit("es_" + name, "sm_opt_sse", float(sm2.sse))
    except Exception as ex:
        print("statsmodels optimized fit failed for", name, ex, file=sys.stderr)

# ---------------------------------------------------------------------------------------------- ARIMA
def difference(y, lag, order):
    for _ in range(order):
        y = y[lag:] - y[:-lag]
    return y


def css_resid(params, w, p, q, use_mean, start):
    phi = params[:p]
    theta = params[p:p + q]
    mu = params[p + q] if use_mean else 0.0
    n = len(w)
    e = np.zeros(n)
    for t in range(start, n):
        v = w[t] - mu
        for i in range(p):
            v -= phi[i] * (w[t - 1 - i] - mu)
        for j in range(q):
            if t - 1 - j >= 0:
                v -= theta[j] * e[t - 1 - j]
        e[t] = v
    return e[start:], e


def css_fit(w, p, q, use_mean, start=None):
    start = max(p, start or 0)
    x0 = np.zeros(p + q + (1 if use_mean else 0))
    if use_mean:
        x0[-1] = w.mean()
    res = scipy.optimize.least_squares(lambda x: css_resid(x, w, p, q, use_mean, start)[0], x0, method="lm", xtol=1e-14, ftol=1e-14, gtol=1e-14)
    e_all = css_resid(res.x, w, p, q, use_mean, start)[1]
    sse = float((e_all[start:] ** 2).sum())
    return res.x, sse, e_all, start


def psi_weights(phi, theta, d, D, m, h):
    poly = np.r_[1.0, -np.array(phi)]
    for _ in range(d):
        poly = np.convolve(poly, [1.0, -1.0])
    for _ in range(D):
        pm = np.zeros(m + 1)
        pm[0] = 1.0
        pm[m] = -1.0
        poly = np.convolve(poly, pm)
    a = -poly[1:]
    psi = np.zeros(h)
    psi[0] = 1.0
    for j in range(1, h):
        v = theta[j - 1] if j <= len(theta) else 0.0
        for i in range(1, min(j, len(a)) + 1):
            v += a[i - 1] * psi[j - i]
        psi[j] = v
    return psi


def arima_ref(name, y, p, d, q, D=0, m=1, use_mean=None, h=12, start=None):
    if use_mean is None:
        use_mean = (d == 0 and D == 0)
    z = difference(y, m, D) if D else y
    w = difference(z, 1, d) if d else z
    x, sse, e, st = css_fit(w, p, q, use_mean, start)
    phi, theta = x[:p], x[p:p + q]
    mu = x[p + q] if use_mean else 0.0
    nw = len(w)
    N = nw - st
    sigma2 = sse / N
    # forecast on w, then integrate
    wext = list(w)
    eext = list(e)
    for t in range(nw, nw + h):
        v = mu
        for i in range(p):
            v += phi[i] * (wext[t - 1 - i] - mu)
        for j in range(q):
            v += theta[j] * eext[t - 1 - j]
        wext.append(v)
        eext.append(0.0)
    wf = np.array(wext[nw:])
    fz = wf.copy()
    if d:
        full = list(z)
        # integrate d times: rebuild from the differences of z
        levels = [z]
        for _ in range(d):
            levels.append(levels[-1][1:] - levels[-1][:-1])
        cur = wf
        for lev in range(d - 1, -1, -1):
            last = levels[lev][-1]
            cur = last + np.cumsum(cur)
        fz = cur
    fc = fz.copy()
    if D:
        cur = fz
        out = []
        hist = list(y)
        for i in range(h):
            v = cur[i] + hist[len(hist) - m]
            hist.append(v)
            out.append(v)
        fc = np.array(out)
    psi = psi_weights(phi, theta, d, D, m, h)
    sd = np.sqrt(sigma2 * np.cumsum(psi ** 2))
    k = p + q + (1 if use_mean else 0) + 1
    ll = -0.5 * N * (math.log(2 * math.pi * sigma2) + 1)
    emit(name, "phi", phi if p else [])
    emit(name, "theta", theta if q else [])
    emit(name, "mu", mu)
    emit(name, "sse", sse)
    emit(name, "nres", N)
    emit(name, "sigma2", sigma2)
    emit(name, "fc", fc)
    emit(name, "sd", sd)
    emit(name, "aic", -2 * ll + 2 * k)
    emit(name, "aicc", -2 * ll + 2 * k + 2 * k * (k + 1) / (N - k - 1))
    emit(name, "bic", -2 * ll + k * math.log(N))
    return phi, theta, mu


arima_ref("arima_ar1", S["ar1"], 1, 0, 0)
arima_ref("arima_ar2", S["ar2"], 2, 0, 0)
arima_ref("arima_ma1", S["ma1"], 0, 0, 1)
arima_ref("arima_arma11", S["arma11"], 1, 0, 1)
arima_ref("arima_rw_110", S["rw"], 1, 1, 0)
arima_ref("arima_rw_011", S["rw"], 0, 1, 1)
arima_ref("arima_rw_010_drift", S["rw"], 0, 1, 0, use_mean=True)
arima_ref("arima_ts_seas", S["ts"], 1, 0, 0, D=1, m=12, use_mean=True)
arima_ref("arima_ar1_start5", S["ar1"], 1, 0, 0, start=5)
arima_ref("arima_ma1_start4", S["ma1"], 0, 0, 1, start=4)

# AutoReg (OLS) is the exact reference for a pure autoregression
for name, key, lags in (("ar1", "ar1", 1), ("ar2", "ar2", 2)):
    res = AutoReg(S[key], lags=lags, trend="c").fit()
    emit("autoreg_" + name, "params", res.params)
    emit("autoreg_" + name, "sigma2", res.sigma2)

# state-space MLE (statsmodels ARIMA), a loose cross-check of CSS on the long series
for name, key, order in (("ar1", "ar1", (1, 0, 0)), ("ma1", "ma1", (0, 0, 1)), ("arma11", "arma11", (1, 0, 1)), ("rw_110", "rw", (1, 1, 0))):
    res = ARIMA(S[key], order=order, trend="c" if order[1] == 0 else "n").fit()
    emit("mle_" + name, "params", res.params)
    emit("mle_" + name, "fc", res.forecast(12))

# model selection: ground truth for the criterion arithmetic is covered by arima_* above
emit("select", "dummy", 0.0)

# ---------------------------------------------------------------------------------------------- metrics
act = np.array([112.0, 118.0, 132.0, 129.0, 121.0, 135.0, 148.0, 148.0])
frc = np.array([110.0, 120.0, 128.0, 133.0, 125.0, 130.0, 150.0, 140.0])
train = np.array([100.0, 104.0, 99.0, 110.0, 108.0, 115.0, 111.0, 120.0, 118.0, 125.0])
emit("metrics", "act", act)
emit("metrics", "frc", frc)
emit("metrics", "train", train)
emit("metrics", "me", (act - frc).mean())
emit("metrics", "mae", np.abs(act - frc).mean())
emit("metrics", "rmse", math.sqrt(((act - frc) ** 2).mean()))
emit("metrics", "mape", 100 * np.mean(np.abs(act - frc) / np.abs(act)))
emit("metrics", "smape", 100 * np.mean(2 * np.abs(act - frc) / (np.abs(act) + np.abs(frc))))
emit("metrics", "mase1", np.abs(act - frc).mean() / np.abs(np.diff(train)).mean())
emit("metrics", "mase3", np.abs(act - frc).mean() / np.abs(train[3:] - train[:-3]).mean())

with open(os.path.join(os.path.dirname(os.path.abspath(__file__)), "expected.txt"), "w") as f:
    f.write("# generated by gen_reference.py: python %s numpy %s scipy %s statsmodels %s pandas %s\n" % (
        sys.version.split()[0], np.__version__, scipy.__version__, statsmodels.__version__, pd.__version__))
    f.write("\n".join(OUT) + "\n")
print("wrote", len(OUT), "lines")
