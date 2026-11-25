## Time series forecasting (System.ML.TimeSeries)

> **Experimental:** API may change.

`System.ML.TimeSeries` forecasts one numeric series: a trend, a seasonal cycle, an autoregression, or all of them, with a prediction interval around every forecast. It is pure Objeck in `ml_timeseries.obl` (`-lib ml_timeseries,gen_collect`, or `-lib @ai`), needs nothing from `System.ML`, and so runs in the web playground. It has the pieces a forecasting workflow uses, in the order it uses them: a container with a missing-value policy, transforms and a classical decomposition, autocorrelation and a stationarity test, the benchmark forecasters, exponential smoothing, ARIMA, automatic selection, and accuracy metrics with rolling-origin cross-validation.

It follows the `System.ML` conventions: a model is fitted with `Fit` and used with `Forecast`, and nothing ends the program on bad input. `Fit` returns `false` and `GetError()` says why (a missing value, too short a series, a zero under a multiplicative model); a helper returns `Nil` or `NaN`. Everything is deterministic: there is no random start, the optimizer is a Nelder-Mead from fixed points, and the simulation helpers take a seed.

```ruby
use System.ML.TimeSeries;

# a series is values, optional timestamps and a seasonal period
y := TimeSeries->New([112.0, 118.0, Float->NaN(), 129.0, 121.0, 135.0], [0, 1, 2, 3, 4, 5]);
y->SetPeriod(Frequency->Monthly());                  # 12; Quarterly 4, Daily 7 (weekly cycle), Hourly 24
y->Check()->PrintLine();                             # "missing value (NaN) at index 2; call Impute first"
filled := y->Impute(TimeSeries->Missing->LINEAR);    # also FORWARD, MEAN, ZERO, DROP, ERROR (returns Nil)
filled->Get(2)->PrintLine();                         # 123.5
```

The models refuse a series with a NaN; `Impute` returns a new series with the gaps handled (`LINEAR` interpolates inside the series and holds the nearest value at the ends, `DROP` removes the observation and its timestamp). `IsRegular`, `GetStep` and `FutureTimestamps(h)` work from the timestamps; the models themselves assume evenly spaced observations and do not read them.

### Look before you fit

```ruby
use System.ML.TimeSeries;

values := Simulate->TrendSeasonal(72, 100.0, 1.5, 12.0, 12, 2.0, 2026);  # n, level, slope, amplitude, period, noise sd, seed

dec := SeriesTransform->Decompose(values, 12, false);   # true: multiplicative
"seasonal strength {$dec->SeasonalStrength()}"->PrintLine();             # 0.960069
trend := dec->GetTrend();                               # a centered moving average; NaN at both ends
figure := dec->GetFigure();                             # one cycle of the seasonal component

adf := Stationarity->Adf(values, -1, "c");              # -1: choose the lag count by AIC; "n", "c" or "ct"
"ADF {$adf->GetStatistic()}, 5% value {$adf->GetCritical(0.05)}, stationary {$adf->IsStationary(0.05)}"->PrintLine();
"d = {$Stationarity->SuggestDiff(values, 2)}, D = {$Stationarity->SuggestSeasonalDiff(values, 12)}"->PrintLine();   # 1, 1

acf := Acf->Compute(values, 12);                        # lags 0..12, r[0] = 1
pacf := Acf->Partial(values, 12);                       # Durbin-Levinson
lb := Acf->LjungBox(values, 12, 0);                     # [Q, p-value]; pass the fitted parameter count for residuals
```

- **Transforms** (`SeriesTransform`): `Diff(x, lag, order)` and its inverse `Undiff(d, original, lag, order)`, `Integrate` (continue a series from forecast differences), `Log`, `BoxCox(x, lambda)` / `InvBoxCox` and `BoxCoxLambda(x)` (maximum likelihood on a 0.01 grid over [-1, 2]), `RollingMean` / `RollingStd` (trailing, NaN first), `LagMatrix` / `LagTarget` (the autoregression design), `DropNaN`.
- **Decomposition** is the classical one (R's `decompose`, statsmodels' `seasonal_decompose`): a centered moving-average trend, the figure as the mean detrended value at each position, centered to sum 0 (additive) or average 1 (multiplicative). It needs two full periods, and every value above 0 for the multiplicative form.
- **ADF test.** The statistic is the t ratio of the lagged level in the regression of the difference on it, the deterministic terms and `k` lagged differences. Its null is a unit root, so a value BELOW the critical value rejects it. The critical values are MacKinnon's response surface, the ones statsmodels uses: for an infinite sample 1%, 5%, 10% are -3.430, -2.862, -2.567 (`"c"`), -3.959, -3.410, -3.127 (`"ct"`), -2.566, -1.941, -1.617 (`"n"`). There is no p-value. With `lags = -1` the lag count is statsmodels' `autolag="AIC"`.

### Benchmarks

Every other model should beat these, and MASE is scaled by the naive one.

```ruby
use System.ML.TimeSeries;

y := TimeSeries->New(Simulate->TrendSeasonal(120, 20.0, 0.3, 5.0, 12, 0.5, 15));
naive := BaselineForecaster->New(BaselineForecaster->Kind->SEASONAL_NAIVE, 12);   # NAIVE, DRIFT, MEAN, MOVING_AVERAGE (window k)
if(<>naive->Fit(y)) {
  naive->GetError()->PrintLine();
  Runtime->Exit(1);
};
f := naive->Forecast(12, 0.95);      # 12 steps, 95% interval
mean := f->GetMean();
lower := f->GetLower();
upper := f->GetUpper();
"{$mean[0]} in [{$lower[0]}, {$upper[0]}]"->PrintLine();
```

The interval widths are the textbook ones (Hyndman and Athanasopoulos, FPP3 section 5.5): the residual standard deviation times `sqrt(h)` for naive, `sqrt(floor((h-1)/m) + 1)` for seasonal naive, `sqrt(h (1 + h/T))` for drift and `sqrt(1 + 1/T)` for the mean.

### Exponential smoothing

`ExponentialSmoothing` is simple smoothing (`Simple()`), Holt's linear trend with optional damping (`Holt(damped)`) and Holt-Winters with an additive or multiplicative season (`HoltWinters(period, multiplicative)`; `New(trend, damped, season, period)` also allows a season without a trend). The recursions are those of statsmodels' `ExponentialSmoothing` and the R forecast package; the smoothing parameters (alpha, beta, gamma in (0, 1), damping phi in (0.8, 0.98)) minimize the sum of squared one-step errors with a Nelder-Mead on a logistic transform, from three fixed starting points and then polished from the best. The initial level, trend and seasonal values are NOT optimized: a documented rule sets them from the first one or two cycles (see the class comment), which keeps the fit fast and deterministic.

```ruby
use System.ML.TimeSeries;

y := TimeSeries->New(Simulate->TrendSeasonal(120, 20.0, 0.3, 5.0, 12, 0.5, 15));
hw := ExponentialSmoothing->HoltWinters(12, false);
hw->Fit(y)->PrintLine();
"alpha {$hw->GetAlpha()} beta {$hw->GetBeta()} gamma {$hw->GetGamma()}, AICc {$hw->GetAicc()}"->PrintLine();
f := hw->Forecast(6, 0.9);
each(i : 6) {
  m := f->GetMean();
  lo := f->GetLower();
  hi := f->GetUpper();
  "+{$i + 1}: {$m[i]}  [{$lo[i]}, {$hi[i]}]"->PrintLine();
};

# fixed parameters skip the optimizer (how the recursions are checked against other software)
fixed := ExponentialSmoothing->Holt(true)->SetParameters(0.5, 0.2, 0.0, 0.9);   # alpha, beta, gamma, phi
fixed->Fit(y)->PrintLine();
```

The one-step forecast, the level, trend and seasonal updates are written out in the class comment, so a result can be reproduced by hand. The intervals are exact for the additive models (the variance sum `sigma^2 (1 + sum c[j]^2)` of Hyndman, Koehler, Ord and Snyder, with `c[j] = alpha (1 + beta (phi + ... + phi^j))`, plus gamma at multiples of the period); for a multiplicative season there is no closed form, and the same sum is used on the relative scale, which is an approximation (its measured coverage is below).

### ARIMA

`ArimaModel->New(p, d, q)` fits ARIMA(p, d, q); `New(p, d, q, D, period)` first takes `D` seasonal differences. A constant mean is estimated when nothing is differenced (`SetIncludeMean(true)` adds a drift to a differenced model). The estimator is conditional least squares: a pure autoregression is linear in its coefficients, so it is solved exactly by least squares (the same numbers as statsmodels' `AutoReg`), and with a moving-average part the Hannan-Rissanen estimate (a long autoregression for estimates of the errors, then one regression) starts a Levenberg-Marquardt iteration to the CSS minimum. Forecasts are made on the differenced series and integrated back; the forecast variance uses the psi weights of the full operator `theta(B) / (phi(B) (1-B)^d (1-B^m)^D)`, so intervals widen with the differencing.

```ruby
use System.ML.TimeSeries;

# ARMA(1,1) with phi 0.5, theta 0.4 and mean 4, simulated from seed 13
y := TimeSeries->New(Simulate->Arma([0.5], [0.4], 2.0, 1.0, 600, 13));
m := ArimaModel->New(1, 0, 1);
m->Fit(y)->PrintLine();
phi := m->GetAr();
theta := m->GetMa();
"phi {$phi[0]}  theta {$theta[0]}  mean {$m->GetMean()}"->PrintLine();     # 0.5083, 0.3567, 3.7843
"AIC {$m->GetAic()}  AICc {$m->GetAicc()}  BIC {$m->GetBic()}"->PrintLine();
"stationary {$m->IsStationary()}, invertible {$m->IsInvertible()}"->PrintLine();

# residual check: Ljung-Box over 12 lags, 2 fitted ARMA parameters (the residuals start with NaN)
lb := Acf->LjungBox(SeriesTransform->DropNaN(m->GetResiduals()), 12, 2);
"Ljung-Box p {$lb[1]}"->PrintLine();

f := m->Forecast(24, 0.95);                      # reverts to the mean, the interval widens to the series' own spread
```

The information criteria are `AIC = -2 logLik + 2k`, `AICc = AIC + 2k(k+1)/(N-k-1)` and `BIC = -2 logLik + k ln N` with the Gaussian conditional log-likelihood at `sigma^2 = SSE / N` and `k = p + q + 1 (mean) + 1 (variance)`. They are comparable only between models with the same `d`, `D` and CSS start (`SetCssStart`), which `AutoArima` arranges.

### Choosing automatically

```ruby
use System.ML.TimeSeries;

y := TimeSeries->New(Simulate->RandomWalk(200, 0.3, 1.0, 17));      # a random walk with drift
auto := AutoArima->New(2, 2, 2, 1, SelectionCriterion->Kind->AICC);   # max p, max q, max d, period (1 = none), criterion
if(auto->Fit(y)) {
  "{$auto->GetName()}, criterion {$auto->GetBestCriterion()}"->PrintLine();
  names := auto->GetCandidateNames();
  scores := auto->GetCandidateCriteria();
  each(i : names) {
    "  {$names[i]}  {$scores[i]}"->PrintLine();
  };
};

ets := AutoEts->New(12, SelectionCriterion->Kind->AICC);            # period 12: also the seasonal candidates
```

`AutoArima` decides `D` (a period of at least 2 and a seasonal strength above 0.64 give one seasonal difference), then `d` by repeated ADF tests on the seasonally differenced series (up to `max d`), then fits every `p` in 0..max p and `q` in 0..max q on the same CSS start and keeps the smallest AICc (or AIC, BIC). A candidate whose autoregressive part is not stationary or whose moving-average part is not invertible is dropped, as `auto.arima` does: conditional least squares can reach such solutions by exploiting the start-up values. A drift is included when `d + D` is at most 1. `AutoEts` fits simple, Holt and damped Holt and, given a period and two cycles of data, the additive Holt-Winters variants (and the multiplicative ones when every value is positive) and keeps the smallest AICc. Both are `Forecaster`s, so they work inside the cross-validation below.

### Does it forecast well? Rolling-origin evaluation

```ruby
use System.ML.TimeSeries;

y := TimeSeries->New(Simulate->TrendSeasonal(300, 20.0, 0.3, 5.0, 12, 0.5, 33));
hw := ExponentialSmoothing->HoltWinters(12, false);

# fit on the first 120 values, forecast 6, move the origin 6, refit; the model passed in is copied per fold, never changed
cv := Evaluation->RollingOrigin(hw, y, 120, 6, 6, 0.9, 12);   # model, series, initial, horizon, step, interval level, period for MASE
"{$cv->GetFolds()} folds: MAE {$cv->GetMae()}  RMSE {$cv->GetRmse()}  MASE {$cv->GetMase()}"->PrintLine();
"90% interval covered {$cv->GetCoverage()} of the actual values, mean width {$cv->GetMeanWidth()}"->PrintLine();
by_step := cv->GetRmseByHorizon();                            # one RMSE per step ahead
errors := cv->GetErrors();                                    # folds x horizon, actual - forecast

# the metrics on their own
a := [10.0, 20.0, 30.0];
f := [12.0, 18.0, 33.0];
"{$ForecastMetrics->Mae(a, f)} {$ForecastMetrics->Rmse(a, f)} {$ForecastMetrics->Smape(a, f)}"->PrintLine();
```

`ForecastMetrics` has `Me`, `Mae`, `Rmse`, `Mape` (percent; NaN when an actual value is 0), `Smape` (percent, in 0..200, a 0/0 term counts 0), `Mase(actual, forecast, train, m)` (scaled by the in-sample seasonal-naive error, below 1 beats it), `Coverage(actual, lower, upper)` and `MeanWidth`. `RollingOrigin` skips an origin where the model cannot be fitted and counts it in `GetSkipped()`; if no origin works, the result is invalid and says so.

### A whole workflow

[`programs/examples/ml/timeseries_forecast.obs`](../../programs/examples/ml/timeseries_forecast.obs) generates six years of monthly data, decomposes it, tests it, compares seasonal naive, Holt-Winters, a seasonal ARIMA and `AutoEts` by walk-forward cross-validation, forecasts six months with intervals and lets `AutoArima` choose. It prints this on every machine:

```
72 months, period 12, missing: 0
seasonal strength 0.960069
ADF statistic -1.039854 against the 5% value -2.904440: stationary = false
suggested differences: 1 ordinary, 1 seasonal
seasonal naive  rmse 18.241529  mae 18.036771  mase 1.015036  coverage 1.000000 (5 folds)
Holt-Winters  rmse 2.438605  mae 1.670300  mase 0.094043  coverage 0.800000 (5 folds)
seasonal ARIMA  rmse 2.767833  mae 2.050673  mase 0.115432  coverage 0.933333 (5 folds)
AutoEts  rmse 2.438605  mae 1.670300  mase 0.094043  coverage 0.800000 (5 folds)
+1: 206.311991  [202.267345, 210.356636]
Ljung-Box p-value of the Holt-Winters residuals 0.130601 (above 0.05: nothing left to model)
AutoArima chose ARIMA(0,0,0)D1[12], AICc 294.762905, from 8 candidates
```

### How accurate is it? Measured against references

The tests (`ml_timeseries_transform`, `_smoothing`, `_arima`, `_eval` and `_edges`, 810 checks in all) compare the library with numbers computed in Python by `programs/regression/ml_timeseries_ref/gen_reference.py` (statsmodels 0.15, scipy, numpy and pandas; never this library) on series the test generates from fixed seeds (the generator is reproduced in Python bit for bit). Worst relative differences measured:

| What | Reference | Worst difference |
|---|---|---|
| ACF, PACF, autocovariance, Ljung-Box, rolling mean and std | statsmodels, pandas | 2e-15 |
| ADF statistic and critical values, fixed lags and `autolag="AIC"` (14 series and regressions, same lag count) | `adfuller` | 2e-14 |
| Classical decomposition, additive and multiplicative (trend, figure, remainder) | `seasonal_decompose` | 1e-14 |
| Box-Cox transform; maximum-likelihood power | scipy | 1e-15; within the 0.01 grid step |
| Special functions behind the intervals (log-gamma, normal quantile, chi-square CDF) | `math`, scipy | 2e-15 |
| Baseline forecasts, standard deviations and bounds (5 methods) | numpy, FPP3 formulas | 4e-16 |
| Holt, damped Holt, Holt-Winters (additive, multiplicative, no trend, damped): SSE and 14-step forecasts at fixed parameters | statsmodels with known initial states | 3e-15 |
| The same, fitted: SSE no worse than the best of 25 L-BFGS-B starts; forecasts, intervals and AIC / AICc / BIC at that optimum | scipy | forecasts 2e-9, sd 8e-8, criteria 5e-13 |
| AR(1), AR(2) coefficients, intercept and variance | statsmodels `AutoReg` (least squares) | 1e-15 |
| ARMA(1,1), MA(1), ARIMA(0,1,1), (1,1,0), drift, seasonal differencing: coefficients, SSE, forecasts, psi-weight standard deviations, AIC, AICc, BIC | conditional least squares in scipy (`least_squares`) | 2e-8 |
| AR / MA / ARMA coefficients | statsmodels state-space maximum likelihood | within 0.03 |
| Simulated AR(1) 0.7, MA(1) 0.6, ARMA(1,1) 0.5 / 0.4 with mean 4 | the true values | 0.695, 0.617, 0.508 / 0.357 / 3.78 |
| MAE, RMSE, MAPE, sMAPE, MASE | numpy | 1e-15 |

Two things the comparison showed. statsmodels forecasts a step that is a multiple of the seasonal period (h = 12 for monthly data) with the seasonal value from before the last update; the textbook forecast, which this library and the reference recursion use, takes the updated one, so that step is left out of the statsmodels comparison (the difference is 0.1%). And conditional least squares can find a lower sum of squares for a high-order ARMA by exploiting the start-up values (an ARMA(3,2) on AR(2) data had a 7% lower SSE than the true AR(2), and the stationarity and invertibility check rejected it); that is why `AutoArima` drops non-stationary and non-invertible candidates.

Prediction-interval coverage, measured by rolling-origin evaluation on simulated data (a good interval covers its nominal share of the future values):

| Model and data | Level | Out-of-sample coverage |
|---|---|---|
| ARIMA(1,0,0), AR(1) 0.7, 6 seeds, 90 origins x 5 steps each (450 forecasts) | 80% | 0.758 to 0.838, mean 0.803 |
| the same | 95% | 0.936 to 0.964 |
| Holt-Winters additive, trend + season, 30 origins x 6 steps | 90% | 0.933 |
| Holt-Winters multiplicative (approximate interval), 12 origins x 6 steps | 90% | 0.931 |

On the trend-and-season series Holt-Winters and the seasonal ARIMA beat the seasonal naive forecast by a factor of 5 or more out of sample (12 steps ahead, RMSE 0.49, 0.66 and 3.65), and the Holt-Winters MASE is 0.11.

### Limits and caveats

- **One series, evenly spaced.** There are no exogenous regressors and no multivariate models. Timestamps are kept and extended (`FutureTimestamps`) but the models treat the observations as equally spaced. Missing values must be filled first.
- **Exponential smoothing** is the additive-error family with an additive trend and an additive or multiplicative season; there is no multiplicative trend and no multiplicative-error (ETS(M,*,*)) model. The initial states come from a rule, not from the optimizer, so the fit is not the exact maximum likelihood of the ETS state-space model. The optimizer is a local method from three fixed starts; a series with several distinct optima can end in a poor one (the tests check the optimum against 25 other starts on the test series). The multiplicative-season interval is an approximation.
- **ARIMA** is conditional least squares, not exact maximum likelihood: on 400 to 600 points the two agree within about 0.03 on the coefficients, and on short series they can differ more. The seasonal part is differencing only: there are no seasonal AR or MA terms, so a seasonally differenced series with a strong seasonal moving-average structure (the common "airline" model) leaves seasonal autocorrelation in the residuals; check them with `Acf->LjungBox`. Intervals do not include coefficient uncertainty. Orders are limited to p, q at most 20, d at most 3, D at most 2. The information criteria are conditional ones.
- **Stationarity.** `ADF` has no p-value and no KPSS counterpart. `SuggestDiff` and `SuggestSeasonalDiff` are heuristics for a first guess, not a replacement for looking at the plot.
- **Cost.** Everything is O(n) per evaluation. The five tests together run in about a second with the JIT (18 seconds with `--jit=off`, almost all of it the cross-validation). Ten Holt-Winters fits on 300 points plus an `AutoArima` grid of nine models on 500 points took 0.12 seconds with the JIT and 2.4 seconds with `--jit=off`.
- **Numbers across platforms.** Results are deterministic on a platform. The random generator is integer arithmetic only (xorshift64), so a seed gives the same uniform numbers everywhere; the normals and every fitted value agree between platforms to the last digit or two (libm differences), so compare with a tolerance, not for equality.

### API summary

`-lib ml_timeseries,gen_collect` (or `-lib @ai`). Bundle `System.ML.TimeSeries`.

| Class | What it does | Main members |
|---|---|---|
| `TimeSeries` | the container | `New(values)`, `New(values, times)`, `SetPeriod`, `Check`, `Impute(TimeSeries->Missing)`, `Slice`, `IsRegular`, `GetStep`, `FutureTimestamps`, `GetValues` |
| `SeriesTransform` | transforms | `Diff`, `Undiff`, `Integrate`, `Log`, `BoxCox`, `InvBoxCox`, `BoxCoxLambda`, `RollingMean`, `RollingStd`, `LagMatrix`, `LagTarget`, `DropNaN`, `Decompose` (returns a `Decomposition`) |
| `Acf` | autocorrelation | `Compute`, `Partial`, `Autocovariance`, `LjungBox`, `Bound` |
| `Stationarity` | unit-root test | `Adf` (returns an `AdfResult`), `SuggestDiff`, `SuggestSeasonalDiff`, `DefaultLags` |
| `Forecaster` (interface) | what every model implements | `Fit(TimeSeries)`, `Forecast(h, level)` (returns a `ForecastResult`: `GetMean`, `GetLower`, `GetUpper`, `GetSd`), `GetFitted`, `GetResiduals`, `GetError`, `IsFitted`, `Copy`, `GetName` |
| `BaselineForecaster` | naive, seasonal naive, drift, mean, moving average | `New(Kind)`, `New(Kind, param)`, `GetSigma` |
| `ExponentialSmoothing` | simple, Holt, damped Holt, Holt-Winters | `Simple`, `Holt`, `HoltWinters`, `SetParameters`, `GetAlpha` / `GetBeta` / `GetGamma` / `GetPhi`, `GetSse`, `GetAic` / `GetAicc` / `GetBic` |
| `ArimaModel` | ARIMA(p, d, q), optional seasonal differencing | `New(p, d, q)`, `New(p, d, q, D, period)`, `SetIncludeMean`, `SetCssStart`, `GetAr`, `GetMa`, `GetMean`, `GetSigma2`, `GetAic` / `GetAicc` / `GetBic`, `IsStationary`, `IsInvertible` |
| `AutoArima`, `AutoEts` | grid selection by `SelectionCriterion->Kind` (AIC, AICC, BIC) | `GetBest`, `GetBestCriterion`, `GetCandidateNames`, `GetCandidateCriteria` |
| `ForecastMetrics` | point-forecast accuracy | `Me`, `Mae`, `Rmse`, `Mape`, `Smape`, `Mase`, `Coverage`, `MeanWidth` |
| `Evaluation` | walk-forward validation | `RollingOrigin(model, series, initial, horizon, step, level, period)` returns a `CvResult` |
| `Simulate`, `Rng` | deterministic test data | `Arma`, `TrendSeasonal`, `MultSeasonal`, `RandomWalk`; `Rng->New(seed)` with `NextFloat`, `NextGaussian` |
| `NelderMead`, `Objective` | the optimizer, usable on its own | `Minimize(objective, x0, step, maxEvals, tol)` |
| `TsMath` | numerical helpers | `NormalQuantile`, `NormalCdf`, `ChiSquareCdf`, `LogGamma`, `Solve`, `Invert`, `Ols` |

Tested by `ml_timeseries_transform.obs`, `ml_timeseries_smoothing.obs`, `ml_timeseries_arima.obs`, `ml_timeseries_eval.obs` and `ml_timeseries_edges.obs`; the references are in [`programs/regression/ml_timeseries_ref`](../../programs/regression/ml_timeseries_ref).
