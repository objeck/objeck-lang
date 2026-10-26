# `System.ML` anomaly detection — Design

**Status:** approved direction (2026-10-26), for v2026.10.2. Written against `master`
`8e7cc56257`. Code lands in three stages after this document, each its own PR.

## Why

A university cybersecurity-ML lab, *Anomaly Detection Sensitivity Across 7 Algorithms*, has
students run seven detectors over labelled network-style data and then, for three of them, sweep
a sensitivity setting and report precision, recall, F1 and false-positive rate:

| Detector | Sensitivity setting swept | Low / Medium / High |
|---|---|---|
| Z-Score | absolute threshold | 3.0 / 2.5 / 2.0 |
| Elliptic Envelope | contamination | 0.01 / 0.05 / 0.10 |
| LOF | contamination | 0.01 / 0.05 / 0.10 |
| DBSCAN | eps | 1.5 / 1.0 / 0.5 |
| One-Class SVM | nu | 0.01 / 0.05 / 0.10 |
| Isolation Forest | contamination | 0.01 / 0.05 / 0.10 |
| Autoencoder | reconstruction-error percentile | 99.5 / 97.5 / 95.0 |

`System.ML` today has `DBSCAN`, `KMeans`, `GaussianMixture`, `PCA`, `KDTree`, a one-hidden-layer
`NeuralNetwork`, `Metrics` (hard-label and score-based) and `Matrix2D`. It has none of Z-score,
Elliptic Envelope, LOF, Isolation Forest or an autoencoder detector, nothing that ties "a
detector" to "a sensitivity setting", and no confusion-count object with F1 and FPR together.
One-Class SVM is being added separately (kernel SVM work in `ml_svm.obs`); this design gives it
an interface to implement and does not implement it.

The bar is **scikit-learn parity where scikit-learn defines the method** (same defaults, same
threshold rule, same strict/non-strict comparisons, same edge handling where reasonable), so a
student can port a notebook and get the same flags, and every difference is written down here.

## 1. The common interface

```objeck
interface OutlierDetector {
  method : virtual : public : Fit(X : Float[,]) ~ Bool;
  method : virtual : public : FitPredict(X : Float[,]) ~ Bool[];
  method : virtual : public : Predict(X : Float[,]) ~ Bool[];
  method : virtual : public : Score(X : Float[,]) ~ Float[];
  method : virtual : public : DecisionFunction(X : Float[,]) ~ Float[];
  method : virtual : public : GetTrainingScores() ~ Float[];
  method : virtual : public : GetThreshold() ~ Float;
  method : virtual : public : IsFitted() ~ Bool;
  method : virtual : public : SetSensitivity(value : Float) ~ Bool;
  method : virtual : public : GetSensitivity() ~ Float;
  method : virtual : public : GetSensitivityName() ~ String;
  method : virtual : public : GetError() ~ String;
}
```

### 1.1 Labels: `Bool`, `true` = outlier

`Predict` and `FitPredict` return `Bool[]` with **`true` meaning outlier**. That is the opposite
polarity of scikit-learn's `+1` inlier / `-1` outlier, and it is deliberate: in this library the
anomaly is the *positive* class, so a prediction feeds `Metrics->Precision/Recall/F1Score`, the
new `ConfusionMatrix` and the sweep directly, with the labels students already have (`true` =
attack). Mapping: `sklearn == -1` ⇔ `ours == true`.

### 1.2 Scores: higher = more anomalous

`Score(X)` returns one `Float` per row, **higher meaning more anomalous**, so it is a valid input to
`Metrics->AucRoc`, `RecallAtFpr`, `AveragePrecision` and `Bootstrap` with no negation.
scikit-learn's `score_samples` is the other way round (higher = more normal). Per detector:

| Detector | `Score` | scikit-learn `score_samples` |
|---|---|---|
| ZScoreDetector | combined \|z\| (default: max over features) | — (no sklearn class) |
| EllipticEnvelope | squared Mahalanobis distance | `-mahalanobis(X)` |
| LocalOutlierFactor | LOF (≈1 inlier, >1 outlier) | `-LOF` |
| IsolationForest | `2^(-E[h(x)]/c(ψ))` in (0, 1] | `-2^(-E[h(x)]/c(ψ))` |
| AutoencoderDetector | per-row mean squared reconstruction error | — |
| DbscanDetector | distance to the nearest core sample | — (no `score_samples`) |

So `Score == -score_samples` exactly wherever both exist.

### 1.3 Threshold, `DecisionFunction`, strictness

`GetThreshold()` is the cut on `Score`; it equals `-offset_` in scikit-learn.
`DecisionFunction(X) = Score(X) - GetThreshold()`; **positive means outlier**, which is
`-decision_function` in scikit-learn. A row is an outlier when its score is **strictly greater**
than the threshold, matching scikit-learn's `predict` (`decision_function < 0` → `-1`). Note that
`Metrics->AtThreshold` counts `score >= threshold` as positive; the difference only matters for a
row whose score equals the threshold exactly, and the sweep uses `Predict`, not `AtThreshold`.

### 1.4 Contamination → threshold

Where scikit-learn has `contamination`, the threshold is the **contamination quantile of the
training scores**: `offset_ = np.percentile(score_samples(X_train), 100 * contamination)` with
numpy's default linear interpolation, i.e. our threshold is the `(1 - contamination)` quantile of
`GetTrainingScores()` (linear interpolation is symmetric under negation, so these are the same
number). `Metrics->Percentile` already implements that interpolation.

`contamination = 0.0` means scikit-learn's `"auto"` where scikit-learn has one: IsolationForest
uses the paper's fixed cut `0.5` (`offset_ = -0.5`), LOF uses `1.5` (`offset_ = -1.5`). Otherwise
contamination must lie in `(0, 0.5]`; anything else makes `Fit` return `false` with `GetError()`
saying why.

### 1.5 `FitPredict` and training scores

`FitPredict(X)` fits and labels the training rows; `GetTrainingScores()` returns the scores the
threshold was taken from. For most detectors that is `Score(X_train)`. For LOF without novelty it
is the leave-self-out LOF of each training row (scikit-learn's `negative_outlier_factor_`, negated),
and `Predict`/`Score` on new data are refused (`Nil`, with an error) exactly as scikit-learn raises
— scoring the training rows "as new" would count each row as its own nearest neighbour. For DBSCAN
the training labels are the noise points.

### 1.6 Sensitivity

`SetSensitivity(v)` sets the detector's one lab-facing knob (`GetSensitivityName()` names it:
`threshold`, `contamination`, `percentile`, `eps`, `nu`). The detector decides whether that needs a
refit:

* **Threshold-only knobs** — z-score threshold, autoencoder percentile, and *contamination* for
  EllipticEnvelope, LOF and IsolationForest. In scikit-learn contamination is applied at fit time
  but touches nothing except `offset_`: the forest, the MCD estimate and the neighbour graph are
  independent of it. So a fitted detector re-derives its threshold from its stored training scores
  and stays fitted. The tests prove this is identical to a fresh fit with the same seed.
* **Model knobs** — DBSCAN `eps`, One-Class SVM `nu`: `SetSensitivity` marks the detector unfitted
  and the next `Fit` rebuilds it.

### 1.7 Inputs, determinism, errors

* `X` is rows × features, `Float[,]`. `Fit` refuses (returns `false`, `GetError()` set) `Nil`,
  zero rows, zero columns, and **any NaN**, naming the first NaN's row and column; `Predict`/`Score`
  refuse a NaN row or a column-count mismatch by returning `Nil`. NaN is refused rather than
  imputed because every one of these methods would otherwise silently rank it somewhere.
* Every random choice uses `System.ML.Random` (seeded Lehmer generator). Each randomised detector
  takes a `seed`, default `42`. Same seed, same data → bit-identical scores, at every `-opt` level
  and with and without the JIT (tested).
* No detector raises: a float division by zero aborts the VM in Objeck, so every denominator is
  guarded and the guard's result documented.

## 2. Detectors

### 2.1 `ZScoreDetector` (Stage 1)

Fit stores each feature's mean and **population** standard deviation (ddof = 0, as
`scipy.stats.zscore`). `z_ij = (x_ij - mean_j) / sd_j`.

* Multi-feature rule, the constructor's `ZScoreDetector->Rule->...`: **`MAX` (default)** — a row's score is
  `max_j |z_ij|`, i.e. "any feature beyond the threshold" (the usual lab formulation
  `(np.abs(z) > t).any(axis=1)`). `MEAN` — mean |z|. `RMS` — `sqrt(mean_j z_ij²)`, a
  diagonal-covariance Mahalanobis distance scaled by `1/sqrt(p)`.
* Threshold: the absolute value itself (default **3.0**); sensitivity name `threshold`. No
  contamination; a lower threshold flags more.
* Constant feature (`sd = 0`): `|z| = 0` when the value equals the training constant, `+Inf`
  otherwise — a feature that never varied in training and now differs is maximally surprising.
* Exact: tests compare against a pure-Python computation to 1e-12.

### 2.2 `IsolationForest` (Stage 1)

Liu, Ting & Zhou (2008), as scikit-learn implements it.

* Parameters: `n_estimators` **100**, `max_samples` **`min(256, n)`** (0 = auto), `contamination`
  **auto**, `seed` 42. `max_features = 1.0` and `bootstrap = false` only (the sklearn defaults).
* Each tree: draw ψ rows **without replacement**; grow to `max_depth = ceil(log2(ψ))`; at a node
  pick a feature uniformly among those **not constant in the node** (a node where every feature is
  constant is a leaf, as sklearn's splitter does), split uniformly in `[min, max)`, `x <= split`
  goes left.
* Path length: `h(x) = depth(leaf) + c(n_leaf)` where `n_leaf` is the number of training rows that
  reached the leaf, and `c(n) = 2 H(n-1) - 2(n-1)/n` with `H(i) = ln(i) + 0.5772156649`,
  `c(1) = 0`, `c(2) = 1` (sklearn's `_average_path_length`).
* `Score = 2^(-mean_t h_t(x) / c(ψ))`.
* Tests: `c(n)` exactly against the formula; scores against an independent numpy forest
  (different RNG) by rank correlation > 0.99 with large forests; injected-outlier detection;
  flagged fraction ≈ contamination.

### 2.3 `LocalOutlierFactor` (Stage 1)

Breunig et al. (2000), as scikit-learn implements it.

* Parameters: `k` (`n_neighbors`) **20**, capped at `n - 1` as sklearn does; `contamination`
  **auto** (threshold 1.5); `novelty` **false**. Euclidean metric only.
* Neighbours via the existing `KDTree` (exact). In training, a row's neighbours exclude the row
  itself; if duplicates crowd it out of the `k + 1` results, the first zero-distance neighbour is
  dropped instead (sklearn's `kneighbors(X=None)` rule), so distances agree either way.
* `k-distance(o)` = distance to o's k-th neighbour; `reach(a, o) = max(k-distance(o), d(a, o))`;
  `lrd(a) = 1 / (mean_o reach(a, o) + 1e-10)` (the `1e-10` is sklearn's guard for more than `k`
  duplicates); `LOF(a) = mean_o lrd(o) / lrd(a)`.
* `novelty = true` scores new rows against the training graph (no self-exclusion); `false` refuses
  `Predict`/`Score` and exposes training LOFs through `FitPredict`/`GetTrainingScores`.
* LOF is deterministic, so tests compare with a numpy brute-force reference to **1e-6**.
* Fewer than 2 rows: `Fit` refuses.

### 2.4 `EllipticEnvelope` (Stage 1)

Rousseeuw & Van Driessen's FastMCD, following `sklearn.covariance.fast_mcd`/`MinCovDet`.

* Parameters: `contamination` **0.1**, `support_fraction` **auto** = `h = ceil((n + p + 1) / 2)`
  rows (else `h = floor(fraction * n)`), `seed` 42.
* `p = 1`: the exact univariate MCD — the shortest contiguous window of `h` sorted values, location
  the mean of the midpoints, support the `h` rows closest to it.
* Starts are the **paper's random `(p + 1)`-subsets** (grown a row at a time while singular),
  extended to the `h` rows nearest their estimate. scikit-learn starts from random `h`-subsets
  instead; in a numpy experiment on 30 contaminated datasets (200 × 3), sklearn's 30 `h`-subset
  starts reached the lowest determinant on 11, 500 `(p + 1)`-subset starts on 24 to 28. On
  Gaussian inliers many `h`-subsets are near-ties, so the search is a heuristic either way.
* `p > 1`, `n <= 500`: 500 starts, two C-steps each, keep the best 50 by log-determinant, then
  C-steps to convergence (≤ 30) and take the lowest determinant.
* `n > 500`: sklearn's nested scheme with the paper's cap — split a permutation into
  `min(5, n / 300)` subsets (300 rows each once there are 5), 10 best of `max(10, 500 / subsets)`
  trials on each, pool on a merged sample of `min(1500, n)` rows (best 10 if `n > 1500`, else 1),
  then finish on the full data. (sklearn does not cap the subset count, so above 1,800 rows it
  runs `n / 300` subsets; at 100k rows that is 333.)
* A C-step: Mahalanobis distances under the current estimate, keep the `h` smallest, recompute mean
  and MLE covariance; stop when the determinant stops falling.
* Then the two corrections `MinCovDet.fit` applies: **consistency** — scale the raw covariance by
  `median(d²) / χ²_p.ppf(0.5)`; **reweighting** — keep rows with corrected `d² < χ²_p.ppf(0.975)`
  and recompute mean and MLE covariance from them.
* `Score` = squared Mahalanobis distance under the reweighted estimate; threshold the
  `(1 - contamination)` quantile of the training distances.
* χ² quantiles come from a regularised lower incomplete gamma (series / continued fraction),
  inverted by bisection, checked against the closed-form CDFs; exposed as
  `EllipticEnvelope->ChiSquareQuantile(dof, prob)`. The degrees of freedom are the *varying*
  features (sklearn passes all of them, constants included).
* At `q >= 6` varying features and 128+ rows the quadratic forms and covariances go through
  `Matrix2D->DotProduct` (native); smaller problems stay in Objeck.
* **Singular covariance:** a feature constant over the training data is excluded from the
  Mahalanobis form (exactly what sklearn's `pinvh` does for an exactly constant column); if the
  support covariance is still singular — collinear features, or `h` rows in a lower-dimensional
  subspace, e.g. many duplicates — `Fit` **refuses** with an error naming the cause, rather than
  regularising silently and returning distances in a metric nobody chose. `n <= p` is refused.
* Tests: Mahalanobis against an independent inverse; the univariate MCD exactly against sklearn's
  algorithm; on 30 contaminated datasets against a numpy MCD run from 600 starts to convergence:
  within 0.005 of its log-determinant everywhere, exactly on it on ≥ 24 (26 measured), reweighted
  location/covariance within 0.05/0.10, identical flagged rows on ≥ 28 (29 measured) and never more
  than 2 rows apart; plus the `n > 500`, `n > 1500` and native paths.

### 2.5 `AutoencoderDetector` and `DenseNetwork` (Stage 2)

The existing `NeuralNetwork` cannot be reused: it has one hidden layer, sigmoid units and a
`Bool` query, and is kept unchanged. A new general `DenseNetwork` goes in `ml_core.obs`:

* Layers of any sizes; a hidden and an output activation from `DenseNetwork->Activation`:
  `LINEAR`, `RELU`, `SIGMOID`, `TANH`. Glorot-uniform weights from the seed, zero biases.
* Loss: mean squared error. Optimiser: **Adam** (`lr` 1e-3, β₁ 0.9, β₂ 0.999, ε 1e-8, as
  Keras/PyTorch), minibatches (default 32) over a seeded shuffle each epoch.
* `Fit(X, Y, epochs) ~ Float` (final epoch loss), `Predict(X) ~ Float[,]`, weight get/set for
  testing.

`AutoencoderDetector` = `DenseNetwork` trained on `X → X`:

* Parameters: hidden sizes (default `[ceil(p/2), ceil(p/4), ceil(p/2)]`, bottleneck ≥ 1), ReLU
  hidden layers and a linear output, `epochs` 50, `batch` 32, `lr` 1e-3, `seed` 42,
  `percentile` **95.0**, `standardize` **true** (features are z-scaled with the training mean/sd
  before the network; constant features scale to 0).
* `Score` = per-row mean squared reconstruction error (in the standardised space).
* Threshold = the `percentile`-th percentile (linear interpolation) of the **training**
  reconstruction errors; sensitivity name `percentile`. 99.5 → 0.5% of training rows above it.
* Tests: forward pass, gradients and Adam steps against a numpy reference given the same initial
  weights and the same shuffle (the Lehmer generator is replayed in Python), to 1e-9; reconstruction
  error separates injected outliers (AUC); determinism.

### 2.6 `DbscanDetector` (Stage 1)

The lab sweeps DBSCAN's `eps`, so DBSCAN needs the same interface. `DbscanDetector(eps, min_points)`
(defaults **0.5, 5**, sklearn's) labels as outliers exactly DBSCAN's noise: rows that are not core
and have no core row within `eps`. The existing `DBSCAN` class is unchanged; the tests assert the
two agree row for row. `Score` = distance to the nearest core training row (0 for a core row), the
threshold is `eps`, and `Predict` on new rows applies the same rule. Sensitivity name `eps`; changing
it requires a refit. Distance computations are brute force (as `DBSCAN`'s are): O(n²).

### 2.7 One-Class SVM (not in these stages)

Owned by the kernel-SVM work. Once both land, `OneClassSVM` implements `OutlierDetector`
(`Score = -decision_function`, sensitivity `nu`, refit on change) and the sweep and lab example
pick it up with no other change. Until then the example prints that row as "not available".

## 3. Metrics

`Metrics` already has hard-label `Accuracy`, `Precision`, `Recall`, `F1Score`, `MCC` and
`ConfusionMatrix` (as an `Int[,]`), and score-based `AucRoc`, `RecallAtFpr`, `AtThreshold`.
Stage 3 adds:

* `ConfusionMatrix` — a class: `New(predictions : Bool[], actuals : Bool[])`, `GetTruePositives`,
  `GetFalsePositives`, `GetTrueNegatives`, `GetFalseNegatives`, `GetAccuracy`, `GetPrecision`,
  `GetRecall`, `GetF1`, `GetFalsePositiveRate`, `GetSpecificity`, `GetFlagged`, `ToString`.
  Zero denominators return **0.0**, as scikit-learn's `zero_division=0`: precision when nothing was
  flagged, recall when there are no real positives, FPR when there are no real negatives, F1 when
  precision + recall = 0, accuracy on an empty sample. Mismatched or `Nil` inputs give an all-zero
  matrix and `IsValid() = false`.
* `Metrics->FalsePositiveRate(predictions, actuals)` — FP / (FP + TN), next to `Precision`/`Recall`.

## 4. `SensitivitySweep`

```objeck
result := SensitivitySweep->Run(detector, [3.0, 2.5, 2.0], ["Low", "Medium", "High"], X, y);
result->ToTable()->PrintLine();
```

* `Run(detector, settings, labels, X, y)` — transductive, the lab's usual shape: for each setting,
  `SetSensitivity`, `FitPredict(X)` if the detector is unfitted (else reuse the fitted model and
  `GetTrainingScores()` against the new threshold), and score the labels against `y`.
* `Run(detector, settings, labels, X_train, X_test, y_test)` — inductive: fit on train (refitting
  only when the setting requires it), `Predict(X_test)`.
* Returns a `SweepResult`: per setting the label, the value, the flagged count and a
  `ConfusionMatrix`; `ToTable()` renders `setting | value | flagged | TP FP TN FN | precision
  recall F1 FPR`. A setting the detector rejects is recorded with its error and the sweep continues.

## 5. Lab example

`programs/examples/ml/anomaly_sweep.obs`: a documented synthetic dataset — 2,000 inliers from a
correlated 6-feature Gaussian mixture (two clusters) plus 100 labelled anomalies (scattered uniform,
shifted-mean and correlation-breaking points), generated from a fixed seed — run through all
detectors at the lab's Low/Medium/High settings, printing one table each, and AUC from the scores.
One-Class SVM is included through the interface when present.

## 6. Test plan

New regression tests `programs/regression/ml_outlier_*.obs` (PASS/FAIL, `EXTRA_LIBS: ml,gen_collect,csv`),
each run at `-opt s3` and `s0`, under `obr`, `obr --jit=off`, and with `OBJECK_JIT_THRESHOLD=1`:

1. **Reference values**: numpy/pure-Python references in `programs/regression/ml_outlier_ref/`
   (scikit-learn is not installed on the dev box; nothing is pip-installed) generate fixtures that
   the Objeck tests embed: z-scores exact; LOF to 1e-6; Mahalanobis exact; MCD vs a many-start
   reference on 30+ contaminated-Gaussian seeds; `c(n)` exact; IF rank correlation; autoencoder
   trajectory to 1e-9; χ² quantiles vs tables.
2. **Behaviour**: injected outliers detected at high rates; contamination controls the flagged
   training fraction (within `1/n` + ties); Low < Medium < High monotone in flagged count; scores
   give AUC > 0.9 on separable data through `Metrics->AucRoc`; `SetSensitivity` without refit equals a
   fresh fit.
3. **Edges**: constant feature, singular covariance (refused), `n < k` (k capped), `n <= p`
   (refused), duplicates (LOF finite), NaN (refused, row/col named), empty / `Nil`, column mismatch.
4. **Determinism**: same seed twice → identical; the suite runs every mode above.
5. **Performance**: 10k and 100k rows × 20 features per detector, Windows x64 JIT, reported in the
   PRs and summarised here when measured. LOF and DBSCAN are neighbour-search bound (KD-trees prune
   little at 20 dimensions; DBSCAN is O(n²)), so their 100k numbers are expected to be the slow ones.

## 7. Files

| Stage | Files |
|---|---|
| 0 | `docs/ML_OUTLIERS_DESIGN.md` |
| 1 | `core/compiler/lib_src/ml_outlier.obs` (new: interface, Z-score, IF, LOF, EE, DBSCAN adapter); library lists (`build_libs.sh`, `ci-build.yml`, `release-build.yml`, `code_doc64.*`, `gen_json.*`, `test.cmd`, `frameworks/ml/build.cmd`, `core/lib/README.md`); `core/lib/ml.obl`; LSP index; tests |
| 2 | `ml_core.obs` (`DenseNetwork`), `ml_outlier.obs` (`AutoencoderDetector`); tests |
| 3 | `ml_data.obs` (`ConfusionMatrix`, `Metrics->FalsePositiveRate`), `ml_outlier.obs` (`SensitivitySweep`, `SweepResult`); `programs/examples/ml/`; tests; docs |

Every stage rebuilds all libraries; `ml.obl` changes for real, and libraries compiled against
`ml` (`query_ml.obl`) are checked by decompressed comparison to differ only by shifted class ids.
