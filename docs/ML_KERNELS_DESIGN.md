# Kernel SVMs for `System.ML`: design

**Status:** design, approved for v2026.10.2 (2026-10-26). Written against `master`
`8e7cc56257`. Stages land as separate PRs in the order of the table at the end.

## Why

A university cybersecurity-ML lab runs three SVM sweeps on NSL-KDD (about 125k
training rows x 41 features, mixed categorical, binary normal/attack):

1. change the kernel among `linear`, `rbf`, `poly` and `sigmoid`, other settings
   fixed, and compare accuracy, recall and training time;
2. change `C` among 0.01, 0.1, 1, 10, 100 and compare training and testing
   performance;
3. fix `kernel=rbf` and vary `gamma` among 0.001, 0.01, 0.1, 1.

`System.ML` has one SVM today: `SVM` in `ml_linear.obs`, a **linear** hinge-loss
model trained by full-batch subgradient descent with `lambda`, `learning_rate` and
`iterations`. It has no kernel, no `C` and no `gamma`, so none of the three sweeps
can be expressed. The lab's random-forest sweeps hit smaller gaps
(`min_samples_leaf`, an unlimited `max_depth`), covered in Stage 3.

`SVM` stays as it is. It is a different model (primal, regularised by `lambda`,
approximately optimised) and existing programs depend on its exact output. Its doc
comment will point at `KernelSVM` for anyone who needs a kernel or scikit-learn's
parameterisation.

## The API

New file `core/compiler/lib_src/ml_svm.obs`, compiled into `ml.obl` with the other
`ml_*.obs` sources (the library is one `obc` invocation over a source list, so the
new file is added to that list and to the five doc lists that
`tools/cicd/check_doc_lists.py` keeps in step).

```objeck
enum SvmKernel { LINEAR, RBF, POLY, SIGMOID }

class KernelSVM implements ScoreModel {
  New(kernel : SvmKernel);                                  # C=1, gamma='scale'
  New(kernel : SvmKernel, c : Float);
  New(kernel : SvmKernel, c : Float, gamma : Float);
  New(kernel : SvmKernel, c : Float, gamma : Float, degree : Int, coef0 : Float);

  # configuration (call before Fit)
  SetC(c : Float), SetGamma(gamma : Float), SetGammaScale(), SetGammaAuto(),
  SetDegree(degree : Int), SetCoef0(coef0 : Float), SetTolerance(tol : Float),
  SetMaxIterations(max_iter : Int), SetCacheSize(megabytes : Int),
  SetShrinking(on : Bool), SetClassWeight(negative : Float, positive : Float),
  SetClassWeightBalanced()

  Fit(X : Float[,], y : Bool[]) ~ Bool;           # true = positive class
  DecisionFunction(X : Float[,]) ~ Float[];        # signed, higher = more positive
  PredictScore(X : Float[,]) ~ Float[];            # = DecisionFunction (ScoreModel)
  PredictClass(X : Float[,]) ~ Bool[];             # DecisionFunction > 0
  Score(X : Float[,], y : Bool[]) ~ Float;         # accuracy

  GetSupportVectorCount() ~ Int;  GetSupportIndices() ~ Int[];
  GetSupportVectors() ~ Float[,]; GetDualCoefficients() ~ Float[];   # alpha_i * y_i
  GetIntercept() ~ Float;         GetObjective() ~ Float;
  GetIterations() ~ Int;          IsConverged() ~ Bool;
  GetFitSeconds() ~ Float;        GetLastError() ~ String;   IsFitted() ~ Bool;
  GetKernel(), GetC(), GetGamma(), GetDegree(), GetCoef0(), GetTolerance(),
  GetMaxIterations(), GetCacheSize(), IsShrinking()
}

class OneClassSVM implements ScoreModel {     # Stage 2
  New(kernel : SvmKernel, nu : Float);   New(kernel, nu, gamma);
  New(kernel, nu, gamma, degree, coef0);  same setters for gamma/tol/cache/...
  Fit(X : Float[,]) ~ Bool;
  Fit(X : Float[,], y : Bool[]) ~ Bool;    # ScoreModel: y is ignored
  DecisionFunction(X) ~ Float[];   # sum alpha_i K(x_i, x) - rho; > 0 inlier
  ScoreSamples(X) ~ Float[];       # sum alpha_i K(x_i, x); sklearn's score_samples
  Predict(X) ~ Int[];              # +1 inlier, -1 outlier (sklearn's convention)
  IsOutlier(X) ~ Bool[];           # true = outlier (decision < 0)
  PredictScore(X) ~ Float[];       # -DecisionFunction: higher = more anomalous
  ...the same model accessors as KernelSVM
}
```

Conventions match the newer siblings (`LogisticRegression`'s `Bool[]` overload,
`DecisionTreeClassifier`, `RandomForestClassifier`): labels are `Bool[]`, `Fit`
returns `false` instead of raising on bad input, and implementing `ScoreModel`
means `CrossValidation->Evaluate` takes a `KernelSVM` with no adapter.
`GetLastError` is new: it names *why* `Fit` returned `false` (`"y holds one class
only"`, `"X and y row counts differ"`, `"X holds NaN or infinity"`, `"C must be >
0"` ...), because the lab's first mistake will be a subsample that drew one class.

Binary classification only. Multiclass (one-vs-one in libsvm) is out of scope: the
lab is binary, and every other classifier in the bundle is binary too.

### Mapping to scikit-learn

| scikit-learn `SVC` | `KernelSVM` | notes |
|---|---|---|
| `kernel='linear'/'rbf'/'poly'/'sigmoid'` | `SvmKernel->LINEAR/RBF/POLY/SIGMOID` | `precomputed` and callables are out of scope |
| `C=1.0` | `c`, default 1.0 | must be > 0 |
| `gamma='scale'` (default) | `SetGammaScale()`, the default | `1 / (n_features * X.var())`, the variance of **every entry** of X, population (ddof=0); 1.0 when that variance is 0 -- exactly sklearn's rule |
| `gamma='auto'` | `SetGammaAuto()` | `1 / n_features` |
| `gamma=<float>` | `New(kernel, c, gamma)` / `SetGamma` | must be >= 0 |
| `degree=3` | `degree`, default 3 | poly only; must be >= 0 |
| `coef0=0.0` | `coef0`, default 0.0 | poly and sigmoid |
| `tol=1e-3` | `SetTolerance`, default 1e-3 | libsvm's `eps`: stop when the maximal KKT violation `m(a) - M(a) < tol` |
| `shrinking=True` | `SetShrinking`, default on | |
| `cache_size=200` (MB) | `SetCacheSize`, default 200 | MB of kernel rows; see the cache section |
| `max_iter=-1` | `SetMaxIterations`, default -1 | `<= 0` means libsvm's own cap, `max(10,000,000, 100 * n)` |
| `class_weight=None/dict/'balanced'` | `SetClassWeight(neg, pos)` / `SetClassWeightBalanced()` | per-class `C_i = C * w(y_i)`; balanced is `n / (2 * n_class)` |
| `decision_function` | `DecisionFunction` | sign: positive means the `true` class (sklearn's `classes_[1]` for `{False, True}`) |
| `predict` | `PredictClass` | `decision > 0` |
| `support_`, `support_vectors_`, `dual_coef_`, `intercept_`, `n_iter_` | `GetSupportIndices`, `GetSupportVectors`, `GetDualCoefficients`, `GetIntercept`, `GetIterations` | `support_` is in training-row order here; sklearn groups it by class |
| `probability=True` | -- | out of scope, see below |
| `random_state` | -- | only used by sklearn's probability CV; the fit itself is deterministic |

The kernels are libsvm's:

| kernel | K(x, z) |
|---|---|
| LINEAR | x . z |
| RBF | exp(-gamma * \|\|x - z\|\|^2) |
| POLY | (gamma * x . z + coef0)^degree |
| SIGMOID | tanh(gamma * x . z + coef0) |

`gamma` is used by RBF, POLY and SIGMOID; LINEAR ignores it.

`OneClassSVM` maps to `sklearn.svm.OneClassSVM(kernel, nu=0.5, gamma='scale',
degree, coef0, tol, shrinking, cache_size, max_iter)`: same gamma rules, `nu` in
(0, 1], `decision_function`, `score_samples` (= decision + rho, sklearn's
`offset_`), and `predict` returning +1/-1.

## The solver

One private SMO solver serves both classes. It is libsvm's `Solver` (Fan, Chen
and Lin, "Working Set Selection Using Second Order Information for Training
SVM", JMLR 2005), transcribed rather than reinvented, because its behaviour is the
reference every SVM user's intuition is built on:

    minimise  f(a) = 1/2 a'Qa + p'a
    subject to  y'a = Delta,  0 <= a_i <= C_i

* **C-SVC:** `Q_ij = y_i y_j K(x_i, x_j)`, `p = -1`, `Delta = 0`, `a = 0` at start.
* **One-class:** `Q_ij = K(x_i, x_j)`, `p = 0`, `y = +1`, `C_i = 1`, `Delta = nu * n`.
  The start is libsvm's: the first `floor(nu * n)` alphas at 1, the next at the
  remainder, the rest 0.

**Working-set selection** is libsvm's WSS3: `i` maximises `-y_t G_t` over
`I_up`, `j` minimises `-(b_ij)^2 / a_ij` over `I_low` with `b_ij > 0` (the second-
order gain), `a_ij <= 0` replaced by `tau = 1e-12`. Stop when
`max_{I_up} -y G - min_{I_low} -y G < tol`.

**The two-variable update** and the clipping to the box are libsvm's
`Solver::Solve` lines, including the `y_i != y_j` and `y_i = y_j` cases; the
gradient is updated over the active set and `G_bar` (the gradient contribution of
bounded alphas) is maintained for un-shrinking.

**Shrinking** is libsvm's: every `min(n, 1000)` iterations, variables at a bound
whose gradient says they will stay there are moved out of the active set
(physically swapped to the end, as libsvm does, so the active set is a prefix and a
partial kernel row covers it); once the violation is within `10 * tol` the
gradient is reconstructed and shrinking restarts from the full set; the final
optimality check is always on the full set. The bias is libsvm's `calculate_rho`:
the mean of `y G` over free variables, or the midpoint of the bound interval when
there are none.

**Kernel-row cache.** An LRU of kernel rows, sized in MB by `SetCacheSize` (default
200, as sklearn). Rows are `Float` (8 bytes; libsvm stores 4-byte `float`). With
`n` training rows the cache holds `max(2, MB * 2^20 / (8 * n))` rows, at most `n`;
rows are pre-allocated slots in one `Float[slots, n]` block, so an eviction recycles
a slot instead of allocating and the collector sees one array, not thousands. A row
may be filled only up to the current active size (libsvm's partial rows) and is
extended on demand. Swaps during shrinking swap the cached columns, and drop a row
that is filled between the two indexes, exactly as libsvm's `Cache::swap_index`.
The cache changes speed, never results: a kernel value is the same whether it was
cached or recomputed.

**`max_iter` reached:** `Fit` still returns `true` and the model is usable (sklearn
warns and carries on); `IsConverged()` is `false` and `GetIterations()` equals the
cap. A caller that needs a converged model checks `IsConverged`.

### Numerics and determinism

* Kernel values are double precision, so results agree with libsvm/sklearn to
  libsvm's float32 rounding of `Q` (about 1e-7 relative), not bit for bit.
* RBF uses the explicit `sum (x_k - z_k)^2`, not `|x|^2 + |z|^2 - 2 x.z`, which can
  go negative through cancellation.
* There is no randomness in fitting. The result is a pure function of `X` (and its
  row order), `y` and the parameters, so no seed is needed or offered. Tests assert
  bit-identical decision values across two fits, across `-opt s0`/`s3`, JIT on and
  off, and `OBJECK_JIT_THRESHOLD=1`.
* `X` is copied at `Fit`; the caller may reuse its matrix.

## Complexity, memory and where it stops being practical

Per iteration: two kernel rows (each `active * d` on a cache miss, free on a hit)
plus an `O(active)` gradient update and an `O(active)` selection scan. Iterations
grow roughly linearly with `n` for easy problems and faster for large `C` or large
`gamma` (more free support vectors). Overall that is between `O(n^2 d)` and
`O(n^3)`, the same as libsvm; sklearn's own documentation says SVC "is hard to
scale to datasets with more than a couple of 10000 samples".

Memory: a copy of `X` (`8 n d` bytes: 16 MB at 50k x 41), the cache (the configured
MB), and a handful of `O(n)` vectors. Prediction costs `n_sv * d` per row.

A probe on this machine (JIT on, 41 features) measured an RBF kernel row of 20,000
entries at 1.5 ms and a 20,000-entry gradient update at 0.04 ms, so a 20k-row fit
of ~40k iterations with a 50% cache hit rate is on the order of a minute; Stage 1
replaces this estimate with measured fits at 5k, 20k and 50k rows and states the
practical ceiling. The full 125,973-row NSL-KDD training set is not a sensible
target for any exact kernel SVM (libsvm included); the lab examples subsample with
a documented, stratified, seeded draw, and the docs say so plainly. Interpreted
(`--jit=off`) the same work is about 40x slower: fine for tests, not for the lab.

## `DecisionFunction` contract

* One `Float` per row of `X`, Nil when unfitted or the column count differs from
  `Fit`'s.
* Higher means more positive (`true`, i.e. attack). `0` is the boundary
  `PredictClass` uses; the values are margins, not probabilities, and are not
  bounded.
* Feed it directly to `Metrics->AucRoc`, `Metrics->RecallAtFpr`,
  `Metrics->ThresholdAtFpr` and `CrossValidation->Evaluate`; pick an operating
  point with `ThresholdAtFpr` rather than assuming 0.
* For `OneClassSVM`, `DecisionFunction` keeps sklearn's sign (positive = inlier);
  `PredictScore` is its negation so that "higher = more anomalous" holds for the
  metric functions, whose positive class is the attack.
* One deliberate difference from LIBSVM (found while testing stage 2): when no
  alpha is free, every rho in `[lb, ub]` is optimal and LIBSVM takes the
  midpoint. A one-class fit at `nu = 1` puts every alpha at its bound, `ub` is
  infinite, and so is LIBSVM's rho and every decision value. The solver takes the
  finite end of the interval instead. A C-SVC fit cannot reach this case.

## Probability outputs

Out of scope. sklearn's `probability=True` fits Platt scaling by an internal
5-fold cross-validation -- five extra fits -- and its own documentation warns the
probabilities can disagree with `predict`. The fixed-FPR metrics this library is
built around need a score, not a probability; `DecisionFunction` is that score.

## Random forest additions (Stage 3)

On `DecisionTreeClassifier` and `RandomForestClassifier` (the continuous-feature
classes in `ml_tree.obs`; the `Bool[,]` `RandomForest` is untouched):

* `min_samples_leaf` with sklearn's meaning: a split is only considered when both
  children keep at least that many rows. New constructors take it; the existing
  `min_samples` ("a node with this many rows or fewer becomes a leaf") keeps its
  meaning, so no existing call changes behaviour. In a forest the count is of
  bootstrap draws, repeats included (sklearn counts distinct rows and uses the
  repeats as weights); this is documented on the parameter.
* A negative `max_depth` means unlimited (sklearn's `None`), named by
  `DecisionTreeClassifier->UnlimitedDepth()` (-1). `0` keeps its meaning of a
  single leaf: `ml_decision_tree_classifier` relies on it, so changing it would
  break an existing caller.
* `GetFitSeconds()`, `GetFeatureImportances()` (mean decrease in Gini impurity,
  normalised per tree, averaged, normalised -- sklearn's `feature_importances_`),
  depth and leaf counts, and `Apply` (sklearn's `apply`, the leaf each row lands
  in -- also how the tests check min_samples_leaf).
* The split search is rewritten as a single sorted sweep per feature. Today each
  candidate threshold rescans every row (`SplitGini`), so a node costs
  `O(rows^2)` per feature; that makes a 500-tree sweep at NSL-KDD scale
  impractical. The sweep evaluates the same candidates in the same order with the
  same arithmetic, so it picks the same splits; old and new libraries give
  bit-identical scores, and one depth-12 tree on 2,000 x 41 rows went from 52 s to
  0.04 s. `RegressionTree` (under the gradient-boosted models) has the same
  quadratic search; it is left alone here because a prefix-sum rewrite would
  change its floating-point sums and so, at near ties, its splits.

## Test plan

scikit-learn is not installed on this machine (Windows Python 3.10 or the WSL
Ubuntu image) and is not to be installed. numpy 2.2.6 is. So the solver is
compared against references written for the purpose, in Python with numpy, sharing
no code with the Objeck solver:

1. **Exact dual optimum on tiny problems** (n <= 8): enumerate every assignment of
   each alpha to {0, free, C}, solve the KKT linear system for the free set, keep
   feasible points, take the minimum objective. Independent of SMO entirely.
2. **A plain first-order SMO** (maximal violating pair, no shrinking, no cache, no
   second-order selection) run to `tol = 1e-10` on n = 10..60.
3. On **50+ random problems per kernel** (both references, varied C, gamma, degree,
   coef0, class balance, and some duplicate rows), the Objeck fit at a tight
   tolerance must give the same dual objective to 1e-6 (relative), the same
   predictions (sign of the decision value; values within 1e-5 except where
   |f| < 1e-6), and the same support set. The support set is unique only when the
   dual optimum is (K positive definite, no repeated rows); there it must match
   exactly. Otherwise -- a linear kernel with more rows than features, a low-degree
   polynomial, duplicates -- the optimal alphas form a face, and what every
   optimum shares is checked instead: a row strictly outside the margin has
   alpha 0, a row strictly inside it alpha C; rows on the margin may take any
   value in between.
   The harness lives in `tools/ml/svm_reference/` with the problem
   generator, both references and the comparison, and its counts are reported in
   the PR; a fixed subset of the problems, with their expected objectives, is baked
   into a regression test so CI checks it without Python.
4. **KKT conditions checked inside the regression tests** after every fit, from
   the public API only: `sum alpha_i y_i = 0`; `alpha = 0 => y f >= 1 - tol`;
   `0 < alpha < C_i => |y f - 1| <= tol`; `alpha = C_i => y f <= 1 + tol`.
   For one-class the same with `f >= 0` / `= 0` / `<= 0` and `sum alpha = nu n`.
5. **Known geometry:** XOR is not learnable by LINEAR and is by RBF and POLY; a
   linearly separable set reaches zero training error at large C; a larger C never
   increases the number of margin violations on a fixed problem (checked over a C
   ladder); tiny gamma underfits toward a near-constant decision, huge gamma
   memorises the training set (every row a support vector, training accuracy 1,
   test accuracy near chance).
6. **Edge cases:** one class only -> `false` with a clear `GetLastError`;
   mismatched sizes; NaN/Inf; constant features (gamma 'scale' falls back to 1.0
   when X.var() = 0); duplicate rows, including duplicates with opposite labels; n
   < d; `max_iter` reached -> `IsConverged() = false`, still predicts; one-class
   `nu` at its extremes.
7. **nu semantics (one-class):** over a nu ladder, training outlier fraction <= nu
   and support-vector fraction >= nu (within the solver tolerance).
8. **Determinism:** two fits in one process compare bit-identical; the harness runs
   every new test at `-opt s3` and `-opt s0`, under `obr` and `obr --jit=off`, and
   with `OBJECK_JIT_THRESHOLD=1`, and diffs their outputs (which include a checksum
   of the decision values). Bit-identity holds within a platform; across
   platforms the C library's `exp` and `tanh` differ in the last bits, so Linux
   and Windows checksums agree only to about 1e-9 (seen in the stage 1 runs).
9. **Scale:** a benchmark program (not a regression test) fits 5k, 20k and 50k
   synthetic NSL-KDD-shaped rows and reports time, iterations, support vectors and
   peak memory at two cache sizes.

## Stages

| Stage | Content | PR |
|---|---|---|
| 0 | this document | docs only |
| 1 | `SvmKernel`, `KernelSVM`, the solver, references, tests, benchmark, LSP index | `ml.obl` + every library linking `ml` |
| 2 | `OneClassSVM` on the same solver, nu tests | `ml.obl` |
| 3 | RF `min_samples_leaf`, unlimited depth, importances, sweep rewrite; lab examples under `programs/examples/ml/` for the RF and SVM sweeps | `ml.obl` |

Each stage rebuilds every library and explains each changed `.obl`: `ml.obl` for
real; `query_ml.obl` (and anything else linking `ml`) shifts by the classes added,
verified by a decompressed comparison.
